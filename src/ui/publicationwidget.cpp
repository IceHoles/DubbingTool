#include "publicationwidget.h"

#include "telegramformatter.h"
#include "ui_publicationwidget.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QTextDocument>
#include <QUrl>
#include <utility>

void DraggableLabel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_filePath.isEmpty())
    {
        QDrag* drag = new QDrag(this);
        QMimeData* mimeData = new QMimeData;

        QList<QUrl> urls;
        urls.append(QUrl::fromLocalFile(m_filePath));
        mimeData->setUrls(urls);

        drag->setMimeData(mimeData);
        drag->setPixmap(this->pixmap());
        drag->exec(Qt::CopyAction);
    }
}

PublicationWidget::PublicationWidget(QWidget* parent) : QWidget(parent), ui(new Ui::PublicationWidget)
{
    ui->setupUi(this);

    m_chapterTimingsGroup = new QGroupBox(QStringLiteral("Тайминги глав (секунды)"), this);
    m_chapterRowsLayout = new QVBoxLayout();
    m_chapterRowsLayout->setContentsMargins(6, 6, 6, 6);
    m_chapterRowsLayout->setSpacing(6);
    m_chapterTimingsGroup->setLayout(m_chapterRowsLayout);
    m_chapterTimingsGroup->setVisible(false);
    ui->leftColumnLayout->addWidget(m_chapterTimingsGroup);

    // Подключаем кнопки копирования напрямую к данным в m_currentPosts
    connect(ui->copyTgMp4Button, &QPushButton::clicked, this,
            [this]()
            {
                TelegramFormatter::formatAndCopyToClipboard(m_currentPosts.value("tg_mp4").markdown);
                emit logMessage(QString("Пост '%1' скопирован в формате Telegram.").arg("Telegram (MP4)"),
                                LogCategory::APP);
                QMessageBox::information(this, "Готово", "Форматированный текст для Telegram скопирован!");
            });

    connect(ui->copyTgMkvButton, &QPushButton::clicked, this,
            [this]()
            {
                TelegramFormatter::formatAndCopyToClipboard(m_currentPosts.value("tg_mkv").markdown);
                emit logMessage(QString("Пост '%1' скопирован в формате Telegram.").arg("Telegram (MKV)"),
                                LogCategory::APP);
                QMessageBox::information(this, "Готово", "Форматированный текст для Telegram скопирован!");
            });

    connect(ui->copyVkButton, &QPushButton::clicked, this,
            [this]()
            {
                QApplication::clipboard()->setText(m_currentPosts.value("vk").markdown);
                emit logMessage(QString("Пост '%1' скопирован.").arg("VK"), LogCategory::APP);
            });

    connect(ui->copyVkCommentButton, &QPushButton::clicked, this,
            [this]()
            {
                QApplication::clipboard()->setText(m_currentPosts.value("vk_comment").markdown);
                emit logMessage(QString("Пост '%1' скопирован.").arg("VK (комментарий)"), LogCategory::APP);
            });

    connect(ui->openUploadUrlsButton, &QPushButton::clicked, this, &PublicationWidget::onOpenUploadUrls);
    connect(ui->updatePostsButton, &QPushButton::clicked, this, &PublicationWidget::onUpdatePosts);
}

PublicationWidget::~PublicationWidget()
{
    delete ui;
}

void PublicationWidget::updateData(const ReleaseTemplate& t, const EpisodeData& data,
                                   const QMap<QString, PostVersions>& posts, const QString& mkvPath,
                                   const QString& mp4Path)
{
    m_template = t;
    m_episodeData = data;
    m_currentPosts = posts;

    // Отображаем HTML-версию
    ui->tgMkvPostEdit->setPlainText(posts.value("tg_mkv").markdown);
    ui->tgMp4PostEdit->setPlainText(posts.value("tg_mp4").markdown);
    ui->vkPostEdit->setPlainText(posts.value("vk").markdown);
    ui->vkCommentEdit->setPlainText(posts.value("vk_comment").markdown);

    setFilePaths(mkvPath, mp4Path);
}

void PublicationWidget::setFilePaths(const QString& mkvPath, const QString& mp4Path)
{
    QPixmap poster(m_template.posterPath);
    if (!poster.isNull())
    {
        ui->posterLabel->setPixmap(
            poster.scaled(ui->posterLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    else
    {
        ui->posterLabel->setText("Постер не найден");
    }
    ui->posterLabel->setFilePath(m_template.posterPath);

    ui->mkvFileLabel->setFilePath(mkvPath);
    if (mkvPath.isEmpty())
        ui->mkvFileLabel->setText("MKV (в процессе...)");
    else
        ui->mkvFileLabel->setText("MKV файл");

    ui->mp4FileLabel->setFilePath(mp4Path);
    if (mp4Path.isEmpty())
        ui->mp4FileLabel->setText("MP4 (в процессе...)");
    else
        ui->mp4FileLabel->setText("MP4 файл");
}

void PublicationWidget::setChapterTimings(const QList<ChapterMarker>& chapters, qint64 durationNs)
{
    m_chapterTimings = ChapterHelper::buildChapterTimingSeconds(chapters, durationNs);
    rebuildChapterRows();
}

void PublicationWidget::clearData()
{
    m_template = ReleaseTemplate();
    m_episodeData = EpisodeData();
    ui->tgMkvPostEdit->clear();
    ui->tgMp4PostEdit->clear();
    ui->vkPostEdit->clear();
    ui->vkCommentEdit->clear();
    ui->linkAnime365Edit->clear();
    ui->linkAnilibEdit->clear();
    m_chapterTimings.clear();
    rebuildChapterRows();
    setFilePaths("", "");
}

void PublicationWidget::rebuildChapterRows()
{
    if (!m_chapterRowsLayout || !m_chapterTimingsGroup)
    {
        return;
    }

    while (QLayoutItem* item = m_chapterRowsLayout->takeAt(0))
    {
        if (QWidget* widget = item->widget())
        {
            widget->deleteLater();
        }
        delete item;
    }

    if (m_chapterTimings.isEmpty())
    {
        m_chapterTimingsGroup->setVisible(false);
        return;
    }

    for (const ChapterTimingSeconds& row : std::as_const(m_chapterTimings))
    {
        QWidget* line = new QWidget(m_chapterTimingsGroup);
        auto* layout = new QHBoxLayout(line);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto* titleLabel = new QLabel(row.title.isEmpty() ? QStringLiteral("Chapter") : row.title, line);
        auto* startValueLabel = new QLabel(QString::number(row.startSeconds), line);
        auto* endValueLabel = new QLabel(QString::number(row.endSeconds), line);
        auto* copyStartButton = new QPushButton(QStringLiteral("Копировать"), line);
        auto* copyEndButton = new QPushButton(QStringLiteral("Копировать"), line);

        connect(copyStartButton, &QPushButton::clicked, this,
                [this, row]()
                {
                    QApplication::clipboard()->setText(QString::number(row.startSeconds));
                    emit logMessage(QStringLiteral("Скопировано начало главы '%1'.").arg(row.title), LogCategory::APP);
                });
        connect(copyEndButton, &QPushButton::clicked, this,
                [this, row]()
                {
                    QApplication::clipboard()->setText(QString::number(row.endSeconds));
                    emit logMessage(QStringLiteral("Скопирован конец главы '%1'.").arg(row.title), LogCategory::APP);
                });

        layout->addWidget(titleLabel, 2);
        layout->addWidget(new QLabel(QStringLiteral("Начало:"), line));
        layout->addWidget(startValueLabel);
        layout->addWidget(copyStartButton);
        layout->addSpacing(8);
        layout->addWidget(new QLabel(QStringLiteral("Конец:"), line));
        layout->addWidget(endValueLabel);
        layout->addWidget(copyEndButton);
        m_chapterRowsLayout->addWidget(line);
    }

    m_chapterTimingsGroup->setVisible(true);
}

void PublicationWidget::onOpenUploadUrls()
{
    if (m_template.uploadUrls.isEmpty())
    {
        QMessageBox::information(this, "Информация", "В шаблоне не указаны ссылки для загрузки.");
        return;
    }

    emit logMessage("Открытие ссылок для загрузки в браузере...", LogCategory::APP);
    for (const QString& urlString : m_template.uploadUrls)
    {
        QString url = urlString;
        url.replace("%EPISODE_NUMBER%", m_episodeData.episodeNumber);
        QDesktopServices::openUrl(QUrl(url));
    }
}

void PublicationWidget::onUpdatePosts()
{
    QMap<QString, QString> links;
    links["Anilib"] = ui->linkAnilibEdit->text();
    links["Anime365"] = ui->linkAnime365Edit->text();

    emit postsUpdateRequest(links);
}
