#include "manualextractionwidget.h"

#include "appsettings.h" // Здесь должен быть LogCategory
#include "mediaprobe.h"
#include "ui_manualextractionwidget.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QMimeData>
#include <QRegularExpression>

ManualExtractionWidget::ManualExtractionWidget(QWidget* parent)
    : QWidget(parent), ui(new Ui::ManualExtractionWidget), m_processManager(new ProcessManager(this))
{
    ui->setupUi(this);
    setAcceptDrops(true); // Включаем прием файлов

    // Настройка колонок дерева
    ui->tracksTree->header()->setDefaultSectionSize(120);
    ui->tracksTree->setColumnWidth(0, 250);

    QSettings settings("MyCompany", "DubbingTool");
    const QByteArray headerState = settings.value("ui/manualExtractionTracksHeader").toByteArray();
    if (!headerState.isEmpty())
    {
        ui->tracksTree->header()->restoreState(headerState);
    }
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
    QSettings settings("MyCompany", "DubbingTool");
    settings.setValue("ui/manualExtractionTracksHeader", ui->tracksTree->header()->saveState());

    delete ui;
}

// --- Drag & Drop ---

void ManualExtractionWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (m_isExtracting)
    {
        return;
    }

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
    if (m_isExtracting)
    {
        return;
    }

    const QUrl url = event->mimeData()->urls().first();
    scanFile(url.toLocalFile());
}

// --- Основная логика ---

void ManualExtractionWidget::onBrowseClicked()
{
    if (m_isExtracting)
    {
        return;
    }

    QSettings settings("MyCompany", "DubbingTool");
    QString lastDir = settings.value("ui/lastBrowseDir").toString();
    if (lastDir.isEmpty() && !ui->inputPathEdit->text().isEmpty())
    {
        lastDir = QFileInfo(ui->inputPathEdit->text()).absolutePath();
    }

    QString path = QFileDialog::getOpenFileName(this, "Выбор файла", lastDir, "Video (*.mkv *.mp4)");
    if (!path.isEmpty())
    {
        settings.setValue("ui/lastBrowseDir", QFileInfo(path).absolutePath());
        scanFile(path);
    }
}

void ManualExtractionWidget::scanFile(const QString& path)
{
    if (m_isExtracting)
    {
        return;
    }

    m_currentFile = path;
    ui->inputPathEdit->setText(path);
    ui->tracksTree->clear();

    // Блокируем интерфейс на время сканирования
    ui->extractButton->setEnabled(false);
    ui->chapterTimingsButton->setEnabled(false);

    emit logMessage("Сканирование структуры файла...", LogCategory::APP);
    QString mkvmergeExe = AppSettings::instance().mkvmergePath();
    if (mkvmergeExe.isEmpty() || !QFileInfo::exists(mkvmergeExe))
    {
        emit logMessage("Ошибка: mkvmerge не найден в настройках!", LogCategory::APP, LogLevel::Error);
        return;
    }

    // mkvmerge -J дает детальную инфу о кодеках и вложениях
    QByteArray output;
    QStringList args = {"--identify", "--identification-format", "json", m_currentFile};

    if (m_processManager->executeAndWait(mkvmergeExe, args, output))
    {
        parseMkvJson(output);
        ui->extractButton->setEnabled(true);
        ui->chapterTimingsButton->setEnabled(true);
        emit logMessage("Сканирование завершено. Выберите дорожки для извлечения.", LogCategory::APP);
    }
    else
    {
        ui->chapterTimingsButton->setEnabled(false);
        emit logMessage("Ошибка: не удалось просканировать файл", LogCategory::APP, LogLevel::Error);
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
    const MediaProbeResult result = MediaProbe::parseMkvMergeJson(data);
    m_durationSec = static_cast<double>(result.durationNs) / 1000000000.0;

    QTreeWidgetItem* videoRoot = new QTreeWidgetItem(ui->tracksTree, {"Видео"});
    QTreeWidgetItem* audioRoot = new QTreeWidgetItem(ui->tracksTree, {"Аудио"});
    QTreeWidgetItem* subsRoot = new QTreeWidgetItem(ui->tracksTree, {"Субтитры"});
    QTreeWidgetItem* attachRoot = new QTreeWidgetItem(ui->tracksTree, {"Шрифты / Вложения"});

    // Раскрываем по умолчанию
    videoRoot->setExpanded(true);
    audioRoot->setExpanded(true);
    subsRoot->setExpanded(true);
    attachRoot->setExpanded(false);

    auto addTrackItem = [this](QTreeWidgetItem* parent, const MediaTrackInfo& track, const QString& type)
    {
        QString combinedInfo = track.codec + " " + track.codecId;
        QString ext = getExtensionForMkvCodec(combinedInfo, type);
        QTreeWidgetItem* item = new QTreeWidgetItem(parent);
        QString displayName = track.trackName.isEmpty() ? type : track.trackName;
        item->setText(0, displayName);
        item->setText(1, QString::number(track.id));
        item->setText(2, ext);
        item->setText(3, track.language);
        item->setText(4, combinedInfo);

        // Сохраняем данные для экстракта
        item->setData(0, Qt::UserRole, "track");
        item->setData(1, Qt::UserRole, track.id);
        item->setData(2, Qt::UserRole, ext);
        item->setData(3, Qt::UserRole, type);
        item->setData(4, Qt::UserRole, track.typeIndex);

        item->setCheckState(0, Qt::Unchecked);
    };

    for (const auto& track : result.videoTracks)
    {
        addTrackItem(videoRoot, track, "video");
    }
    for (const auto& track : result.audioTracks)
    {
        addTrackItem(audioRoot, track, "audio");
    }
    for (const auto& track : result.subtitleTracks)
    {
        addTrackItem(subsRoot, track, "subtitles");
    }

    for (const auto& att : result.attachments)
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(attachRoot);
        item->setText(0, att.fileName);
        item->setText(1, QString::number(att.id));
        item->setText(2, "attach");
        item->setText(4, att.contentType);

        item->setData(0, Qt::UserRole, "attachment");
        item->setData(1, Qt::UserRole, att.id);
        item->setData(2, Qt::UserRole, att.fileName);

        // Авто-выбор шрифтов, если это шрифт
        if ((att.contentType.contains("font") || att.fileName.endsWith(".ttf") || att.fileName.endsWith(".otf")) &&
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
                const QString trackType = (*it)->data(3, Qt::UserRole).toString();
                const int typeIndex = (*it)->data(4, Qt::UserRole).toInt();
                QString lang = (*it)->text(3);

                if (trackType == "audio")
                {
                    ext = isMkv ? "mka" : "m4a";
                }

                QString outName = QString("%1_track%2_%3.%4").arg(baseName).arg(id).arg(lang).arg(ext);
                QString outPath = QDir(sourceDir).filePath(outName);

                QString ffmpegMap;
                if (trackType == "video")
                {
                    ffmpegMap = QString("0:v:%1").arg(typeIndex);
                }
                else if (trackType == "audio")
                {
                    ffmpegMap = QString("0:a:%1").arg(typeIndex);
                }
                else if (trackType == "subtitles")
                {
                    ffmpegMap = QString("0:s:%1").arg(typeIndex);
                }
                else
                {
                    ffmpegMap = QString("0:%1").arg(id);
                }

                if (trackType == "audio")
                {
                    if (ffmpegArgs.isEmpty())
                    {
                        ffmpegArgs << "-y" << "-i" << m_currentFile;
                    }
                    ffmpegArgs << "-map" << ffmpegMap << "-c" << "copy" << outPath;
                }
                else if (isMkv)
                {
                    mkvextractTracks << QString("%1:%2").arg(id).arg(outPath);
                }
                else
                {
                    ffmpegArgs << "-map" << ffmpegMap << "-c" << "copy" << outPath;
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

    m_taskQueue.clear();

    if (isMkv)
    {
        QString mkvextractExe = AppSettings::instance().mkvextractPath();
        if (!mkvextractTracks.isEmpty())
        {
            QStringList args;
            args << "tracks" << m_currentFile << mkvextractTracks;
            m_taskQueue.enqueue({mkvextractExe, args, "Извлечение дорожек..."});
        }

        if (!mkvextractAttach.isEmpty())
        {
            QStringList args;
            args << "attachments" << m_currentFile << mkvextractAttach;
            m_taskQueue.enqueue({mkvextractExe, args, "Извлечение вложений..."});
        }

        if (!ffmpegArgs.isEmpty())
        {
            QString ffmpegExe = AppSettings::instance().ffmpegPath();
            m_taskQueue.enqueue({ffmpegExe, ffmpegArgs, "Извлечение аудио (ffmpeg)..."});
        }
    }
    else
    {
        if (!ffmpegArgs.isEmpty())
        {
            QString ffmpegExe = AppSettings::instance().ffmpegPath();
            m_taskQueue.enqueue({ffmpegExe, ffmpegArgs, "Извлечение дорожек (ffmpeg)..."});
        }
    }

    if (m_taskQueue.isEmpty())
    {
        emit logMessage("Нет задач для выполнения.", LogCategory::APP);
        return;
    }

    ui->extractButton->setEnabled(false);
    ui->chapterTimingsButton->setEnabled(false);
    m_isExtracting = true;
    emit extractionStarted();

    emit progressUpdated(0, "Начало извлечения...");
    emit logMessage("Запуск извлечения...", LogCategory::APP);

    runNextTask();
}

void ManualExtractionWidget::runNextTask()
{
    if (m_taskQueue.isEmpty())
    {
        m_isExtracting = false;
        ui->extractButton->setEnabled(true);
        ui->chapterTimingsButton->setEnabled(!m_currentFile.isEmpty());
        emit logMessage("Извлечение завершено успешно.", LogCategory::APP);
        emit progressUpdated(100, "Готово");
        emit extractionFinished(true);
        return;
    }

    ExtractionTask task = m_taskQueue.dequeue();
    m_currentTaskIsMkvextract = task.program.contains(QLatin1String("mkvextract"), Qt::CaseInsensitive);
    emit logMessage(task.description, LogCategory::APP);
    m_processManager->startProcess(task.program, task.args);
}

void ManualExtractionWidget::cancel()
{
    m_taskQueue.clear();
    m_isExtracting = false;
    if (m_processManager)
    {
        m_processManager->killProcess();
    }
}

void ManualExtractionWidget::on_chapterTimingsButton_clicked()
{
    if (!m_currentFile.isEmpty())
    {
        emit chapterTimingsRequested(m_currentFile);
    }
}

void ManualExtractionWidget::onProcessFinished(int exitCode)
{
    if (m_processManager != nullptr && m_processManager->wasKilled())
    {
        m_taskQueue.clear();
        m_isExtracting = false;
        ui->extractButton->setEnabled(true);
        ui->chapterTimingsButton->setEnabled(!m_currentFile.isEmpty());
        emit logMessage("Извлечение отменено пользователем.", LogCategory::APP);
        emit progressUpdated(0, "Отменено");
        emit extractionFinished(false);
        return;
    }

    if (m_currentTaskIsMkvextract && exitCode == 1)
    {
        emit logMessage(QStringLiteral("mkvextract завершился с предупреждением (код 1)."), LogCategory::MKVTOOLNIX,
                        LogLevel::Warning);
        runNextTask();
        return;
    }

    if (exitCode != 0)
    {
        m_taskQueue.clear();
        m_isExtracting = false;
        ui->extractButton->setEnabled(true);
        ui->chapterTimingsButton->setEnabled(!m_currentFile.isEmpty());
        emit logMessage(QString("Ошибка извлечения (код %1).").arg(exitCode), LogCategory::APP, LogLevel::Error);
        emit progressUpdated(0, "Ошибка");
        emit extractionFinished(false);
        return;
    }

    runNextTask();
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
    if (output.contains(QStringLiteral("time=")) && m_durationSec > 0.0)
    {
        static const QRegularExpression re(QStringLiteral(R"(time=(\d+):(\d+):(\d+)(?:\.(\d+))?)"));
        const QRegularExpressionMatch match = re.match(output);
        if (match.hasMatch())
        {
            const int h = match.captured(1).toInt();
            const int m = match.captured(2).toInt();
            const int s = match.captured(3).toInt();
            const QString frac = match.captured(4);
            double subSec = 0.0;
            if (!frac.isEmpty())
            {
                subSec = frac.toDouble() / ((frac.length() >= 3) ? 1000.0 : 100.0);
            }
            const double currentSec =
                static_cast<double>(h) * 3600.0 + static_cast<double>(m) * 60.0 + static_cast<double>(s) + subSec;
            int percent = static_cast<int>((currentSec / m_durationSec) * 100.0);
            percent = qBound(0, percent, 100);
            emit progressUpdated(percent, QStringLiteral("Извлечение..."));
        }
    }

    if (!output.trimmed().isEmpty())
    {
        emit logMessage(output.trimmed(), LogCategory::FFMPEG);
    }
}