#ifndef PUBLICATIONWIDGET_H
#define PUBLICATIONWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QMimeData>
#include <QDrag>
#include <QMap>
#include "releasetemplate.h"
#include "postgenerator.h"
#include "appsettings.h"


class DraggableLabel : public QLabel {
    Q_OBJECT
public:
    using QLabel::QLabel;
    void setFilePath(const QString& path) { m_filePath = path; }
protected:
    void mousePressEvent(QMouseEvent *event) override;
private:
    QString m_filePath;
};

namespace Ui {
class PublicationWidget;
}

class PublicationWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PublicationWidget(QWidget *parent = nullptr);
    ~PublicationWidget();

    void updateData(const ReleaseTemplate &t, const EpisodeData &data, const QMap<QString, PostVersions>& posts, const QString& mkvPath, const QString& mp4Path);
    void setFilePaths(const QString& mkvPath, const QString& mp4Path);
    void clearData();

signals:
    void logMessage(const QString&, LogCategory);
    void postsUpdateRequest(const QMap<QString, QString>& viewLinks);

private slots:
    void onOpenUploadUrls();
    void onUpdatePosts();

private:
    Ui::PublicationWidget *ui;

    ReleaseTemplate m_template;
    EpisodeData m_episodeData;
    QMap<QString, PostVersions> m_currentPosts;
};

#endif // PUBLICATIONWIDGET_H
