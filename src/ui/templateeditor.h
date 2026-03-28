#ifndef TEMPLATEEDITOR_H
#define TEMPLATEEDITOR_H

#include "releasetemplate.h"

#include <QDialog>
#include <QMap>
#include <QPointer>
#include <QTimer>
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QComboBox;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class TelegramPasteEdit;
struct PostParseResult;
namespace Ui
{
class TemplateEditor;
} // namespace Ui

class TemplateEditor : public QDialog
{
    Q_OBJECT

public:
    explicit TemplateEditor(QWidget* parent = nullptr);
    ~TemplateEditor();

    void setTemplate(const ReleaseTemplate& t);
    ReleaseTemplate getTemplate();
    void enableDraftAutosave(const QString& draftFilePath);
    void saveDraftNow();

private slots:
    void on_selectStylesButton_clicked();
    void on_helpButton_clicked();
    void slotValidateAndAccept();
    void onPostTemplateSelectionChanged();
    void onPostTemplateFilterChanged(const QString& filterText);
    void onPostTemplateAddClicked();
    void onPostTemplateDeleteClicked();
    void onPostTemplateRestoreBuiltinClicked();
    void onPostTemplateMetaEdited();
    void onPostTemplateBodyEdited();
    void onPostTemplateSaveClicked();
    void onParsePostToTemplateClicked();
    void onOpenPostTemplateCatalogClicked();

private:
    void setupPostTemplateCatalogUi();
    void buildPostTemplateCatalogDialogUi(QDialog* dialog);
    static void ensureBuiltinLegacyPostTemplates(QMap<QString, QString>& templates);
    void sanitizePostTemplateCatalog();
    void loadPostTemplateCatalogFromDisk(const ReleaseTemplate& t);
    void savePostTemplateCatalogToDisk() const;
    void syncPostTemplateEditorsFromTemplate();
    void syncPostTemplateCatalogToTemplate(ReleaseTemplate& t) const;
    void rebuildPostTemplateTree(const QString& filterText);
    void saveCurrentPostTemplateEdits(const QString& key = QString());
    void loadSelectedPostTemplateToEditor();
    QPlainTextEdit* legacyEditorForPostKey(const QString& key) const;
    void applyParsedFieldsToTemplate(const PostParseResult& parseResult);
    static QMap<QString, QString> builtinPostTemplates();
    static QMap<QString, PostTemplateMeta> builtinPostTemplateMeta();
    static QString displayNameForPostKey(const QString& key);
    static QString platformForPostKey(const QString& key);
    static bool isTelegramPlatform(const QString& platform);
    QString selectedPostTemplateKey() const;
    QString createUniquePostTemplateKey(const QString& title) const;

    static bool containsForbiddenChars(const QString& text);
    static QString forbiddenCharsFound(const QString& text);

    QMap<QString, QString> m_postTemplates;
    QMap<QString, PostTemplateMeta> m_postTemplateMeta;
    QString m_selectedPostTemplateKey;

    QLineEdit* m_postTemplateSearchEdit = nullptr;
    QTreeWidget* m_postTemplateTree = nullptr;
    QLineEdit* m_postTemplateTitleEdit = nullptr;
    QComboBox* m_postTemplateTypeEdit = nullptr;
    TelegramPasteEdit* m_postTemplateBodyEdit = nullptr;
    QPushButton* m_postTemplateDeleteButton = nullptr;
    QPushButton* m_postTemplateSaveButton = nullptr;
    QComboBox* m_parseSourceComboBox = nullptr;
    QPushButton* m_parsePostButton = nullptr;
    QPushButton* m_openCatalogButton = nullptr;
    QPointer<QDialog> m_postTemplateDialog;

    QString m_draftFilePath;
    QByteArray m_lastDraftPayload;
    QTimer m_draftAutosaveTimer;
    QPointer<QDialog> m_helpDialog;
    Ui::TemplateEditor* ui;
};

#endif // TEMPLATEEDITOR_H
