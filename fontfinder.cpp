#include "fontfinder.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>


FontFinder::FontFinder(QObject *parent) : QObject(parent) {
    m_wrapperPath = extractWrapper();
    m_process = new QProcess(this);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &FontFinder::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &FontFinder::onProcessError);
}

FontFinder::~FontFinder() {
    if (m_process && m_process->state() == QProcess::Running) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void FontFinder::findFontsInSubs(const QStringList& subFilesToCheck) {
    if (m_wrapperPath.isEmpty()) {
        emit finished(FontFinderResult());
        return;
    }

    if (m_process->state() == QProcess::Running) {
        emit logMessage("Поиск шрифтов уже запущен. Пожалуйста, подождите.", LogCategory::APP);
        return;
    }

    QStringList arguments;
    arguments.append(subFilesToCheck);

    emit logMessage(QString("Асинхронный запуск: %1 %2").arg(m_wrapperPath, arguments.join(" ")), LogCategory::APP);

    m_process->start(m_wrapperPath, arguments);
}

void FontFinder::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        emit logMessage("Критическая ошибка: Процесс поиска шрифтов завершился с ошибкой.", LogCategory::APP);
        emit logMessage("Stderr: " + m_process->readAllStandardError(), LogCategory::APP);
        emit finished(FontFinderResult());
        return;
    }

    QByteArray jsonData = m_process->readAllStandardOutput();
    FontFinderResult result = parseJsonOutput(jsonData);
    emit finished(result); // Отправляем готовый результат через сигнал
}

void FontFinder::onProcessError(QProcess::ProcessError error) {
    Q_UNUSED(error);
    emit logMessage("Ошибка запуска процесса поиска шрифтов: " + m_process->errorString(), LogCategory::APP);
    emit finished(FontFinderResult());
}

FontFinderResult FontFinder::parseJsonOutput(const QByteArray& jsonData) {
    FontFinderResult result;
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (doc.isNull()) {
        emit logMessage("Ошибка: не удалось распарсить JSON от Python-скрипта. " + parseError.errorString(), LogCategory::APP);
        return result;
    }

    QJsonObject root = doc.object();
    if (root.contains("found_fonts") && root["found_fonts"].isArray()) {
        QJsonArray foundArray = root["found_fonts"].toArray();
        for (const QJsonValue& value : foundArray) {
            QJsonObject fontObj = value.toObject();
            FoundFontInfo fontInfo;
            fontInfo.path = fontObj["path"].toString();
            fontInfo.familyName = fontObj["family_name"].toString();
            result.foundFonts.append(fontInfo);
            emit logMessage(QString(" -> НАЙДЕН: '%1' (Файл: %2)").arg(fontInfo.familyName, fontInfo.path), LogCategory::APP);
        }
    }

    if (root.contains("not_found_font_names") && root["not_found_font_names"].isArray()) {
        QJsonArray notFoundArray = root["not_found_font_names"].toArray();
        for (const QJsonValue& value : notFoundArray) {
            result.notFoundFontNames.append(value.toString());
        }
    }
    return result;
}

QString FontFinder::extractWrapper()
{
    QDir tempDir = QDir::temp();
    QString wrapperPathInTemp = tempDir.filePath("DubbingTool_FontFinder.exe");
    if (QFile::exists(wrapperPathInTemp)) {
        return wrapperPathInTemp;
    }

    QFile resourceFile(":/font_finder_wrapper.exe");
    if (!resourceFile.open(QIODevice::ReadOnly)) {
        emit logMessage("Критическая ошибка: не удалось открыть обертку из ресурсов (:/font_finder_wrapper.exe). Убедитесь, что она добавлена в .qrc файл.", LogCategory::APP);
        return QString();
    }

    if (!resourceFile.copy(wrapperPathInTemp)) {
        emit logMessage(QString("Критическая ошибка: не удалось извлечь обертку во временную папку: %1").arg(wrapperPathInTemp), LogCategory::APP);
        return QString();
    }

    QFile(wrapperPathInTemp).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);

    emit logMessage("Обертка для поиска шрифтов успешно извлечена.", LogCategory::APP);
    return wrapperPathInTemp;
}
