#pragma once
#include <QMainWindow>
#include <QString>
#include "sol/host.h"

class QDockWidget;
class QTimer;
class ConsolePanel;
class HierarchyPanel;
class InspectorPanel;
class AssetBrowserPanel;
class ViewportWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString& projectDir = QString(), QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onRefreshTick();

private:
    void setupCentralWidget();
    void setupPanels();
    void setupMenuBar();
    void setupToolBar();
    void restoreLayout();
    void saveLayout();

    QString            m_projectDir;
    ViewportWidget*    m_viewport{};
    sol::EngineHost*   m_host{};

    HierarchyPanel*    m_hierarchy{};
    InspectorPanel*    m_inspector{};
    AssetBrowserPanel* m_assetBrowser{};
    ConsolePanel*      m_console{};

    QDockWidget* m_hierarchyDock{};
    QDockWidget* m_inspectorDock{};
    QDockWidget* m_assetBrowserDock{};
    QDockWidget* m_consoleDock{};

    QTimer* m_refresh_timer{};
};
