#include "manualassemblywidget.h"

#include "appsettings.h"
#include "fontfinder.h"
#include "ui_manualassemblywidget.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantMap>

ManualAssemblyWidget::ManualAssemblyWidget(QWidget* parent)
    : QWidget(parent), m_fontFinder(new FontFinder(this)), ui(new Ui::ManualAssemblyWidget)
{
    ui->setupUi(this);

    setAcceptDrops(true);
    const auto lineEdits = findChildren<QLineEdit*>();
    for (auto* le : lineEdits)
    {
        le->setAcceptDrops(false);
    }

    connect(m_fontFinder, &FontFinder::finished, this, &ManualAssemblyWidget::onFontFinderFinished);

    const bool hasNugenAmb = AppSettings::instance().isNugenAmbAvailable();
    ui->normalizeAudioCheckBox->setEnabled(hasNugenAmb);

    connect(ui->convertAudioCheckBox, &QCheckBox::toggled, ui->convertAudioFormatComboBox, &QComboBox::setVisible);
    connect(ui->manualChaptersCustomCheckBox, &QCheckBox::toggled, this,
            [this](bool) { updateChaptersUiVisibility(); });
    connect(ui->chapterTimingsButton, &QPushButton::clicked, this,
            [this]() { emit chapterTimingsRequested(chapterTimingSourcePath()); });

    connect(ui->videoPathEdit, &QLineEdit::editingFinished, this,
            [this]() { probeVideoAudioTracks(ui->videoPathEdit->text().trimmed(), false); });

    ui->convertAudioFormatComboBox->setVisible(ui->convertAudioCheckBox->isChecked());
    updateChaptersUiVisibility();

    // Настройка пресетов авторов сабов
    ui->subAuthorComboBox->setEditable(true);
    QSettings settings("MyCompany", "DubbingTool");
    const QString lastSubAuthor = settings.value("ui/lastSubAuthor", "Дубляжная").toString();
    ui->subAuthorComboBox->setCurrentText(lastSubAuthor);

    ui->selectAudioFromVideoButton->setEnabled(false);
}

ManualAssemblyWidget::~ManualAssemblyWidget()
{
    delete ui;
}

void ManualAssemblyWidget::probeVideoAudioTracks(const QString& videoPath, bool autoPrompt)
{
    if (videoPath.isEmpty() || !QFileInfo::exists(videoPath))
    {
        m_lastProbedVideoPath.clear();
        m_videoAudioTracks.clear();
        m_selectedVideoTrackId = -1;
        m_selectedVideoAudioTrackId = -1;
        m_hasAudioInVideo = false;
        ui->selectAudioFromVideoButton->setEnabled(false);
        ui->originalAudioPathEdit->setPlaceholderText(QString());
        return;
    }

    if (videoPath == m_lastProbedVideoPath)
    {
        return;
    }

    m_lastProbedVideoPath = videoPath;
    m_videoAudioTracks.clear();
    m_selectedVideoTrackId = -1;
    m_selectedVideoAudioTrackId = -1;
    m_hasAudioInVideo = false;

    const QString ext = QFileInfo(videoPath).suffix().toLower();
    if (ext != "mp4" && ext != "mkv" && ext != "m4v" && ext != "mov" && ext != "ts")
    {
        ui->selectAudioFromVideoButton->setEnabled(false);
        ui->originalAudioPathEdit->setPlaceholderText(QString());
        return;
    }

    const QString mkvmergeExe = AppSettings::instance().mkvmergePath();
    if (mkvmergeExe.isEmpty() || !QFileInfo::exists(mkvmergeExe))
    {
        return;
    }

    QProcess process;
    process.start(mkvmergeExe, {"--identify", "--identification-format", "json", videoPath});
    if (!process.waitForFinished(5000))
    {
        process.kill();
        return;
    }

    const QByteArray output = process.readAllStandardOutput();
    const QJsonDocument doc = QJsonDocument::fromJson(output);
    const QJsonObject root = doc.object();
    const QJsonArray tracks = root["tracks"].toArray();

    for (const QJsonValue& val : tracks)
    {
        const QJsonObject track = val.toObject();
        const QString type = track["type"].toString();
        if (type == "video")
        {
            if (m_selectedVideoTrackId == -1)
            {
                m_selectedVideoTrackId = track["id"].toInt();
            }
        }
        else if (type == "audio")
        {
            ContainerAudioTrack t;
            t.id = track["id"].toInt();
            t.codec = track["codec"].toString();
            const QJsonObject props = track["properties"].toObject();
            t.language = props.contains("language") ? props["language"].toString() : "und";
            t.trackName = props["track_name"].toString();
            t.channels = props["audio_channels"].toInt();
            m_videoAudioTracks.append(t);
        }
    }

    if (m_selectedVideoTrackId == -1)
    {
        m_selectedVideoTrackId = 0;
    }

    if (m_videoAudioTracks.isEmpty())
    {
        m_hasAudioInVideo = false;
        m_selectedVideoAudioTrackId = -1;
        ui->selectAudioFromVideoButton->setEnabled(false);
        ui->originalAudioPathEdit->setPlaceholderText(QString());
    }
    else if (m_videoAudioTracks.size() == 1)
    {
        m_hasAudioInVideo = true;
        m_selectedVideoAudioTrackId = m_videoAudioTracks.first().id;
        ui->selectAudioFromVideoButton->setEnabled(true);
        const auto& t = m_videoAudioTracks.first();
        ui->originalAudioPathEdit->setPlaceholderText(
            QString("Из видео: дорожка #%1 (%2, %3)").arg(t.id).arg(t.language.toUpper()).arg(t.codec));
        if (t.language != "und" && !t.language.isEmpty())
        {
            ui->languageEdit->setText(t.language);
        }
    }
    else
    {
        m_hasAudioInVideo = true;
        ui->selectAudioFromVideoButton->setEnabled(true);
        if (autoPrompt)
        {
            promptUserForAudioTrack(m_videoAudioTracks);
        }
        else
        {
            ui->originalAudioPathEdit->setPlaceholderText(
                QString("Из видео: найдено %1 аудиодорожек (нажмите «Из видео...»)").arg(m_videoAudioTracks.size()));
        }
    }
}

bool ManualAssemblyWidget::promptUserForAudioTrack(const QList<ContainerAudioTrack>& tracks)
{
    if (tracks.isEmpty())
    {
        return false;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Выбор оригинальной аудиодорожки");
    dialog.setMinimumWidth(440);

    auto* layout = new QVBoxLayout(&dialog);
    auto* label = new QLabel(QString("В видеофайле найдено несколько аудиодорожек (%1).\n"
                                     "Выберите дорожку для использования в качестве оригинального аудио:")
                                 .arg(tracks.size()),
                             &dialog);
    layout->addWidget(label);

    auto* listWidget = new QListWidget(&dialog);
    for (int i = 0; i < tracks.size(); ++i)
    {
        const auto& t = tracks.at(i);
        QString desc = QString("Дорожка #%1: %2 (%3%4)%5")
                           .arg(t.id)
                           .arg(t.language.toUpper())
                           .arg(t.codec)
                           .arg(t.channels > 0 ? QString(", %1ch").arg(t.channels) : QString())
                           .arg(!t.trackName.isEmpty() ? QString(" — %1").arg(t.trackName) : QString());

        auto* item = new QListWidgetItem(desc, listWidget);
        item->setData(Qt::UserRole, t.id);
        if (t.id == m_selectedVideoAudioTrackId || (m_selectedVideoAudioTrackId == -1 && i == 0))
        {
            listWidget->setCurrentItem(item);
        }
    }
    layout->addWidget(listWidget);
    connect(listWidget, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    btnBox->button(QDialogButtonBox::Ok)->setText("Выбрать");
    btnBox->button(QDialogButtonBox::Cancel)->setText("Не использовать звук из видео");
    layout->addWidget(btnBox);

    connect(btnBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted && listWidget->currentItem())
    {
        m_selectedVideoAudioTrackId = listWidget->currentItem()->data(Qt::UserRole).toInt();
        ui->originalAudioPathEdit->clear();
        for (const auto& t : tracks)
        {
            if (t.id == m_selectedVideoAudioTrackId)
            {
                ui->originalAudioPathEdit->setPlaceholderText(
                    QString("Из видео: дорожка #%1 (%2, %3)").arg(t.id).arg(t.language.toUpper()).arg(t.codec));
                if (t.language != "und" && !t.language.isEmpty())
                {
                    ui->languageEdit->setText(t.language);
                }
                break;
            }
        }
        return true;
    }
    else
    {
        m_selectedVideoAudioTrackId = -1;
        ui->includeOriginalAudioCheckBox->setChecked(false);
        ui->originalAudioPathEdit->setPlaceholderText("Звук из видео отключён; укажите отдельный файл");
        return false;
    }
}

void ManualAssemblyWidget::on_selectAudioFromVideoButton_clicked()
{
    if (m_videoAudioTracks.isEmpty())
    {
        probeVideoAudioTracks(ui->videoPathEdit->text().trimmed(), false);
    }

    if (!m_videoAudioTracks.isEmpty())
    {
        promptUserForAudioTrack(m_videoAudioTracks);
    }
    else
    {
        QMessageBox::information(this, "Аудиодорожки", "В выбранном видеофайле не обнаружено аудиодорожек.");
    }
}

void ManualAssemblyWidget::updateChaptersUiVisibility()
{
    ui->manualChaptersCustomCheckBox->setVisible(true);
    const bool showPathRow = ui->manualChaptersCustomCheckBox->isChecked();
    ui->manualChaptersXmlPathRowWidget->setVisible(showPathRow);
}

bool ManualAssemblyWidget::browseForFile(QLineEdit* lineEdit, const QString& caption, const QString& filter)
{
    QSettings settings("MyCompany", "DubbingTool");
    QString lastDir = settings.value("ui/lastBrowseDir").toString();
    if (lastDir.isEmpty() && !lineEdit->text().isEmpty())
    {
        lastDir = QFileInfo(lineEdit->text()).absolutePath();
    }
    QString path = QFileDialog::getOpenFileName(this, caption, lastDir, filter);
    if (!path.isEmpty())
    {
        lineEdit->setText(path);
        settings.setValue("ui/lastBrowseDir", QFileInfo(path).absolutePath());
        return true;
    }
    return false;
}

void ManualAssemblyWidget::updateAutoOutputName(const QString& videoPath)
{
    if (videoPath.isEmpty())
    {
        return;
    }
    const QString base = QFileInfo(videoPath).completeBaseName();
    if (base.isEmpty())
    {
        return;
    }
    const QString suggested = QStringLiteral("[DUB] %1.mkv").arg(base);
    const QString current = ui->outputFileNameEdit->text().trimmed();
    if (current.isEmpty() || current == m_lastAutoOutputName)
    {
        ui->outputFileNameEdit->setText(suggested);
        m_lastAutoOutputName = suggested;
    }
}

void ManualAssemblyWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        static const QSet<QString> supportedExts = {
            QStringLiteral("mp4"),  QStringLiteral("mkv"),  QStringLiteral("m4v"),  QStringLiteral("mov"),
            QStringLiteral("ts"),   QStringLiteral("h264"), QStringLiteral("h265"), QStringLiteral("avc"),
            QStringLiteral("hevc"), QStringLiteral("wav"),  QStringLiteral("flac"), QStringLiteral("aac"),
            QStringLiteral("m4a"),  QStringLiteral("mka"),  QStringLiteral("ac3"),  QStringLiteral("eac3"),
            QStringLiteral("opus"), QStringLiteral("ass"),  QStringLiteral("srt"),  QStringLiteral("xml"),
            QStringLiteral("ttf"),  QStringLiteral("otf"),  QStringLiteral("ttc")};

        for (const QUrl& url : event->mimeData()->urls())
        {
            if (url.isLocalFile())
            {
                const QString ext = QFileInfo(url.toLocalFile()).suffix().toLower();
                if (supportedExts.contains(ext))
                {
                    event->acceptProposedAction();
                    return;
                }
            }
        }
    }
}

void ManualAssemblyWidget::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
    {
        return;
    }

    const QSet<QString> videoExts = {QStringLiteral("mp4"),  QStringLiteral("mkv"), QStringLiteral("m4v"),
                                     QStringLiteral("mov"),  QStringLiteral("ts"),  QStringLiteral("h264"),
                                     QStringLiteral("h265"), QStringLiteral("avc"), QStringLiteral("hevc")};
    const QSet<QString> audioExts = {QStringLiteral("wav"),  QStringLiteral("flac"), QStringLiteral("aac"),
                                     QStringLiteral("m4a"),  QStringLiteral("mka"),  QStringLiteral("ac3"),
                                     QStringLiteral("eac3"), QStringLiteral("opus")};
    const QSet<QString> subExts = {QStringLiteral("ass"), QStringLiteral("srt")};
    const QSet<QString> fontExts = {QStringLiteral("ttf"), QStringLiteral("otf"), QStringLiteral("ttc")};

    QString droppedVideoPath;

    for (const QUrl& url : urls)
    {
        const QString path = url.toLocalFile();
        if (path.isEmpty() || !QFileInfo::exists(path))
        {
            continue;
        }

        const QFileInfo fi(path);
        const QString ext = fi.suffix().toLower();

        if (videoExts.contains(ext))
        {
            ui->videoPathEdit->setText(path);
            ui->includeVideoCheckBox->setChecked(true);
            updateAutoOutputName(path);
            droppedVideoPath = path;
        }
        else if (audioExts.contains(ext))
        {
            if (ext == QLatin1String("wav"))
            {
                ui->russianAudioPathEdit->setText(path);
                ui->includeRussianAudioCheckBox->setChecked(true);
            }
            else if (ui->russianAudioPathEdit->text().trimmed().isEmpty())
            {
                ui->russianAudioPathEdit->setText(path);
                ui->includeRussianAudioCheckBox->setChecked(true);
            }
            else
            {
                ui->originalAudioPathEdit->setText(path);
                ui->includeOriginalAudioCheckBox->setChecked(true);
            }
        }
        else if (subExts.contains(ext))
        {
            const QString fileNameLower = fi.fileName().toLower();
            if (fileNameLower.contains(QLatin1String("sign")) || fileNameLower.contains(QString::fromUtf8("надпис")))
            {
                ui->signsPathEdit->setText(path);
                ui->includeSignsCheckBox->setChecked(true);
            }
            else if (ui->subtitlesPathEdit->text().trimmed().isEmpty())
            {
                ui->subtitlesPathEdit->setText(path);
                ui->includeSubtitlesCheckBox->setChecked(true);
            }
            else
            {
                ui->signsPathEdit->setText(path);
                ui->includeSignsCheckBox->setChecked(true);
            }
        }
        else if (ext == QLatin1String("xml"))
        {
            ui->chaptersXmlPathManualAssemblyEdit->setText(path);
            ui->manualChaptersCustomCheckBox->setChecked(true);
            updateChaptersUiVisibility();
        }
        else if (fontExts.contains(ext))
        {
            bool alreadyExists = false;
            for (int i = 0; i < ui->fontsListWidget->count(); ++i)
            {
                if (ui->fontsListWidget->item(i)->data(Qt::UserRole).toString() == path)
                {
                    alreadyExists = true;
                    break;
                }
            }
            if (!alreadyExists)
            {
                auto* item = new QListWidgetItem(fi.fileName() + tr(" - добавлен вручную"), ui->fontsListWidget);
                item->setForeground(Qt::magenta);
                item->setData(Qt::UserRole, path);
                item->setToolTip(path);
            }
        }
    }

    event->acceptProposedAction();

    if (!droppedVideoPath.isEmpty())
    {
        QTimer::singleShot(
            0, this, [this, droppedVideoPath]()
            { probeVideoAudioTracks(droppedVideoPath, ui->originalAudioPathEdit->text().trimmed().isEmpty()); });
    }
}

void ManualAssemblyWidget::on_browseVideo_clicked()
{
    if (browseForFile(ui->videoPathEdit, "Выберите видеофайл",
                      "Видеофайлы (*.mp4 *.mkv *.h264 *.h265 *.avc *.hevc *.mov *.ts);;Все файлы (*)"))
    {
        const QString path = ui->videoPathEdit->text().trimmed();
        if (!path.isEmpty())
        {
            probeVideoAudioTracks(path, true);
            updateAutoOutputName(path);
        }
    }
}

void ManualAssemblyWidget::on_browseOriginalAudio_clicked()
{
    browseForFile(ui->originalAudioPathEdit, "Выберите оригинальную аудиодорожку",
                  "Аудиофайлы (*.aac *.ac3 *.eac3 *.flac *.wav *.opus *.m4a *.mka);;Все файлы (*)");
}

void ManualAssemblyWidget::on_browseRussianAudio_clicked()
{
    browseForFile(ui->russianAudioPathEdit, "Выберите русскую аудиодорожку",
                  "Аудиофайлы (*.wav *.aac *.flac *.m4a *.mka);;Все файлы (*)");
}

void ManualAssemblyWidget::on_browseSubtitles_clicked()
{
    browseForFile(ui->subtitlesPathEdit, "Выберите файл полных субтитров", "Subtitles (*.ass *.srt)");
}

void ManualAssemblyWidget::on_browseSigns_clicked()
{
    browseForFile(ui->signsPathEdit, "Выберите файл надписей", "Subtitles (*.ass *.srt)");
}

void ManualAssemblyWidget::on_analyzeSubsButton_clicked()
{
    QStringList filesToAnalyze;
    if (!ui->subtitlesPathEdit->text().isEmpty())
    {
        filesToAnalyze << ui->subtitlesPathEdit->text();
    }
    if (!ui->signsPathEdit->text().isEmpty())
    {
        filesToAnalyze << ui->signsPathEdit->text();
    }

    if (filesToAnalyze.isEmpty())
    {
        QMessageBox::warning(this, "Ошибка", "Выберите хотя бы один файл субтитров для анализа.");
        return;
    }

    ui->analyzeSubsButton->setEnabled(false);
    ui->analyzeSubsButton->setText("Анализ...");
    m_fontFinder->findFontsInSubs(filesToAnalyze);
}

void ManualAssemblyWidget::on_addFontsButton_clicked()
{
    QStringList paths = QFileDialog::getOpenFileNames(this, "Выберите файлы шрифтов", "",
                                                      "Файлы шрифтов (*.ttf *.otf *.ttc);;Все файлы (*)");
    for (const QString& path : paths)
    {
        auto* item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setForeground(Qt::magenta);
        item->setText(item->text() + " - добавлен вручную");
        item->setData(Qt::UserRole, path);
        ui->fontsListWidget->addItem(item);
    }
}

void ManualAssemblyWidget::on_clearFontsButton_clicked()
{
    clearFontsList();
}

void ManualAssemblyWidget::clearFontsList()
{
    ui->fontsListWidget->clear();
}

QVariantMap ManualAssemblyWidget::getParameters() const
{
    QVariantMap params;
    params["isManualMode"] = true;

    if (ui->includeVideoCheckBox->isChecked())
    {
        const QString videoPath = ui->videoPathEdit->text().trimmed();
        if (!videoPath.isEmpty())
        {
            params["videoPath"] = videoPath;
            params["videoTrackId"] = (m_selectedVideoTrackId >= 0) ? m_selectedVideoTrackId : 0;
        }
    }

    if (ui->includeOriginalAudioCheckBox->isChecked())
    {
        const QString separateAudio = ui->originalAudioPathEdit->text().trimmed();
        if (!separateAudio.isEmpty())
        {
            params["originalAudioPath"] = separateAudio;
            params["useAudioFromVideo"] = false;
        }
        else if (m_hasAudioInVideo && m_selectedVideoAudioTrackId >= 0)
        {
            params["useAudioFromVideo"] = true;
            params["audioTrackFromVideoId"] = m_selectedVideoAudioTrackId;
        }
    }
    else
    {
        params["useAudioFromVideo"] = false;
    }

    if (ui->includeRussianAudioCheckBox->isChecked())
    {
        const QString rus = ui->russianAudioPathEdit->text().trimmed();
        if (!rus.isEmpty())
        {
            params["russianAudioPath"] = rus;
        }
    }
    if (ui->includeSubtitlesCheckBox->isChecked())
    {
        const QString subs = ui->subtitlesPathEdit->text().trimmed();
        if (!subs.isEmpty())
        {
            params["subtitlesPath"] = subs;
        }
    }
    if (ui->includeSignsCheckBox->isChecked())
    {
        const QString signs = ui->signsPathEdit->text().trimmed();
        if (!signs.isEmpty())
        {
            params["signsPath"] = signs;
        }
    }

    params["normalizeAudio"] = ui->normalizeAudioCheckBox->isChecked();
    params["convertAudio"] = ui->convertAudioCheckBox->isChecked();

    const bool useCustomChapters =
        ui->manualChaptersCustomCheckBox->isVisible() && ui->manualChaptersCustomCheckBox->isChecked();
    params["useCustomChaptersXml"] = useCustomChapters;
    if (useCustomChapters)
    {
        const QString chaptersPath = ui->chaptersXmlPathManualAssemblyEdit->text().trimmed();
        if (!chaptersPath.isEmpty())
        {
            params["chaptersXmlPath"] = chaptersPath;
        }
    }

    if (params["convertAudio"].toBool())
    {
        if (ui->convertAudioFormatComboBox->currentText() == "в AAC")
        {
            params["convertAudioFormat"] = "aac";
        }
        else
        {
            params["convertAudioFormat"] = "flac";
        }
    }

    params["workDir"] = ui->workDirEdit->text().trimmed();
    QString outputName = ui->outputFileNameEdit->text().trimmed();
    if (!outputName.isEmpty() && !outputName.endsWith(".mkv", Qt::CaseInsensitive))
    {
        outputName += ".mkv";
    }
    params["outputName"] = outputName;

    QStringList fontPaths;
    for (int i = 0; i < ui->fontsListWidget->count(); ++i)
    {
        QString path = ui->fontsListWidget->item(i)->data(Qt::UserRole).toString();
        if (!path.isEmpty())
        {
            fontPaths.append(path);
        }
    }
    params["fontPaths"] = fontPaths;

    params["studio"] = ui->studioEdit->text().trimmed();
    params["language"] = ui->languageEdit->text().trimmed();
    const QString subAuthor = ui->subAuthorComboBox->currentText().trimmed();
    params["subAuthor"] = subAuthor;
    params["addTb"] = false;

    return params;
}

QString ManualAssemblyWidget::chapterTimingSourcePath() const
{
    const bool useCustomXml =
        ui->manualChaptersCustomCheckBox->isVisible() && ui->manualChaptersCustomCheckBox->isChecked();
    if (useCustomXml)
    {
        const QString chaptersXml = ui->chaptersXmlPathManualAssemblyEdit->text().trimmed();
        if (!chaptersXml.isEmpty())
        {
            return chaptersXml;
        }
    }
    return ui->videoPathEdit->text().trimmed();
}

void ManualAssemblyWidget::on_assembleButton_clicked()
{
    if (ui->includeOriginalAudioCheckBox->isChecked() && ui->originalAudioPathEdit->text().trimmed().isEmpty())
    {
        if (m_videoAudioTracks.size() > 1 && m_selectedVideoAudioTrackId < 0)
        {
            if (!promptUserForAudioTrack(m_videoAudioTracks))
            {
                // Если пользователь нажал "Не использовать звук из видео", чекбокс снялся — продолжаем сборку.
                if (ui->includeOriginalAudioCheckBox->isChecked())
                {
                    return;
                }
            }
        }
    }

    const QVariantMap params = getParameters();
    if (!params.contains("videoPath") && !params.contains("russianAudioPath"))
    {
        QMessageBox::warning(this, "Сборка MKV", "Необходимо указать как минимум видеофайл или русскую аудиодорожку.");
        return;
    }

    if (params.value("outputName").toString().isEmpty())
    {
        QMessageBox::warning(this, "Ошибка", "Укажите имя выходного файла.");
        return;
    }

    // Сохраняем последнего автора сабов
    QSettings settings("MyCompany", "DubbingTool");
    settings.setValue("ui/lastSubAuthor", ui->subAuthorComboBox->currentText().trimmed());

    bool fontsMissing = false;
    for (int i = 0; i < ui->fontsListWidget->count(); ++i)
    {
        QListWidgetItem* item = ui->fontsListWidget->item(i);
        if (item->foreground().color() == Qt::red)
        {
            fontsMissing = true;
            break;
        }
    }

    if (fontsMissing)
    {
        auto reply = QMessageBox::question(this, "Отсутствуют шрифты",
                                           "Некоторые шрифты не были найдены в системе (отмечены красным). Субтитры "
                                           "могут отображаться некорректно.\n\n"
                                           "Вы уверены, что хотите продолжить сборку?",
                                           QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No)
        {
            return;
        }
    }

    emit assemblyRequested();
}

void ManualAssemblyWidget::on_browseWorkDirButton_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Выберите рабочую папку");
    if (!dir.isEmpty())
    {
        ui->workDirEdit->setText(dir);
    }
}

void ManualAssemblyWidget::on_browseChaptersXmlManualAssemblyButton_clicked()
{
    browseForFile(ui->chaptersXmlPathManualAssemblyEdit, "Выберите XML глав",
                  "Matroska chapters (*.xml);;Все файлы (*)");
}

void ManualAssemblyWidget::onFontFinderFinished(const FontFinderResult& result)
{
    ui->analyzeSubsButton->setEnabled(true);
    ui->analyzeSubsButton->setText("Анализировать субтитры");

    ui->fontsListWidget->clear();

    for (const FoundFontInfo& fontInfo : result.foundFonts)
    {
        auto* item = new QListWidgetItem(QString("%1 - НАЙДЕН -> %2").arg(fontInfo.familyName).arg(fontInfo.path));
        item->setForeground(Qt::darkGreen);
        item->setData(Qt::UserRole, fontInfo.path);
        item->setToolTip(fontInfo.path);
        ui->fontsListWidget->addItem(item);
    }

    for (const QString& fontName : result.notFoundFontNames)
    {
        auto* item = new QListWidgetItem(QString("%1 - НЕ НАЙДЕН В СИСТЕМЕ").arg(fontName));
        item->setForeground(Qt::red);
        ui->fontsListWidget->addItem(item);
    }

    if (ui->fontsListWidget->count() == 0)
    {
        QMessageBox::warning(this, "Анализ завершен", "Не удалось найти информацию о шрифтах в указанных файлах.");
    }
}

void ManualAssemblyWidget::setAssembling(bool assembling)
{
    ui->assembleButton->setDisabled(assembling);
}
