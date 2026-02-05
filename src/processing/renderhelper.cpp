#include "renderhelper.h"
#include "processmanager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>


RenderHelper::RenderHelper(RenderPreset preset, const QString &outputMp4Path, ProcessManager *procManager, QObject *parent)
    : QObject(parent),
    m_preset(preset),
    m_outputMp4Path(outputMp4Path),
    m_procManager(procManager)
{}

void RenderHelper::startCheck()
{
    if (m_preset.targetBitrateKbps <= 0) {
        emit logMessage("Проверка битрейта пропущена (не задан в пресете).");
        emit finished(RerenderDecision::Accept, m_preset);
        this->deleteLater();
        return;
    }

    emit logMessage("Проверка битрейта финального файла...");
    QByteArray jsonData;
    QString ffmpegPath = AppSettings::instance().ffmpegPath();
    QFileInfo ffmpegInfo(ffmpegPath);
    QString ffprobePath = QDir(ffmpegInfo.absolutePath()).filePath("ffprobe.exe");

    if (!QFileInfo::exists(ffprobePath)) {
        emit logMessage("Не удалось найти ffprobe.exe рядом с ffmpeg.exe по пути: " + ffprobePath);
        emit finished(RerenderDecision::Accept, m_preset);
        this->deleteLater();
        return;
    }

    QStringList ffprobeArgs = {"-v", "quiet", "-select_streams", "v:0",
                               "-show_entries", "stream=bit_rate",
                               "-print_format", "json", m_outputMp4Path};

    if (!m_procManager->executeAndWait(ffprobePath, ffprobeArgs, jsonData) || jsonData.isEmpty()) {
        emit logMessage("Не удалось получить информацию о MP4 файле для проверки битрейта (процесс ffprobe не вернул данные).");
        emit finished(RerenderDecision::Accept, m_preset);
        this->deleteLater();
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        emit logMessage("Не удалось распарсить JSON от ffprobe: " + parseError.errorString());
        emit finished(RerenderDecision::Accept, m_preset);
        this->deleteLater();
        return;
    }

    QJsonObject root = doc.object();
    if (!root.contains("streams") || !root["streams"].isArray() || root["streams"].toArray().isEmpty()) {
        emit logMessage("Не удалось найти видеопоток в выводе ffprobe.");
        emit finished(RerenderDecision::Accept, m_preset);
        this->deleteLater();
        return;
    }

    QJsonObject streamObject = root["streams"].toArray()[0].toObject();
    if (!streamObject.contains("bit_rate")) {
        emit logMessage("В информации о видеопотоке отсутствует поле 'bit_rate'.");
        emit finished(RerenderDecision::Accept, m_preset);
        this->deleteLater();
        return;
    }

    double actualBitrateBps = streamObject["bit_rate"].toString().toDouble();
    if (actualBitrateBps == 0) {
        emit logMessage("ffprobe вернул нулевой битрейт для видеопотока.");
        emit finished(RerenderDecision::Accept, m_preset);
        this->deleteLater();
        return;
    }

    double actualBitrateKbps = actualBitrateBps / 1000.0;
    double targetBitrate = m_preset.targetBitrateKbps;
    double deviation = qAbs(actualBitrateKbps - targetBitrate) / targetBitrate;

    emit logMessage(QString("Целевой битрейт: %1 kbps. Фактический: %2 kbps. Отклонение: %3%.")
                        .arg(targetBitrate).arg(qRound(actualBitrateKbps)).arg(qRound(deviation * 100)));

    if (deviation > 0.03) {
        emit logMessage("Отклонение битрейта превышает 3%. Запрос решения у пользователя.");
        emit showDialogRequest(m_preset, actualBitrateKbps);
        // Ждем ответа в onDialogFinished
    } else {
        emit logMessage("Битрейт в пределах нормы.");
        emit finished(RerenderDecision::Accept, m_preset);
        this->deleteLater();
    }
}

void RenderHelper::onDialogFinished(bool accepted, const QString &pass1, const QString &pass2)
{
    if (accepted) {
        m_preset.commandPass1 = pass1;
        m_preset.commandPass2 = pass2;
        emit finished(RerenderDecision::Rerender, m_preset);
    } else {
        emit logMessage("Пользователь принял результат как есть.");
        emit finished(RerenderDecision::Accept, m_preset);
    }
    this->deleteLater();
}
