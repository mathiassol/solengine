#include "asset_browser_panel.h"

#include "sol/host.h"

#include <QTreeView>
#include <QFileSystemModel>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QAbstractItemView>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

// ---------------------------------------------------------------------------
AssetBrowserPanel::AssetBrowserPanel(sol::EngineHost* host, const QString& projectDir, QWidget* parent)
    : QWidget(parent), m_host(host), m_projectDir(projectDir)
{
    setupUi();
}

void AssetBrowserPanel::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_model = new QFileSystemModel(this);
    // Show only recognised asset types; hide everything else
    m_model->setNameFilters({"*.glb", "*.solscene", "*.json",
                              "*.png", "*.jpg",      "*.hdr"});
    m_model->setNameFilterDisables(false); // hide non-matching files

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setDragEnabled(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragOnly);
    m_treeView->setDefaultDropAction(Qt::CopyAction);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setIndentation(14);
    m_treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    // Hide size / type / date columns — just show names
    m_treeView->hideColumn(1);
    m_treeView->hideColumn(2);
    m_treeView->hideColumn(3);

    setRootPath(m_projectDir.isEmpty() ? QDir::currentPath() : m_projectDir);

    connect(m_treeView, &QTreeView::doubleClicked,
            this, &AssetBrowserPanel::onDoubleClicked);

    auto* hint = new QLabel("Drag .glb onto the viewport to add to scene", this);
    hint->setProperty("muted", true);
    hint->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    hint->setContentsMargins(4, 4, 4, 0);
    layout->addWidget(hint);
    layout->addWidget(m_treeView);
}

// ---------------------------------------------------------------------------
void AssetBrowserPanel::setRootPath(const QString& path)
{
    m_projectDir = path;
    const QModelIndex rootIdx = m_model->setRootPath(path);
    m_treeView->setRootIndex(rootIdx);
}

// ---------------------------------------------------------------------------
void AssetBrowserPanel::onDoubleClicked(const QModelIndex& index)
{
    if (!m_host || !m_host->is_open()) return;

    const QString filePath = m_model->filePath(index);
    if (!filePath.endsWith(".solscene", Qt::CaseInsensitive)) return;

    // Make path relative to project dir for the engine API
    const QString rel = QDir(m_projectDir).relativeFilePath(filePath);
    if (!m_host->load_scene(rel.toStdString())) {
        QMessageBox::warning(this, "Load Scene",
            QString("Failed to load scene:\n%1").arg(rel));
    }
}
