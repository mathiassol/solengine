#pragma once
#include <QWidget>

class QTreeView;
class QFileSystemModel;

namespace sol { class EngineHost; }

class AssetBrowserPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AssetBrowserPanel(sol::EngineHost* host, const QString& projectDir, QWidget* parent = nullptr);

    // Change the root shown in the tree (call from MainWindow when project opens)
    void setRootPath(const QString& path);

private:
    void setupUi();
    void onDoubleClicked(const QModelIndex& index);

    sol::EngineHost*  m_host{};
    QString           m_projectDir;
    QTreeView*        m_treeView{};
    QFileSystemModel* m_model{};
};
