#include "publicationwidget.h"
#include "ui_publicationwidget.h"
#include <QApplication>
#include <QClipboard>
#include <QPixmap>
#include <QUrl>
#include <QDesktopServices>
#include <QMessageBox>
#include <QMimeData>
#include <QTextDocument>
#include <QMouseEvent>


void DraggableLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_filePath.isEmpty()) {
        QDrag *drag = new QDrag(this);
        QMimeData *mimeData = new QMimeData;

        QList<QUrl> urls;
        urls.append(QUrl::fromLocalFile(m_filePath));
        mimeData->setUrls(urls);

        drag->setMimeData(mimeData);
        drag->setPixmap(this->pixmap());
        drag->exec(Qt::CopyAction);
    }
}

// Реализация основного виджета
PublicationWidget::PublicationWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PublicationWidget)
{
    ui->setupUi(this);

    // Подключаем кнопки копирования напрямую к данным в m_currentPosts
    connect(ui->copyTgMp4Button, &QPushButton::clicked, this, [this](){
        QApplication::clipboard()->setText(m_currentPosts.value("tg_mp4").markdown);
        emit logMessage(QString("Пост '%1' скопирован.").arg("Telegram (MP4)"));
    });

    connect(ui->copyTgMkvButton, &QPushButton::clicked, this, [this](){
        QApplication::clipboard()->setText(m_currentPosts.value("tg_mkv").markdown);
        emit logMessage(QString("Пост '%1' скопирован.").arg("Telegram (MKV)"));
    });

    connect(ui->copyVkButton, &QPushButton::clicked, this, [this](){
        QApplication::clipboard()->setText(m_currentPosts.value("vk").markdown);
        emit logMessage(QString("Пост '%1' скопирован.").arg("VK"));
    });

    connect(ui->copyVkCommentButton, &QPushButton::clicked, this, [this](){
        QApplication::clipboard()->setText(m_currentPosts.value("vk_comment").markdown);
        emit logMessage(QString("Пост '%1' скопирован.").arg("VK (комментарий)"));
    });

    connect(ui->openUploadUrlsButton, &QPushButton::clicked, this, &PublicationWidget::onOpenUploadUrls);
    connect(ui->updatePostsButton, &QPushButton::clicked, this, &PublicationWidget::onUpdatePosts);
}

PublicationWidget::~PublicationWidget()
{
    delete ui;
}

void PublicationWidget::updateData(const ReleaseTemplate &t, const EpisodeData &data, const QMap<QString, PostVersions>& posts, const QString &mkvPath, const QString &mp4Path)
{
    m_template = t;
    m_episodeData = data;
    m_currentPosts = posts;

    // Отображаем HTML-версию
    ui->tgMkvPostEdit->setHtml(posts.value("tg_mkv").html);
    ui->tgMp4PostEdit->setHtml(posts.value("tg_mp4").html);
    ui->vkPostEdit->setHtml(posts.value("vk").html);
    ui->vkCommentEdit->setHtml(posts.value("vk_comment").html);

    setFilePaths(mkvPath, mp4Path);
}

void PublicationWidget::setFilePaths(const QString &mkvPath, const QString &mp4Path)
{
    QPixmap poster(m_template.posterPath);
    if (!poster.isNull()) {
        ui->posterLabel->setPixmap(poster.scaled(ui->posterLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        ui->posterLabel->setText("Постер не найден");
    }
    ui->posterLabel->setFilePath(m_template.posterPath);

    ui->mkvFileLabel->setFilePath(mkvPath);
    if(mkvPath.isEmpty()) ui->mkvFileLabel->setText("MKV (в процессе...)");
    else ui->mkvFileLabel->setText("MKV файл");

    ui->mp4FileLabel->setFilePath(mp4Path);
    if(mp4Path.isEmpty()) ui->mp4FileLabel->setText("MP4 (в процессе...)");
    else ui->mp4FileLabel->setText("MP4 файл");
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
    setFilePaths("", "");
}


void PublicationWidget::onOpenUploadUrls()
{
    if (m_template.uploadUrls.isEmpty()) {
        QMessageBox::information(this, "Информация", "В шаблоне не указаны ссылки для загрузки.");
        return;
    }

    emit logMessage("Открытие ссылок для загрузки в браузере...");
    for (const QString &urlString : m_template.uploadUrls) {
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
    // ...
    emit postsUpdateRequest(links);
}
