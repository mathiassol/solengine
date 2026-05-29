#pragma once
#include <QWidget>
#include "sol/host.h"
#include "sol/scene/node.h"

Q_DECLARE_METATYPE(sol::Node*)

class QPoint;
class QTreeWidget;
class QTreeWidgetItem;

class HierarchyPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HierarchyPanel(sol::EngineHost* host, QWidget* parent = nullptr);

    void refresh();
    void clear();
    void setSelectedNode(sol::Node* node);

signals:
    void nodeSelected(sol::Node* node);
void nodeCreated(sol::Node* node);
void nodeRemoved(sol::Node* node);

private slots:
void onContextMenu(const QPoint& pos);
void onDeleteSelected();
void onRenameSelected();

private:
void buildTree(sol::Node* node, QTreeWidgetItem* parentItem);
void showPlaceholder();

    sol::EngineHost* m_host{};
    QTreeWidget*     m_tree{};
};
