#include "manualrenderer.h"
#include "processmanager.h"
#include "appsettings.h"
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QProcess>


ManualRenderer::ManualRenderer(const QVariantMap &params, QObject *parent)
    : QObject(parent), m_params(params)
{
    m_processManager = new ProcessManager(this);
    connect(m_processManager, &ProcessManager::processOutput, this, &ManualRenderer::onProcessText);
    connect(m_processManager, &ProcessManager::processStdErr, this, &ManualRenderer::onProcessText);
    connect(m_processManager, &ProcessManager::processFinished, this, &ManualRenderer::onProcessFinished);
}

ManualRenderer::~ManualRenderer() {}

void ManualRenderer::start()
{
    emit logMessage("--- Начало ручного рендера ---", LogCategory::APP);
    emit progressUpdated(-1, "Рендер MP4");

    QString inputMkv = m_params["inputMkv"].toString();
    QString presetName = m_params["renderPresetName"].toString();

    if (inputMkv.isEmpty() || presetName.isEmpty()) {
        emit logMessage("Ошибка: не указан входной файл или не выбран пресет рендера.", LogCategory::APP);
        emit finished();
        return;
    }

    // Получаем актуальный пресет из настроек
    m_preset = AppSettings::instance().findRenderPreset(presetName);
    if (m_preset.name.isEmpty()) {
        emit logMessage("Критическая ошибка: пресет рендера '" + presetName + "' не найден в настройках.", LogCategory::APP);
        emit finished();
        return;
    }

    QByteArray jsonData;
    if (m_processManager->executeAndWait(AppSettings::instance().mkvmergePath(), {"-J", inputMkv}, jsonData)) {
        QJsonObject root = QJsonDocument::fromJson(jsonData).object();
        if (root.contains("container")) {
            m_sourceDurationS = root["container"].toObject()["properties"].toObject()["duration"].toDouble() / 1000000000.0;
        }
    }
    if (m_sourceDurationS == 0) {
        emit logMessage("Предупреждение: не удалось определить длительность файла. Прогресс не будет отображаться.", LogCategory::APP);
    }

    m_currentStep = Step::Pass1;
    runPass(m_currentStep);
}

void ManualRenderer::runPass(Step pass)
{
    QString commandTemplate = (pass == Step::Pass1) ? m_preset.commandPass1 : m_preset.commandPass2;

    QStringList args = prepareCommandArguments(commandTemplate);
    if (args.isEmpty()) {
        emit logMessage("Ошибка: не удалось подготовить команду для рендера.", LogCategory::APP);
        emit finished();
        return;
    }

    QString program = args.takeFirst();

    emit logMessage(QString("Запуск прохода: ") + program + " " + args.join(" "), LogCategory::APP);
    m_processManager->startProcess(program, args);
}

QStringList ManualRenderer::prepareCommandArguments(const QString& commandTemplate)
{
    QString processedTemplate = commandTemplate;

    // 1. Подставляем пути
    QString inputMkv = m_params.value("inputMkv").toString();
    QString outputMp4 = m_params.value("outputMp4").toString();

    processedTemplate.replace("%INPUT%", inputMkv);
    processedTemplate.replace("%OUTPUT%", outputMp4);

    // 2. Обрабатываем фильтр субтитров
    bool useHardsub = m_params.value("useHardsub").toBool();

    if (useHardsub) {
        QString hardsubMode = m_params.value("hardsubMode").toString();
        QString filterValue;

        if (hardsubMode == "internal") {
            int trackIndex = m_params.value("subtitleTrackIndex").toInt();
            // Формируем specifier 's:<index>', например 's:0' для первой дорожки субтитров
            filterValue = QString("'%1':si=%2")
                              .arg(escapePathForFfmpegFilter(inputMkv))
                              .arg(trackIndex);
            emit logMessage(QString("Hardsub: используется внутренняя дорожка субтитров #%1.").arg(trackIndex), LogCategory::APP);

        } else { // "external"
            QString externalPath = m_params.value("externalSubsPath").toString();
            filterValue = QString("'%1'")
                              .arg(escapePathForFfmpegFilter(externalPath));
            emit logMessage(QString("Hardsub: используется внешний файл: %1").arg(externalPath), LogCategory::APP);
        }

        // Подставляем готовый фильтр в плейсхолдер
        processedTemplate.replace("%SIGNS%", filterValue);

    } else {
        // Если hardsub отключен, нужно аккуратно удалить сам плейсхолдер и опцию -vf, если она относится только к нему.
        // Простой вариант - заменить плейсхолдер на пустую строку, но это может оставить -vf "" в команде.
        // Надежный вариант:
        QRegularExpression filterRegex(R"(-vf\s+\"[^\"]*%SIGNS%[^\"]*\")");
        processedTemplate.remove(filterRegex);
        emit logMessage("Hardsub отключен. Фильтр субтитров удален из команды.", LogCategory::APP);
    }

    // 3. Используем QProcess для безопасного разбора готовой строки в список аргументов
    return QProcess::splitCommand(processedTemplate);
}

void ManualRenderer::cancelOperation()
{
    emit logMessage("Получена команда на отмену ручного рендера...", LogCategory::APP);
    if (m_processManager) {
        m_processManager->killProcess();
    }
}


void ManualRenderer::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_processManager && m_processManager->wasKilled()) {
        emit logMessage("Ручной рендер отменен пользователем.", LogCategory::APP);
        emit finished();
        return;
    }

    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        emit logMessage("Ошибка выполнения дочернего процесса. Рендер остановлен.", LogCategory::APP);
        emit finished();
        return;
    }

    if (m_currentStep == Step::Pass1) {
        if (m_preset.isTwoPass()) {
            emit logMessage("Первый проход успешно завершен.", LogCategory::APP);
            m_currentStep = Step::Pass2;
            runPass(m_currentStep);
        } else {
            emit logMessage("Рендер успешно завершен (один проход).", LogCategory::APP);
            RenderHelper* helper = new RenderHelper(m_preset, m_params["outputMp4"].toString(), m_processManager, this);
            connect(helper, &RenderHelper::logMessage, this, &ManualRenderer::logMessage);
            connect(helper, &RenderHelper::finished, this, &ManualRenderer::onBitrateCheckFinished);
            connect(helper, &RenderHelper::showDialogRequest, this, &ManualRenderer::bitrateCheckRequest);
            helper->startCheck();
        }
    } else { // Step::Pass2
        emit logMessage("Второй проход и весь рендер успешно завершены.", LogCategory::APP);
        RenderHelper* helper = new RenderHelper(m_preset, m_params["outputMp4"].toString(), m_processManager, this);
        connect(helper, &RenderHelper::logMessage, this, &ManualRenderer::logMessage);
        connect(helper, &RenderHelper::finished, this, &ManualRenderer::onBitrateCheckFinished);
        connect(helper, &RenderHelper::showDialogRequest, this, &ManualRenderer::bitrateCheckRequest);
        helper->startCheck();
    }
}

void ManualRenderer::onProcessText(const QString &output)
{
    if (!output.trimmed().isEmpty()) {
        emit logMessage(output.trimmed(), LogCategory::FFMPEG);
    }

    QRegularExpression re("time=(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{2})");
    QRegularExpressionMatch match = re.match(output);
    if (match.hasMatch()) {
        int hours = match.captured(1).toInt();
        int minutes = match.captured(2).toInt();
        int seconds = match.captured(3).toInt();
        double currentTimeS = (hours * 3600) + (minutes * 60) + seconds;
        if (m_sourceDurationS > 0) {
            int basePercentage = 0;
            if (m_preset.isTwoPass()) {
                basePercentage = (m_currentStep == Step::Pass1) ? 0 : 50;
            }
            int percentage = basePercentage + static_cast<int>((currentTimeS / m_sourceDurationS) * (m_preset.isTwoPass() ? 50 : 100));
            emit progressUpdated(qMin(100, percentage), "Рендер MP4");
        }
    }
}

ProcessManager* ManualRenderer::getProcessManager() const {
    return m_processManager;
}

void ManualRenderer::onBitrateCheckFinished(RerenderDecision decision, const RenderPreset &newPreset)
{
    if (decision == RerenderDecision::Rerender) {
        emit logMessage("Получено решение о перерендере.", LogCategory::APP);
        m_preset = newPreset;
        m_currentStep = Step::Pass1;
        runPass(m_currentStep);
    } else {
        emit progressUpdated(100, "Рендер MP4");
        emit finished();
    }
}
