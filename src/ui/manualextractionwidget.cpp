#include "manualextractionwidget.h"

#include "appsettings.h" // Здесь должен быть LogCategory
#include "ui_manualextractionwidget.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

ManualExtractionWidget::ManualExtractionWidget(QWidget* parent)
    : QWidget(parent), ui(new Ui::ManualExtractionWidget), m_processManager(new ProcessManager(this))
{
    ui->setupUi(this);
    setAcceptDrops(true); // Включаем прием файлов

    // Настройка колонок дерева
    ui->tracksTree->header()->setDefaultSectionSize(120);
    ui->tracksTree->setColumnWidth(0, 250);
    connect(ui->extractFontsCheckBox, &QCheckBox::toggled, this,
            [this](bool checked)
            {
                QTreeWidgetItemIterator it(ui->tracksTree);
                while (*it)
                {
                    if ((*it)->data(0, Qt::UserRole).toString() == "attachment")
                    {
                        (*it)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
                    }
                    ++it;
                }
            });

    connect(ui->browseButton, &QPushButton::clicked, this, &ManualExtractionWidget::onBrowseClicked);
    connect(ui->extractButton, &QPushButton::clicked, this, &ManualExtractionWidget::onExtractClicked);
    connect(m_processManager, &ProcessManager::processFinished, this, &ManualExtractionWidget::onProcessFinished);
    connect(m_processManager, &ProcessManager::processOutput, this, &ManualExtractionWidget::onProcessStdOut);
    connect(m_processManager, &ProcessManager::processStdErr, this, &ManualExtractionWidget::onProcessStdErr);
}

ManualExtractionWidget::~ManualExtractionWidget()
{
    delete ui;
}

// --- Drag & Drop ---

void ManualExtractionWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        QList<QUrl> urls = event->mimeData()->urls();
        if (urls.size() == 1)
        {
            QString suffix = QFileInfo(urls.first().toLocalFile()).suffix().toLower();
            if (suffix == "mkv" || suffix == "mp4" || suffix == "mks")
            {
                event->acceptProposedAction();
            }
        }
    }
}

void ManualExtractionWidget::dropEvent(QDropEvent* event)
{
    const QUrl url = event->mimeData()->urls().first();
    scanFile(url.toLocalFile());
}

// --- Основная логика ---

void ManualExtractionWidget::onBrowseClicked()
{
    QString path = QFileDialog::getOpenFileName(this, "Выбор файла", "", "Video (*.mkv *.mp4)");
    if (!path.isEmpty())
    {
        scanFile(path);
    }
}

void ManualExtractionWidget::scanFile(const QString& path)
{
    m_currentFile = path;
    ui->inputPathEdit->setText(path);
    ui->tracksTree->clear();

    // Блокируем интерфейс на время сканирования
    ui->extractButton->setEnabled(false);

    emit logMessage("Сканирование структуры файла...", LogCategory::APP);
    QString mkvmergeExe = AppSettings::instance().mkvmergePath();
    if (mkvmergeExe.isEmpty() || !QFileInfo::exists(mkvmergeExe))
    {
        emit logMessage("Ошибка: mkvmerge не найден в настройках!", LogCategory::APP);
        return;
    }

    // mkvmerge -J дает детальную инфу о кодеках и вложениях
    QByteArray output;
    QStringList args = {"--identify", "--identification-format", "json", m_currentFile};

    if (m_processManager->executeAndWait(mkvmergeExe, args, output))
    {
        parseMkvJson(output);
        ui->extractButton->setEnabled(true);
        emit logMessage("Сканирование завершено. Выберите дорожки для извлечения.", LogCategory::APP);
    }
    else
    {
        emit logMessage("Ошибка: не удалось просканировать файл", LogCategory::APP);
    }
}

QString ManualExtractionWidget::getExtensionForMkvCodec(const QString& codecId, const QString& trackType)
{
    QString cid = codecId.toUpper();

    if (trackType == "video")
    {
        // ПЕРВЫМ ДЕЛОМ ищем HEVC (H.265)
        if (cid.contains("HEVC") || cid.contains("H265") || cid.contains("HVC1") || cid.contains("MPEGH"))
        {
            return "h265"; // FFmpeg поймет это как HEVC bitstream
        }

        // ВТОРЫМ ДЕЛОМ ищем AVC (H.264)
        if (cid.contains("AVC") || cid.contains("H264") || cid.contains("MPEG4") || cid.contains("AVC1"))
        {
            return "h264"; // FFmpeg поймет это как AVC bitstream
        }

        if (cid.contains("AV1"))
            return "ivf";
        if (cid.contains("VP9") || cid.contains("VP8"))
            return "ivf";

        return "h264"; // Fallback
    }

    if (trackType == "audio")
    {
        if (cid.contains("AAC"))
            return "aac";
        if (cid.contains("EAC3"))
            return "eac3";
        if (cid.contains("AC3"))
            return "ac3";
        if (cid.contains("DTS"))
            return "dts";
        if (cid.contains("FLAC"))
            return "flac";
        if (cid.contains("OPUS"))
            return "opus";
        if (cid.contains("VORBIS"))
            return "ogg";
        if (cid.contains("MP3") || cid.contains("MPEG/L3"))
            return "mp3";

        return "aac";
    }

    if (trackType == "subtitles")
    {
        if (cid.contains("ASS") || cid.contains("SSA") || cid.contains("S_TEXT/ASS"))
            return "ass";
        if (cid.contains("UTF8") || cid.contains("SRT") || cid.contains("TEXT/UTF8"))
            return "srt";
        if (cid.contains("PGS") || cid.contains("SUP") || cid.contains("S_HDMV/PGS"))
            return "sup";
        return "ass";
    }

    return "bin";
}

void ManualExtractionWidget::parseMkvJson(const QByteArray& data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();
    if (root.contains("container") && root["container"].toObject().contains("properties"))
    {
        double durationNs = root["container"].toObject()["properties"].toObject()["duration"].toDouble();
        m_durationSec = durationNs / 1000000000.0;
    }

    QTreeWidgetItem* videoRoot = new QTreeWidgetItem(ui->tracksTree, {"Видео"});
    QTreeWidgetItem* audioRoot = new QTreeWidgetItem(ui->tracksTree, {"Аудио"});
    QTreeWidgetItem* subsRoot = new QTreeWidgetItem(ui->tracksTree, {"Субтитры"});
    QTreeWidgetItem* attachRoot = new QTreeWidgetItem(ui->tracksTree, {"Шрифты / Вложения"});

    // Раскрываем по умолчанию
    videoRoot->setExpanded(true);
    audioRoot->setExpanded(true);
    subsRoot->setExpanded(true);
    attachRoot->setExpanded(false);

    // 1. Парсим дорожки
    QJsonArray tracks = root["tracks"].toArray();
    for (const QJsonValue& v : tracks)
    {
        QJsonObject track = v.toObject();
        QJsonObject props = track["properties"].toObject();

        QString type = track["type"].toString();
        int id = track["id"].toInt();
        QString codecName = track["codec"].toString();
        QString codecIdProp = props["codec_id"].toString();
        QString combinedInfo = codecName + " " + codecIdProp;
        QString ext = getExtensionForMkvCodec(combinedInfo, type);

        QString lang = props.contains("language") ? props["language"].toString() : "und";
        QString name = props["track_name"].toString();

        QTreeWidgetItem* parentItem = nullptr;
        if (type == "video")
            parentItem = videoRoot;
        else if (type == "audio")
            parentItem = audioRoot;
        else if (type == "subtitles")
            parentItem = subsRoot;
        else
            parentItem = ui->tracksTree->invisibleRootItem();

        QTreeWidgetItem* item = new QTreeWidgetItem(parentItem);

        // Формируем описание
        QString displayName = name.isEmpty() ? type : name;
        item->setText(0, displayName);
        item->setText(1, QString::number(id));
        item->setText(2, ext);
        item->setText(3, lang);
        item->setText(4, combinedInfo);

        // Сохраняем данные для экстракта
        item->setData(0, Qt::UserRole, "track");
        item->setData(1, Qt::UserRole, id);
        item->setData(2, Qt::UserRole, ext);

        item->setCheckState(0, Qt::Unchecked);
    }

    // 2. Парсим вложения (Шрифты)
    if (root.contains("attachments"))
    {
        QJsonArray atts = root["attachments"].toArray();
        for (const QJsonValue& v : atts)
        {
            QJsonObject att = v.toObject();
            int id = att["id"].toInt();
            QString fileName = att["file_name"].toString();
            QString mime = att["content_type"].toString();

            QTreeWidgetItem* item = new QTreeWidgetItem(attachRoot);
            item->setText(0, fileName);
            item->setText(1, QString::number(id));
            item->setText(2, "attach");
            item->setText(4, mime);

            item->setData(0, Qt::UserRole, "attachment");
            item->setData(1, Qt::UserRole, id);
            item->setData(2, Qt::UserRole, fileName);

            // Авто-выбор шрифтов, если это шрифт
            if ((mime.contains("font") || fileName.endsWith(".ttf") || fileName.endsWith(".otf")) &&
                ui->extractFontsCheckBox->isChecked())
            {
                item->setCheckState(0, Qt::Checked);
            }
            else
            {
                item->setCheckState(0, Qt::Unchecked);
            }
        }
    }
}

void ManualExtractionWidget::onExtractClicked()
{
    if (m_currentFile.isEmpty())
        return;

    QFileInfo fileInfo(m_currentFile);
    QString suffix = fileInfo.suffix().toLower();
    QString sourceDir = fileInfo.absolutePath();
    QString baseName = fileInfo.completeBaseName();

    bool isMkv = (suffix == "mkv" || suffix == "mks" || suffix == "mka");

    QStringList mkvextractTracks; // Список ID:Path
    QStringList mkvextractAttach; // Список ID:Path
    QStringList ffmpegArgs;

    if (!isMkv)
    {
        ffmpegArgs << "-y" << "-i" << m_currentFile;
    }

    bool hasWork = false;
    QTreeWidgetItemIterator it(ui->tracksTree);
    while (*it)
    {
        if ((*it)->checkState(0) == Qt::Checked)
        {
            QString mode = (*it)->data(0, Qt::UserRole).toString();
            int id = (*it)->data(1, Qt::UserRole).toInt();

            if (mode == "track")
            {
                QString ext = (*it)->data(2, Qt::UserRole).toString();
                QString lang = (*it)->text(3);

                QString outName = QString("%1_track%2_%3.%4").arg(baseName).arg(id).arg(lang).arg(ext);
                QString outPath = QDir(sourceDir).filePath(outName);

                if (isMkv)
                {
                    mkvextractTracks << QString("%1:%2").arg(id).arg(outPath);
                }
                else
                {
                    ffmpegArgs << "-map" << QString("0:%1").arg(id) << "-c" << "copy" << outPath;
                }
                hasWork = true;
            }
            else if (mode == "attachment")
            {
                if (isMkv)
                {
                    QString fileName = (*it)->data(2, Qt::UserRole).toString();
                    QString fontsDir = QDir(sourceDir).filePath("attached_fonts");
                    QDir().mkpath(fontsDir);
                    QString outPath = QDir(fontsDir).filePath(fileName);

                    mkvextractAttach << QString("%1:%2").arg(id).arg(outPath);
                    hasWork = true;
                }
            }
        }
        ++it;
    }

    if (!hasWork)
    {
        emit logMessage("Ничего не выбрано.", LogCategory::APP);
        return;
    }

    ui->extractButton->setEnabled(false);

    emit progressUpdated(0, "Начало извлечения...");
    emit logMessage("Запуск извлечения...", LogCategory::APP);

    if (isMkv)
    {
        QString mkvextractExe = AppSettings::instance().mkvextractPath();

        // --- ИЗМЕНЕНИЕ ЗДЕСЬ (стиль WorkflowManager) ---

        // 1. Извлекаем ДОРОЖКИ (tracks source.mkv ID:Path...)
        if (!mkvextractTracks.isEmpty())
        {
            QStringList args;
            args << "tracks" << m_currentFile << mkvextractTracks; // "tracks" ПЕРВЫМ

            if (!mkvextractAttach.isEmpty())
            {
                QByteArray dummy;
                m_processManager->executeAndWait(mkvextractExe, args, dummy);
            }
            else
            {
                m_processManager->startProcess(mkvextractExe, args);
                return;
            }
        }

        // 2. Извлекаем ВЛОЖЕНИЯ (attachments source.mkv ID:Path...)
        if (!mkvextractAttach.isEmpty())
        {
            QStringList args;
            args << "attachments" << m_currentFile << mkvextractAttach; // "attachments" ПЕРВЫМ
            m_processManager->startProcess(mkvextractExe, args);
        }
    }
    else
    {
        QString ffmpegExe = AppSettings::instance().ffmpegPath();
        m_processManager->startProcess(ffmpegExe, ffmpegArgs);
    }
}

void ManualExtractionWidget::onProcessFinished(int exitCode)
{
    ui->extractButton->setEnabled(true);

    if (exitCode == 0)
    {
        emit logMessage("Извлечение завершено успешно.", LogCategory::APP);
    }
    else
    {
        emit logMessage(QString("Ошибка извлечения (код %1).").arg(exitCode), LogCategory::APP);
    }
    emit progressUpdated(100, "Готово");
}

void ManualExtractionWidget::onProcessStdOut(const QString& output)
{
    if (output.contains("Progress:"))
    {
        QRegularExpression re("Progress: (\\d+)%");
        QRegularExpressionMatch match = re.match(output);
        if (match.hasMatch())
        {
            int percent = match.captured(1).toInt();
            emit progressUpdated(percent, "Извлечение...");
        }
    }

    else if (!output.trimmed().isEmpty())
    {
        emit logMessage(output.trimmed(), LogCategory::MKVTOOLNIX);
    }
}

void ManualExtractionWidget::onProcessStdErr(const QString& output)
{
    if (output.contains("time="))
    {
        QRegularExpression re("time=(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{2})");
        QRegularExpressionMatch match = re.match(output);
        if (match.hasMatch() && m_durationSec > 0)
        {
            int h = match.captured(1).toInt();
            int m = match.captured(2).toInt();
            int s = match.captured(3).toInt();
            double currentSec = h * 3600 + m * 60 + s;

            int percent = static_cast<int>((currentSec / m_durationSec) * 100);
            emit progressUpdated(percent, "Извлечение...");
        }
    }

    if (!output.trimmed().isEmpty())
    {
        emit logMessage(output.trimmed(), LogCategory::FFMPEG);
    }
}