#include "manualrenderwidget.h"

#include "appsettings.h"
#include "mediaprobe.h"
#include "ui_manualrenderwidget.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>

ManualRenderWidget::ManualRenderWidget(QWidget* parent) : QWidget(parent), ui(new Ui::ManualRenderWidget)
{
    ui->setupUi(this);

    setAcceptDrops(true);
    const auto lineEdits = findChildren<QLineEdit*>();
    for (auto* le : lineEdits)
    {
        le->setAcceptDrops(false);
    }

    m_mediaProbe = new MediaProbe(this);
    connect(m_mediaProbe, &MediaProbe::finished, this,
            [this](const QString& filePath, const MediaProbeResult& result)
            {
                if (filePath != ui->inputMkvPathEdit->text().trimmed())
                {
                    return;
                }
                ui->subtitleTrackComboBox->clear();
                if (result.ok)
                {
                    for (const MediaTrackInfo& track : result.subtitleTracks)
                    {
                        QString title = QString("Дорожка #%1 (ID:%2) [%3] %4")
                                            .arg(track.typeIndex) // Порядковый номер для ffmpeg
                                            .arg(track.id)        // ID для информации
                                            .arg(track.language)
                                            .arg(track.trackName);

                        ui->subtitleTrackComboBox->addItem(title.trimmed(), track.typeIndex);
                    }
                    if (result.subtitleTracks.isEmpty())
                    {
                        ui->subtitleTrackComboBox->addItem("Дорожки не найдены");
                        ui->internalSubsRadio->setEnabled(false);
                        ui->externalSubsRadio->setChecked(true);
                    }
                    else
                    {
                        ui->internalSubsRadio->setEnabled(true);
                    }
                }
                else
                {
                    ui->subtitleTrackComboBox->addItem("Ошибка анализа файла");
                }
                updateHardsubOptions();
            });

    updateRenderPresets();

    QSettings settings("MyCompany", "DubbingTool");
    QString lastPreset = settings.value("manualRender/lastUsedPreset", "").toString();
    if (!lastPreset.isEmpty())
    {
        ui->renderPresetComboBox->setCurrentText(lastPreset);
    }
    const bool reencodeAudio = settings
                                   .value(QStringLiteral("manualRender/reencodeAudioAac"),
                                          settings.value(QStringLiteral("manualRender/reencodeAudioAac256"), true))
                                   .toBool();
    ui->reencodeAudioAac256CheckBox->setChecked(reencodeAudio);
    connect(ui->hardsubCheckBox, &QCheckBox::toggled, this, &ManualRenderWidget::updateHardsubOptions);
    connect(ui->internalSubsRadio, &QRadioButton::toggled, this, &ManualRenderWidget::updateHardsubOptions);
    connect(ui->externalSubsRadio, &QRadioButton::toggled, this, &ManualRenderWidget::updateHardsubOptions);
    connect(ui->browseExternalSubsButton, &QPushButton::clicked, this,
            [this]()
            {
                QSettings settings("MyCompany", "DubbingTool");
                QString lastDir = settings.value("ui/lastBrowseDir").toString();
                if (lastDir.isEmpty() && !ui->externalSubsPathEdit->text().isEmpty())
                {
                    lastDir = QFileInfo(ui->externalSubsPathEdit->text()).absolutePath();
                }
                QString path =
                    QFileDialog::getOpenFileName(this, "Выберите файл субтитров", lastDir, "Subtitles (*.ass *.srt)");
                if (!path.isEmpty())
                {
                    settings.setValue("ui/lastBrowseDir", QFileInfo(path).absolutePath());
                    ui->externalSubsPathEdit->setText(path);
                }
            });

    connect(ui->browseChaptersManualButton, &QPushButton::clicked, this,
            [this]()
            {
                QSettings settings("MyCompany", "DubbingTool");
                QString lastDir = settings.value("ui/lastBrowseDir").toString();
                if (lastDir.isEmpty() && !ui->chaptersExternalPathManualEdit->text().isEmpty())
                {
                    lastDir = QFileInfo(ui->chaptersExternalPathManualEdit->text()).absolutePath();
                }
                QString path =
                    QFileDialog::getOpenFileName(this, "Файл глав Matroska XML", lastDir, "XML (*.xml);;Все файлы (*)");
                if (!path.isEmpty())
                {
                    settings.setValue("ui/lastBrowseDir", QFileInfo(path).absolutePath());
                    ui->chaptersExternalPathManualEdit->setText(path);
                }
            });
    connect(ui->chapterTimingsButton, &QPushButton::clicked, this,
            [this]() { emit chapterTimingsRequested(chapterTimingSourcePath()); });

    updateHardsubOptions();
}

ManualRenderWidget::~ManualRenderWidget()
{
    delete ui;
}

void ManualRenderWidget::updateRenderPresets()
{
    QString currentPreset = ui->renderPresetComboBox->currentText();

    ui->renderPresetComboBox->clear();
    for (const auto& preset : AppSettings::instance().renderPresets())
    {
        ui->renderPresetComboBox->addItem(preset.name);
    }

    int index = ui->renderPresetComboBox->findText(currentPreset);
    if (index != -1)
    {
        ui->renderPresetComboBox->setCurrentIndex(index);
    }
}

void ManualRenderWidget::setInputVideoPath(const QString& path)
{
    if (path.isEmpty())
    {
        return;
    }
    ui->inputMkvPathEdit->setText(path);
    analyzeMkvForSubtitles(path);

    const QFileInfo fi(path);
    const QString suffix = fi.suffix().toLower();
    const QString outFileName = (suffix == QLatin1String("mp4"))
                                    ? fi.completeBaseName() + QStringLiteral("_rendered.mp4")
                                    : fi.completeBaseName() + QStringLiteral(".mp4");
    ui->outputMp4PathEdit->setText(QDir::toNativeSeparators(fi.absolutePath() + QLatin1Char('/') + outFileName));
}

void ManualRenderWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        static const QSet<QString> supportedExts = {QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("ass"),
                                                    QStringLiteral("srt"), QStringLiteral("xml")};
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

void ManualRenderWidget::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls)
    {
        const QString path = url.toLocalFile();
        if (path.isEmpty() || !QFileInfo::exists(path))
        {
            continue;
        }

        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == QLatin1String("mkv") || ext == QLatin1String("mp4"))
        {
            setInputVideoPath(path);
        }
        else if (ext == QLatin1String("ass") || ext == QLatin1String("srt"))
        {
            ui->externalSubsPathEdit->setText(path);
            ui->hardsubCheckBox->setChecked(true);
            ui->externalSubsRadio->setChecked(true);
            updateHardsubOptions();
        }
        else if (ext == QLatin1String("xml"))
        {
            ui->chaptersExternalPathManualEdit->setText(path);
        }
    }
    event->acceptProposedAction();
}

void ManualRenderWidget::on_browseInputButton_clicked()
{
    QSettings settings("MyCompany", "DubbingTool");
    QString lastDir = settings.value("ui/lastBrowseDir").toString();
    if (lastDir.isEmpty() && !ui->inputMkvPathEdit->text().isEmpty())
    {
        lastDir = QFileInfo(ui->inputMkvPathEdit->text()).absolutePath();
    }

    QString path = QFileDialog::getOpenFileName(this, "Выберите входное видео", lastDir,
                                                "Видеофайлы (*.mkv *.mp4);;Все файлы (*)");
    if (!path.isEmpty())
    {
        settings.setValue("ui/lastBrowseDir", QFileInfo(path).absolutePath());
        setInputVideoPath(path);
    }
}

void ManualRenderWidget::on_browseOutputButton_clicked()
{
    QSettings settings("MyCompany", "DubbingTool");
    QString lastDir = settings.value("ui/lastBrowseDir").toString();
    if (lastDir.isEmpty() && !ui->outputMp4PathEdit->text().isEmpty())
    {
        lastDir = QFileInfo(ui->outputMp4PathEdit->text()).absolutePath();
    }
    QString path = QFileDialog::getSaveFileName(this, "Выберите, куда сохранить MP4", lastDir, "MPEG-4 Video (*.mp4)");
    if (!path.isEmpty())
    {
        settings.setValue("ui/lastBrowseDir", QFileInfo(path).absolutePath());
        ui->outputMp4PathEdit->setText(path);
    }
}

void ManualRenderWidget::on_renderButton_clicked()
{
    if (ui->inputMkvPathEdit->text().isEmpty() || ui->outputMp4PathEdit->text().isEmpty())
    {
        QMessageBox::warning(this, "Ошибка", "Необходимо указать входной и выходной файлы.");
        return;
    }
    if (ui->hardsubCheckBox->isChecked() && ui->externalSubsRadio->isChecked() &&
        ui->externalSubsPathEdit->text().isEmpty())
    {
        QMessageBox::warning(this, "Ошибка", "Выбран режим с внешним файлом субтитров, но путь не указан.");
        return;
    }

    QSettings settings("MyCompany", "DubbingTool");
    settings.setValue("manualRender/lastUsedPreset", ui->renderPresetComboBox->currentText());
    settings.setValue(QStringLiteral("manualRender/reencodeAudioAac"), ui->reencodeAudioAac256CheckBox->isChecked());

    emit renderRequested();
}

void ManualRenderWidget::setRendering(bool rendering)
{
    ui->renderButton->setDisabled(rendering);
}

QString ManualRenderWidget::getCurrentPresetName() const
{
    return ui->renderPresetComboBox->currentText();
}

QString ManualRenderWidget::chapterTimingSourcePath() const
{
    const QString externalPath = ui->chaptersExternalPathManualEdit->text().trimmed();
    if (!externalPath.isEmpty())
    {
        return externalPath;
    }
    if (ui->transferEmbeddedChaptersCheckBox->isChecked())
    {
        return ui->inputMkvPathEdit->text().trimmed();
    }
    return QString();
}

void ManualRenderWidget::analyzeMkvForSubtitles(const QString& path)
{
    ui->subtitleTrackComboBox->clear();
    ui->subtitleTrackComboBox->addItem("Анализ файла...");
    ui->subtitleTrackComboBox->setEnabled(false);
    ui->internalSubsRadio->setEnabled(false);

    const QString mkvmergePath = AppSettings::instance().mkvmergePath();
    m_mediaProbe->probeFileAsync(mkvmergePath, path);
}

void ManualRenderWidget::updateHardsubOptions()
{
    bool hardsubEnabled = ui->hardsubCheckBox->isChecked();
    ui->hardsubOptionsGroup->setEnabled(hardsubEnabled);

    if (hardsubEnabled)
    {
        ui->subtitleTrackComboBox->setEnabled(ui->internalSubsRadio->isChecked());
        ui->externalSubsPathEdit->setEnabled(ui->externalSubsRadio->isChecked());
        ui->browseExternalSubsButton->setEnabled(ui->externalSubsRadio->isChecked());
    }
}

QVariantMap ManualRenderWidget::getParameters() const
{
    QVariantMap params;
    params["inputMkv"] = ui->inputMkvPathEdit->text();
    params["outputMp4"] = ui->outputMp4PathEdit->text();
    params["renderPresetName"] = ui->renderPresetComboBox->currentText();

    bool useHardsub = ui->hardsubCheckBox->isChecked();
    params["useHardsub"] = useHardsub;
    params["chaptersExternalPath"] = ui->chaptersExternalPathManualEdit->text().trimmed();
    params["transferEmbeddedChapters"] = ui->transferEmbeddedChaptersCheckBox->isChecked();
    params[QStringLiteral("reencodeAudioAac")] = ui->reencodeAudioAac256CheckBox->isChecked();

    if (useHardsub)
    {
        if (ui->internalSubsRadio->isChecked())
        {
            params["hardsubMode"] = "internal";
            params["subtitleTrackIndex"] = ui->subtitleTrackComboBox->currentData().toInt();
        }
        else
        {
            params["hardsubMode"] = "external";
            params["externalSubsPath"] = ui->externalSubsPathEdit->text();
        }
    }

    return params;
}
