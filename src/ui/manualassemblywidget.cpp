#include "manualassemblywidget.h"

#include "appsettings.h"
#include "fontfinder.h"
#include "mediaprobe.h"
#include "trackselectordialog.h"
#include "ui_manualassemblywidget.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QUrl>
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

    m_mediaProbe = new MediaProbe(this);
    connect(m_mediaProbe, &MediaProbe::finished, this, &ManualAssemblyWidget::onMediaProbeFinished);

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
        m_isProbingVideo = false;
        ui->selectAudioFromVideoButton->setEnabled(false);
        ui->originalAudioPathEdit->setPlaceholderText(QString());
        return;
    }

    if (videoPath == m_lastProbedVideoPath && (m_isProbingVideo || !m_videoAudioTracks.isEmpty()))
    {
        return;
    }

    m_lastProbedVideoPath = videoPath;
    m_autoPromptAudio = autoPrompt;
    m_videoAudioTracks.clear();
    m_selectedVideoTrackId = -1;
    m_selectedVideoAudioTrackId = -1;
    m_hasAudioInVideo = false;
    m_isProbingVideo = true;
    ui->originalAudioPathEdit->setPlaceholderText("Анализ дорожек видео...");

    const QString ext = QFileInfo(videoPath).suffix().toLower();
    if (ext != "mp4" && ext != "mkv" && ext != "m4v" && ext != "mov" && ext != "ts")
    {
        m_isProbingVideo = false;
        ui->selectAudioFromVideoButton->setEnabled(false);
        ui->originalAudioPathEdit->setPlaceholderText(QString());
        return;
    }

    const QString mkvmergeExe = AppSettings::instance().mkvmergePath();
    if (mkvmergeExe.isEmpty() || !QFileInfo::exists(mkvmergeExe))
    {
        m_isProbingVideo = false;
        ui->originalAudioPathEdit->setPlaceholderText(QString());
        return;
    }

    m_mediaProbe->probeFileAsync(mkvmergeExe, videoPath);
}

void ManualAssemblyWidget::onMediaProbeFinished(const QString& filePath, const MediaProbeResult& result)
{
    m_isProbingVideo = false;

    if (filePath != m_lastProbedVideoPath || !result.ok)
    {
        if (filePath == m_lastProbedVideoPath && !result.ok)
        {
            ui->originalAudioPathEdit->setPlaceholderText(QString());
            ui->selectAudioFromVideoButton->setEnabled(false);
        }
        if (m_notifyIfNoAudio)
        {
            m_notifyIfNoAudio = false;
            QMessageBox::information(this, "Аудиодорожки", "В выбранном видеофайле не обнаружено аудиодорожек.");
        }
        return;
    }

    if (!result.videoTracks.isEmpty())
    {
        m_selectedVideoTrackId = result.videoTracks.first().id;
    }
    else
    {
        m_selectedVideoTrackId = 0;
    }

    m_videoAudioTracks = result.audioTracks;

    const bool userExplicitlyClicked = m_notifyIfNoAudio;
    m_notifyIfNoAudio = false;

    if (m_videoAudioTracks.isEmpty())
    {
        m_hasAudioInVideo = false;
        m_selectedVideoAudioTrackId = -1;
        ui->selectAudioFromVideoButton->setEnabled(false);
        ui->originalAudioPathEdit->setPlaceholderText(QString());
        if (userExplicitlyClicked)
        {
            QMessageBox::information(this, "Аудиодорожки", "В выбранном видеофайле не обнаружено аудиодорожек.");
        }
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
        if (userExplicitlyClicked)
        {
            promptUserForAudioTrack(m_videoAudioTracks);
        }
    }
    else
    {
        m_hasAudioInVideo = true;
        ui->selectAudioFromVideoButton->setEnabled(true);
        if (m_autoPromptAudio || userExplicitlyClicked)
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

bool ManualAssemblyWidget::promptUserForAudioTrack(const QList<MediaTrackInfo>& tracks)
{
    if (tracks.isEmpty())
    {
        return false;
    }

    QList<AudioTrackInfo> dialogTracks;
    dialogTracks.reserve(tracks.size());
    for (const auto& t : tracks)
    {
        AudioTrackInfo info;
        info.id = t.id;
        info.codec = t.codec;
        info.name = t.trackName;
        info.language = t.language;
        info.channels = t.channels;
        dialogTracks.append(info);
    }

    TrackSelectorDialog dialog(dialogTracks, this, true);
    if (m_selectedVideoAudioTrackId >= 0)
    {
        dialog.setSelectedTrackId(m_selectedVideoAudioTrackId);
    }

    if (dialog.exec() == QDialog::Accepted)
    {
        m_selectedVideoAudioTrackId = dialog.getSelectedTrackId();
        ui->includeOriginalAudioCheckBox->setChecked(true);
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
    const QString videoPath = ui->videoPathEdit->text().trimmed();
    if (videoPath.isEmpty() || !QFileInfo::exists(videoPath))
    {
        return;
    }

    if (m_isProbingVideo)
    {
        m_notifyIfNoAudio = true;
        return;
    }

    if (m_videoAudioTracks.isEmpty())
    {
        m_lastProbedVideoPath.clear();
        m_notifyIfNoAudio = true;
        probeVideoAudioTracks(videoPath, true);
        return;
    }

    promptUserForAudioTrack(m_videoAudioTracks);
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
        ui->includeVideoCheckBox->setChecked(true);
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
    if (browseForFile(ui->originalAudioPathEdit, "Выберите оригинальную аудиодорожку",
                      "Аудиофайлы (*.aac *.ac3 *.eac3 *.flac *.wav *.opus *.m4a *.mka);;Все файлы (*)"))
    {
        ui->includeOriginalAudioCheckBox->setChecked(true);
    }
}

void ManualAssemblyWidget::on_browseRussianAudio_clicked()
{
    if (browseForFile(ui->russianAudioPathEdit, "Выберите русскую аудиодорожку",
                      "Аудиофайлы (*.wav *.aac *.flac *.m4a *.mka);;Все файлы (*)"))
    {
        ui->includeRussianAudioCheckBox->setChecked(true);
    }
}

void ManualAssemblyWidget::on_browseSubtitles_clicked()
{
    if (browseForFile(ui->subtitlesPathEdit, "Выберите файл полных субтитров", "Subtitles (*.ass *.srt)"))
    {
        ui->includeSubtitlesCheckBox->setChecked(true);
    }
}

void ManualAssemblyWidget::on_browseSigns_clicked()
{
    if (browseForFile(ui->signsPathEdit, "Выберите файл надписей", "Subtitles (*.ass *.srt)"))
    {
        ui->includeSignsCheckBox->setChecked(true);
    }
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
    QSettings settings("MyCompany", "DubbingTool");
    QString lastDir = settings.value("ui/lastBrowseDir").toString();
    QStringList paths = QFileDialog::getOpenFileNames(this, "Выберите файлы шрифтов", lastDir,
                                                      "Файлы шрифтов (*.ttf *.otf *.ttc);;Все файлы (*)");
    if (!paths.isEmpty())
    {
        settings.setValue("ui/lastBrowseDir", QFileInfo(paths.first()).absolutePath());
    }

    for (const QString& path : paths)
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
            auto* item =
                new QListWidgetItem(QFileInfo(path).fileName() + tr(" - добавлен вручную"), ui->fontsListWidget);
            item->setForeground(Qt::magenta);
            item->setData(Qt::UserRole, path);
            item->setToolTip(path);
        }
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
    if (m_isProbingVideo)
    {
        QMessageBox::information(this, "Анализ видео", "Подождите секунду, идёт анализ дорожек видеофайла.");
        return;
    }

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
