#pragma once
#include <QDialog>
#include <QString>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QLabel;

// ---------------------------------------------------------------------------
// ProjectLauncherDialog
//
// Shown on startup when no project is passed on the command line, or when
// File → Open / File → New is triggered from the editor.
//
// Emits projectSelected(path) when the user wants to open a project.
// The dialog then hides itself; callers should show/open MainWindow.
// ---------------------------------------------------------------------------
class ProjectLauncherDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProjectLauncherDialog(QWidget* parent = nullptr);

    // Returns the chosen project directory, or "" if cancelled.
    QString selectedProject() const { return m_selected; }

signals:
    void projectSelected(const QString& projectDir);

private slots:
    void onNewProject();
    void onOpenProject();
    void onOpenSelected();
    void onRemoveSelected();
    void onItemDoubleClicked(QListWidgetItem* item);
    void onSelectionChanged();

private:
    void buildUi();
    void loadRecent();
    void saveRecent();
    void addRecent(const QString& path);
    void openProject(const QString& path);

    QListWidget* m_list{};
    QPushButton* m_openBtn{};
    QPushButton* m_removeBtn{};

    QString m_selected;
};
