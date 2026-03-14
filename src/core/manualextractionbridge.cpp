#include "manualextractionbridge.h"

#include "appsettings.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QVariantList>
#include <QVariantMap>

ManualExtractionBridge::ManualExtractionBridge(QObject* parent) : QObject(parent)
{
    connect(&m_processManager, &ProcessManager::processFinished, this,
            &ManualExtractionBridge::onProcessFinished);
    connect(&m_processManager, &ProcessManager::processOutput, this,
            &ManualExtractionBridge::onProcessStdOut);
    connect(&m_processManager, &ProcessManager::processStdErr, this,
            &ManualExtractionBridge::onProcessStdErr);
}

QString ManualExtractionBridge::identifyFile(const QString& path)
{
    m_currentFile = path;

    emit logMessage(QStringLiteral("Сканирование структуры файла..."));

    const QString mkvmergeExe = AppSettings::instance().mkvmergePath();
    if (mkvmergeExe.isEmpty() || !QFileInfo::exists(mkvmergeExe))
    {
        emit logMessage(QStringLiteral("Ошибка: mkvmerge не найден в настройках!"));
        return {};
    }

    QByteArray output;
    const QStringList args = {QStringLiteral("--identify"), QStringLiteral("--identification-format"),
                              QStringLiteral("json"), m_currentFile};

    if (m_processManager.executeAndWait(mkvmergeExe, args, output))
    {
        emit logMessage(QStringLiteral("Сканирование завершено. Выберите дорожки для извлечения."));
        return QString::fromUtf8(output);
    }

    emit logMessage(QStringLiteral("Ошибка: не удалось просканировать файл"));
    return {};
}

void ManualExtractionBridge::setDurationSeconds(double seconds)
{
    m_durationSec = seconds;
}

void ManualExtractionBridge::buildMkvExtractArgs(const QString& filePath, const QVariantList& selectedItems,
                                                 QStringList& trackArgs, QStringList& attachmentArgs, bool& hasWork)
{
    QFileInfo fileInfo(filePath);
    const QString sourceDir = fileInfo.absolutePath();
    const QString baseName = fileInfo.completeBaseName();

    for (const QVariant& v : selectedItems)
    {
        const QVariantMap map = v.toMap();
        const QString mode = map.value(QStringLiteral("mode")).toString();
        const int id = map.value(QStringLiteral("id")).toInt();

        if (mode == QLatin1String("track"))
        {
            const QString ext = map.value(QStringLiteral("ext")).toString();
            const QString lang = map.value(QStringLiteral("lang")).toString();

            const QString outName =
                QStringLiteral("%1_track%2_%3.%4").arg(baseName).arg(id).arg(lang).arg(ext);
            const QString outPath = QDir(sourceDir).filePath(outName);

            trackArgs << QStringLiteral("%1:%2").arg(id).arg(outPath);
            hasWork = true;
        }
        else if (mode == QLatin1String("attachment"))
        {
            const QString fileName = map.value(QStringLiteral("fileName")).toString();
            const QString fontsDir = QDir(sourceDir).filePath(QStringLiteral("attached_fonts"));
            QDir().mkpath(fontsDir);
            const QString outPath = QDir(fontsDir).filePath(fileName);

            attachmentArgs << QStringLiteral("%1:%2").arg(id).arg(outPath);
            hasWork = true;
        }
    }
}

void ManualExtractionBridge::startExtraction(const QString& filePath, const QVariantList& selectedItems)
{
    if (filePath.isEmpty())
        return;

    m_currentFile = filePath;

    QFileInfo fileInfo(m_currentFile);
    const QString suffix = fileInfo.suffix().toLower();
    const bool isMkv = (suffix == QStringLiteral("mkv") || suffix == QStringLiteral("mks")
                        || suffix == QStringLiteral("mka"));

    QStringList mkvextractTracks;
    QStringList mkvextractAttach;
    QStringList ffmpegArgs;

    bool hasWork = false;

    if (!isMkv)
    {
        ffmpegArgs << QStringLiteral("-y") << QStringLiteral("-i") << m_currentFile;
    }

    if (isMkv)
    {
        buildMkvExtractArgs(m_currentFile, selectedItems, mkvextractTracks, mkvextractAttach, hasWork);
    }
    else
    {
        QFileInfo fi(m_currentFile);
        const QString sourceDir = fi.absolutePath();
        const QString baseName = fi.completeBaseName();

        for (const QVariant& v : selectedItems)
        {
            const QVariantMap map = v.toMap();
            const QString mode = map.value(QStringLiteral("mode")).toString();
            const int id = map.value(QStringLiteral("id")).toInt();

            if (mode == QLatin1String("track"))
            {
                const QString ext = map.value(QStringLiteral("ext")).toString();
                const QString lang = map.value(QStringLiteral("lang")).toString();

                const QString outName =
                    QStringLiteral("%1_track%2_%3.%4").arg(baseName).arg(id).arg(lang).arg(ext);
                const QString outPath = QDir(sourceDir).filePath(outName);

                ffmpegArgs << QStringLiteral("-map") << QStringLiteral("0:%1").arg(id) << QStringLiteral("-c")
                           << QStringLiteral("copy") << outPath;
                hasWork = true;
            }
        }
    }

    if (!hasWork)
    {
        emit logMessage(QStringLiteral("Ничего не выбрано."));
        return;
    }

    emit progressUpdated(0, QStringLiteral("Начало извлечения..."));
    emit logMessage(QStringLiteral("Запуск извлечения..."));

    if (isMkv)
    {
        const QString mkvextractExe = AppSettings::instance().mkvextractPath();

        if (mkvextractExe.isEmpty() || !QFileInfo::exists(mkvextractExe))
        {
            emit logMessage(QStringLiteral("Ошибка: mkvextract не найден в настройках!"));
            return;
        }

        if (!mkvextractTracks.isEmpty())
        {
            QStringList args;
            args << QStringLiteral("tracks") << m_currentFile << mkvextractTracks;

            if (!mkvextractAttach.isEmpty())
            {
                QByteArray dummy;
                m_processManager.executeAndWait(mkvextractExe, args, dummy);
            }
            else
            {
                m_processManager.startProcess(mkvextractExe, args);
                return;
            }
        }

        if (!mkvextractAttach.isEmpty())
        {
            QStringList args;
            args << QStringLiteral("attachments") << m_currentFile << mkvextractAttach;
            m_processManager.startProcess(mkvextractExe, args);
        }
    }
    else
    {
        const QString ffmpegExe = AppSettings::instance().ffmpegPath();

        if (ffmpegExe.isEmpty() || !QFileInfo::exists(ffmpegExe))
        {
            emit logMessage(QStringLiteral("Ошибка: ffmpeg не найден в настройках!"));
            return;
        }

        m_processManager.startProcess(ffmpegExe, ffmpegArgs);
    }
}

void ManualExtractionBridge::onProcessFinished(int exitCode)
{
    if (exitCode == 0)
    {
        emit logMessage(QStringLiteral("Извлечение завершено успешно."));
    }
    else
    {
        emit logMessage(QStringLiteral("Ошибка извлечения (код %1).").arg(exitCode));
    }

    emit progressUpdated(100, QStringLiteral("Готово"));
    emit extractionFinished(exitCode);
}

void ManualExtractionBridge::onProcessStdOut(const QString& output)
{
    if (output.contains(QStringLiteral("Progress:")))
    {
        static const QRegularExpression re(QStringLiteral("Progress: (\\d+)%"));
        const QRegularExpressionMatch match = re.match(output);
        if (match.hasMatch())
        {
            const int percent = match.captured(1).toInt();
            emit progressUpdated(percent, QStringLiteral("Извлечение..."));
        }
    }
    else if (!output.trimmed().isEmpty())
    {
        emit logMessage(output.trimmed());
    }
}

void ManualExtractionBridge::onProcessStdErr(const QString& output)
{
    if (output.contains(QStringLiteral("time=")))
    {
        static const QRegularExpression re(
            QStringLiteral("time=(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{2})"));
        const QRegularExpressionMatch match = re.match(output);
        if (match.hasMatch() && m_durationSec > 0.0)
        {
            const int h = match.captured(1).toInt();
            const int m = match.captured(2).toInt();
            const int s = match.captured(3).toInt();
            const double currentSec = h * 3600 + m * 60 + s;

            const int percent = static_cast<int>((currentSec / m_durationSec) * 100.0);
            emit progressUpdated(percent, QStringLiteral("Извлечение..."));
        }
    }

    if (!output.trimmed().isEmpty())
    {
        emit logMessage(output.trimmed());
    }
}

