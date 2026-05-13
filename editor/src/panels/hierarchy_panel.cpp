#include "hierarchy_panel.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QSignalBlocker>

// ---------------------------------------------------------------------------
HierarchyPanel::HierarchyPanel(sol::EngineHost* host, QWidget* parent)
    : QWidget(parent), m_host(host)
{
    qRegisterMetaType<sol::Node*>();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({"Name", "Type"});
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setIndentation(14);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->hideColumn(1);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (!current) {
            emit nodeSelected(nullptr);
            return;
        }
        auto* node = current->data(0, Qt::UserRole).value<sol::Node*>();
        emit nodeSelected(node);
    });

    showPlaceholder();
}

// ---------------------------------------------------------------------------
void HierarchyPanel::showPlaceholder()
{
    auto* item = new QTreeWidgetItem(m_tree, {"(empty)", ""});
    item->setForeground(0, QColor(108, 112, 134));
    QFont f = item->font(0);
    f.setItalic(true);
    item->setFont(0, f);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
}

// ---------------------------------------------------------------------------
void HierarchyPanel::buildTree(sol::Node* node, QTreeWidgetItem* parentItem)
{
    QTreeWidgetItem* item;
    if (parentItem)
        item = new QTreeWidgetItem(parentItem);
    else
        item = new QTreeWidgetItem(m_tree);

    item->setText(0, QString::fromStdString(node->name));
    item->setText(1, QString(node->type_name()));
    item->setData(0, Qt::UserRole, QVariant::fromValue(node));

    for (const auto& child : node->children())
        buildTree(child.get(), item);
}

// ---------------------------------------------------------------------------
void HierarchyPanel::refresh()
{
    // Remember current selection so we can restore it after rebuild.
    sol::Node* prevSelected = nullptr;
    if (auto* cur = m_tree->currentItem())
        prevSelected = cur->data(0, Qt::UserRole).value<sol::Node*>();

    // Block ALL signals for the entire rebuild.
    // Without this, clear() emits currentItemChanged(nullptr) which cascades
    // into InspectorPanel::showNode(nullptr) and blanks the inspector.
    {
        QSignalBlocker blocker(m_tree);

        m_tree->clear();

        sol::Node* root = (m_host && m_host->is_open()) ? m_host->scene_root() : nullptr;
        if (!root) {
            showPlaceholder();
            return;
        }

        buildTree(root, nullptr);
        m_tree->expandAll();

        // Restore selection silently.
        if (prevSelected) {
            QTreeWidgetItemIterator it(m_tree);
            while (*it) {
                auto* node = (*it)->data(0, Qt::UserRole).value<sol::Node*>();
                if (node == prevSelected) {
                    m_tree->setCurrentItem(*it);
                    break;
                }
                ++it;
            }
        }
    }
    // Signals unblocked here — no spurious events fired during rebuild.
}

// ---------------------------------------------------------------------------
void HierarchyPanel::setSelectedNode(sol::Node* node)
{
    QSignalBlocker blocker(m_tree);

    if (!node) {
        m_tree->setCurrentItem(nullptr);
        m_tree->clearSelection();
        return;
    }

    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).value<sol::Node*>() == node) {
            m_tree->setCurrentItem(*it);
            m_tree->scrollToItem(*it);
            return;
        }
        ++it;
    }
}

// ---------------------------------------------------------------------------
void HierarchyPanel::clear()
{
    m_tree->clear();
}
