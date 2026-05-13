#include "main_window.h"
#include "viewport_widget.h"
#include "panels/console_panel.h"
#include "panels/hierarchy_panel.h"
#include "panels/inspector_panel.h"
#include "panels/asset_browser_panel.h"

#include "sol/host.h"

#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QAction>
#include <QDockWidget>
#include <QStatusBar>
#include <QCloseEvent>
#include <QSettings>
#include <QMessageBox>
#include <QTimer>
#include <QFileInfo>
#include <QSize>

// ---------------------------------------------------------------------------
MainWindow::MainWindow(const QString& projectDir, QWidget* parent)
    : QMainWindow(parent), m_projectDir(projectDir)
{
    const QString projectName = projectDir.isEmpty()
        ? QString()
        : QFileInfo(projectDir).fileName();
    setWindowTitle(projectName.isEmpty()
        ? "SolEngine"
        : "SolEngine — " + projectName);
    resize(1400, 900);
    setDockNestingEnabled(true);

    // Order matters: panels must exist before the View menu reads their actions
    setupCentralWidget();
    setupPanels();
    setupMenuBar();
    setupToolBar();
    restoreLayout();

    statusBar()->showMessage("Ready");
}

MainWindow::~MainWindow()
{
    if (m_host) {
        m_host->close();
        delete m_host;
    }
}

// ---------------------------------------------------------------------------
void MainWindow::setupCentralWidget()
{
    m_host = new sol::EngineHost();
    m_viewport = new ViewportWidget(m_host, m_projectDir, this);
    setCentralWidget(m_viewport);
}

// ---------------------------------------------------------------------------
void MainWindow::setupPanels()
{
    m_hierarchy    = new HierarchyPanel(m_host, this);
    m_inspector    = new InspectorPanel(m_host, this);
    m_assetBrowser = new AssetBrowserPanel(m_host, m_projectDir, this);
    m_console      = new ConsolePanel(this);

    m_hierarchyDock = new QDockWidget("Hierarchy", this);
    m_hierarchyDock->setObjectName("HierarchyDock");
    m_hierarchyDock->setWidget(m_hierarchy);
    addDockWidget(Qt::LeftDockWidgetArea, m_hierarchyDock);

    m_inspectorDock = new QDockWidget("Inspector", this);
    m_inspectorDock->setObjectName("InspectorDock");
    m_inspectorDock->setWidget(m_inspector);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

    m_assetBrowserDock = new QDockWidget("Asset Browser", this);
    m_assetBrowserDock->setObjectName("AssetBrowserDock");
    m_assetBrowserDock->setWidget(m_assetBrowser);
    addDockWidget(Qt::BottomDockWidgetArea, m_assetBrowserDock);

    m_consoleDock = new QDockWidget("Console", this);
    m_consoleDock->setObjectName("ConsoleDock");
    m_consoleDock->setWidget(m_console);
    addDockWidget(Qt::BottomDockWidgetArea, m_consoleDock);

    // Place asset browser and console side-by-side in the bottom area
    splitDockWidget(m_assetBrowserDock, m_consoleDock, Qt::Horizontal);

    // Wire hierarchy selection → inspector
    connect(m_hierarchy, &HierarchyPanel::nodeSelected,
            this, [this](sol::Node* node) {
                if (m_host) m_host->set_selected_node(node);
                m_inspector->showNode(node);
            });
    connect(m_viewport, &ViewportWidget::nodeSelected,
            m_hierarchy, &HierarchyPanel::setSelectedNode);
    connect(m_viewport, &ViewportWidget::nodeSelected,
            m_inspector, &InspectorPanel::showNode);

    // 1 Hz refresh timer — only ticks when host is open
    m_refresh_timer = new QTimer(this);
    m_refresh_timer->setInterval(1000);
    connect(m_refresh_timer, &QTimer::timeout, this, &MainWindow::onRefreshTick);
    m_refresh_timer->start();
}

// ---------------------------------------------------------------------------
void MainWindow::setupMenuBar()
{
    // ---- File ---------------------------------------------------------------
    QMenu* fileMenu = menuBar()->addMenu("&File");

    auto* newAction  = fileMenu->addAction("&New Project");
    auto* openAction = fileMenu->addAction("&Open Project...");
    auto* saveAction = fileMenu->addAction("&Save");
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction("E&xit");

    newAction->setShortcut(QKeySequence::New);
    openAction->setShortcut(QKeySequence::Open);
    saveAction->setShortcut(QKeySequence::Save);
    exitAction->setShortcut(QKeySequence::Quit);

    // Stubs — Phase 4 will wire these up
    connect(newAction,  &QAction::triggered, this, []{ /* TODO Phase 4 */ });
    connect(openAction, &QAction::triggered, this, []{ /* TODO Phase 4 */ });
    connect(saveAction, &QAction::triggered, this, [this] {
        if (m_host && m_host->is_open()) {
            bool ok = m_host->save_scene();
            statusBar()->showMessage(ok ? "Scene saved." : "Save failed!", 3000);
        }
    });
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);

    // ---- View ---------------------------------------------------------------
    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(m_hierarchyDock->toggleViewAction());
    viewMenu->addAction(m_inspectorDock->toggleViewAction());
    viewMenu->addAction(m_assetBrowserDock->toggleViewAction());
    viewMenu->addAction(m_consoleDock->toggleViewAction());

    // ---- Help ---------------------------------------------------------------
    QMenu* helpMenu = menuBar()->addMenu("&Help");
    auto* aboutAction = helpMenu->addAction("&About SolEngine");

    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this,
            "About SolEngine Editor",
            "<b>SolEngine Editor</b> v0.1.0<br>"
            "Phase 3 \xe2\x80\x94 Qt6 scaffold.<br><br>"
            "Built with Qt " QT_VERSION_STR "."
        );
    });
}

// ---------------------------------------------------------------------------
void MainWindow::setupToolBar()
{
    QToolBar* tb = addToolBar("Main");
    tb->setObjectName("MainToolBar");
    tb->setMovable(false);
    tb->setIconSize(QSize(16, 16));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // -- File actions ---------------------------------------------------------
    auto* newAction  = tb->addAction("New");
    auto* openAction = tb->addAction("Open");
    auto* saveAction = tb->addAction("Save");

    newAction->setShortcut(QKeySequence::New);
    openAction->setShortcut(QKeySequence::Open);
    saveAction->setShortcut(QKeySequence::Save);

    connect(newAction,  &QAction::triggered, this, []{ /* TODO Phase 4 */ });
    connect(openAction, &QAction::triggered, this, []{ /* TODO Phase 4 */ });
    connect(saveAction, &QAction::triggered, this, [this] {
        if (m_host && m_host->is_open()) {
            bool ok = m_host->save_scene();
            statusBar()->showMessage(ok ? "Scene saved." : "Save failed!", 3000);
        }
    });

    tb->addSeparator();

    // -- Playback actions (disabled until Phase 4) ----------------------------
    auto* playAction  = tb->addAction("\xe2\x96\xb6  Play");
    auto* pauseAction = tb->addAction("\xe2\x8f\xb8  Pause");
    auto* stopAction  = tb->addAction("\xe2\x96\xa0  Stop");

    playAction->setEnabled(false);
    pauseAction->setEnabled(false);
    stopAction->setEnabled(false);
}

// ---------------------------------------------------------------------------
void MainWindow::restoreLayout()
{
    QSettings settings("SolEngine", "Editor");
    if (settings.contains("geometry"))
        restoreGeometry(settings.value("geometry").toByteArray());
    if (settings.contains("windowState"))
        restoreState(settings.value("windowState").toByteArray());
}

void MainWindow::saveLayout()
{
    QSettings settings("SolEngine", "Editor");
    settings.setValue("geometry",    saveGeometry());
    settings.setValue("windowState", saveState());
}

// ---------------------------------------------------------------------------
void MainWindow::onRefreshTick()
{
    if (!m_host || !m_host->is_open()) return;
    m_hierarchy->refresh();
    m_inspector->refresh();
}

// ---------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent* event)
{
    saveLayout();
    QMainWindow::closeEvent(event);
}
