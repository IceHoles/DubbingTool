// fontfinder.cpp
#include "fontfinder.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>

FontFinder::FontFinder(QObject *parent) : QObject(parent) {
    m_wrapperPath = extractWrapper();
    m_process = new QProcess(this);

    // Соединяем сигналы от процесса с нашими слотами
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &FontFinder::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &FontFinder::onProcessError);
}

FontFinder::~FontFinder() {
    // При уничтожении объекта убеждаемся, что процесс завершен
    if (m_process && m_process->state() == QProcess::Running) {
        m_process->kill();
        m_process->waitForFinished(1000); // Даем ему секунду, чтобы умереть
    }
}

// Метод теперь только запускает процесс и не ждет его
void FontFinder::findFontsInSubs(const QStringList& subFilesToCheck) {
    if (m_wrapperPath.isEmpty()) {
        emit finished(FontFinderResult()); // Отправляем пустой результат, если обертка не найдена
        return;
    }

    if (m_process->state() == QProcess::Running) {
        emit logMessage("Поиск шрифтов уже запущен. Пожалуйста, подождите.");
        return;
    }

    QStringList arguments;
    arguments.append(subFilesToCheck);

    emit logMessage(QString("Асинхронный запуск: %1 %2").arg(m_wrapperPath, arguments.join(" ")));

    m_process->start(m_wrapperPath, arguments);
}

// Этот слот вызывается, когда процесс завершается
void FontFinder::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        emit logMessage("Критическая ошибка: Процесс поиска шрифтов завершился с ошибкой.");
        emit logMessage("Stderr: " + m_process->readAllStandardError());
        emit finished(FontFinderResult()); // Отправляем пустой результат при ошибке
        return;
    }

    QByteArray jsonData = m_process->readAllStandardOutput();
    FontFinderResult result = parseJsonOutput(jsonData);
    emit finished(result); // Отправляем готовый результат через сигнал
}

void FontFinder::onProcessError(QProcess::ProcessError error) {
    Q_UNUSED(error);
    emit logMessage("Ошибка запуска процесса поиска шрифтов: " + m_process->errorString());
    emit finished(FontFinderResult()); // Отправляем пустой результат при ошибке
}


// Методы parseJsonOutput и extractWrapper остаются без изменений
FontFinderResult FontFinder::parseJsonOutput(const QByteArray& jsonData) {
    FontFinderResult result;
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (doc.isNull()) {
        emit logMessage("Ошибка: не удалось распарсить JSON от Python-скрипта. " + parseError.errorString());
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
            emit logMessage(QString(" -> НАЙДЕН: '%1' (Файл: %2)").arg(fontInfo.familyName, fontInfo.path));
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
        emit logMessage("Критическая ошибка: не удалось открыть обертку из ресурсов (:/font_finder_wrapper.exe). Убедитесь, что она добавлена в .qrc файл.");
        return QString();
    }

    if (!resourceFile.copy(wrapperPathInTemp)) {
        emit logMessage(QString("Критическая ошибка: не удалось извлечь обертку во временную папку: %1").arg(wrapperPathInTemp));
        return QString();
    }

    QFile(wrapperPathInTemp).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);

    emit logMessage("Обертка для поиска шрифтов успешно извлечена.");
    return wrapperPathInTemp;
}
