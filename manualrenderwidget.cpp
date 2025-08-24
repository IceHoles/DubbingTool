#include "appsettings.h"
#include "manualrenderwidget.h"
#include "ui_manualrenderwidget.h"
#include <QVariantMap>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>


ManualRenderWidget::ManualRenderWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ManualRenderWidget)
{
    ui->setupUi(this);
    updateRenderPresets();

    QSettings settings("MyCompany", "DubbingTool");
    QString lastPreset = settings.value("manualRender/lastUsedPreset", "").toString();
    if (!lastPreset.isEmpty()) {
        ui->renderPresetComboBox->setCurrentText(lastPreset);
    }
    connect(ui->hardsubCheckBox, &QCheckBox::toggled, this, &ManualRenderWidget::updateHardsubOptions);
    connect(ui->internalSubsRadio, &QRadioButton::toggled, this, &ManualRenderWidget::updateHardsubOptions);
    connect(ui->externalSubsRadio, &QRadioButton::toggled, this, &ManualRenderWidget::updateHardsubOptions);
    connect(ui->browseExternalSubsButton, &QPushButton::clicked, this, [this](){
        QString path = QFileDialog::getOpenFileName(this, "Выберите файл субтитров", "", "ASS Subtitles (*.ass)");
        if (!path.isEmpty()) {
            ui->externalSubsPathEdit->setText(path);
        }
    });

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
    for (const auto& preset : AppSettings::instance().renderPresets()) {
        ui->renderPresetComboBox->addItem(preset.name);
    }

    int index = ui->renderPresetComboBox->findText(currentPreset);
    if (index != -1) {
        ui->renderPresetComboBox->setCurrentIndex(index);
    }
}


void ManualRenderWidget::on_browseInputButton_clicked()
{
    QString path = QFileDialog::getOpenFileName(this, "Выберите входной MKV", "", "Matroska Video (*.mkv)");
    if (!path.isEmpty()) {
        ui->inputMkvPathEdit->setText(path);
        analyzeMkvForSubtitles(path);
        ui->outputMp4PathEdit->setText(path.replace(".mkv", ".mp4"));
    }
}

void ManualRenderWidget::on_browseOutputButton_clicked()
{
    QString path = QFileDialog::getSaveFileName(this, "Выберите, куда сохранить MP4", "", "MPEG-4 Video (*.mp4)");
    if (!path.isEmpty()) {
        ui->outputMp4PathEdit->setText(path);
    }
}

void ManualRenderWidget::on_renderButton_clicked()
{
    if (ui->inputMkvPathEdit->text().isEmpty() || ui->outputMp4PathEdit->text().isEmpty()) {
        QMessageBox::warning(this, "Ошибка", "Необходимо указать входной и выходной файлы.");
        return;
    }
    if (ui->hardsubCheckBox->isChecked() && ui->externalSubsRadio->isChecked() && ui->externalSubsPathEdit->text().isEmpty()){
        QMessageBox::warning(this, "Ошибка", "Выбран режим с внешним файлом субтитров, но путь не указан.");
        return;
    }

    QSettings settings("MyCompany", "DubbingTool");
    settings.setValue("manualRender/lastUsedPreset", ui->renderPresetComboBox->currentText());

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

void ManualRenderWidget::analyzeMkvForSubtitles(const QString& path)
{
    ui->subtitleTrackComboBox->clear();
    ui->subtitleTrackComboBox->addItem("Анализ файла...");
    ui->subtitleTrackComboBox->setEnabled(false);
    ui->internalSubsRadio->setEnabled(false);

    QProcess *process = new QProcess(this);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process](int exitCode, QProcess::ExitStatus exitStatus){
                if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
                    QJsonDocument doc = QJsonDocument::fromJson(process->readAllStandardOutput());
                    QJsonArray tracks = doc.object()["tracks"].toArray();
                    ui->subtitleTrackComboBox->clear();
                    int foundCount = 0;
                    int subtitleCounter = 0;
                    for (const QJsonValue& val : tracks) {
                        QJsonObject track = val.toObject();
                        if (track["type"].toString() == "subtitles") {
                            int id = track["id"].toInt();
                            QString lang = track["properties"].toObject()["language"].toString("und");
                            QString name = track["properties"].toObject()["track_name"].toString();

                            QString title = QString("Дорожка #%1 (ID:%2) [%3] %4")
                                                .arg(subtitleCounter) // Порядковый номер для ffmpeg
                                                .arg(id)              // ID для информации
                                                .arg(lang)
                                                .arg(name);

                            ui->subtitleTrackComboBox->addItem(title.trimmed(), subtitleCounter);
                            subtitleCounter++;
                            foundCount++;
                        }
                    }
                    if(foundCount == 0) {
                        ui->subtitleTrackComboBox->addItem("Дорожки не найдены");
                        ui->internalSubsRadio->setEnabled(false);
                        ui->externalSubsRadio->setChecked(true);
                    } else {
                        ui->internalSubsRadio->setEnabled(true);
                    }
                } else {
                    ui->subtitleTrackComboBox->clear();
                    ui->subtitleTrackComboBox->addItem("Ошибка анализа файла");
                }
                process->deleteLater();
                updateHardsubOptions();
            });


    QString mkvmergePath = AppSettings::instance().mkvmergePath();
    process->start(mkvmergePath, {"-J", path});
}

void ManualRenderWidget::updateHardsubOptions()
{
    bool hardsubEnabled = ui->hardsubCheckBox->isChecked();
    ui->hardsubOptionsGroup->setEnabled(hardsubEnabled);

    if (hardsubEnabled) {
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

    if (useHardsub) {
        if (ui->internalSubsRadio->isChecked()) {
            params["hardsubMode"] = "internal";
            params["subtitleTrackIndex"] = ui->subtitleTrackComboBox->currentData().toInt();
        } else {
            params["hardsubMode"] = "external";
            params["externalSubsPath"] = ui->externalSubsPathEdit->text();
        }
    }

    return params;
}
