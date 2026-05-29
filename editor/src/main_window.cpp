#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "main_window.h"

#include "project_launcher.h"
#include "sol/host.h"
#include "viewport_widget.h"

#include <QCloseEvent>
#include <QDialog>
#include <QFileInfo>
#include <QSettings>

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

MainWindow::MainWindow(const QString& projectDir, QWidget* parent)
    : QMainWindow(parent), m_projectDir(projectDir)
{
    const QString projectName = projectDir.isEmpty()
        ? QString()
        : QFileInfo(projectDir).fileName();
    setWindowTitle(projectName.isEmpty() ? "SolEngine" : "SolEngine — " + projectName);
    resize(1600, 1000);

    // Frameless — all chrome is custom ImGui
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, false);

    // Tell DWM to remove the default shadow
    HWND hwnd = reinterpret_cast<HWND>(winId());
    MARGINS margins{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    m_host = new sol::EngineHost();
    m_viewport = new ViewportWidget(m_host, m_projectDir, this);
    setCentralWidget(m_viewport);
    setMinimumSize(800, 600);

    restoreLayout();
}

MainWindow::~MainWindow() {
    if (m_host) {
        m_host->close();
        delete m_host;
        m_host = nullptr;
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveLayout();
    QMainWindow::closeEvent(event);
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            const int x = GET_X_LPARAM(msg->lParam);
            const int y = GET_Y_LPARAM(msg->lParam);
            const QPoint local = mapFromGlobal(QPoint(x, y));
            const int w = width();
            const int h = height();
            const int b = kResizeBorder;

            bool left   = local.x() < b;
            bool right  = local.x() > w - b;
            bool top    = local.y() < b;
            bool bottom = local.y() > h - b;

            if      (top    && left)  { *result = HTTOPLEFT;     return true; }
            else if (top    && right) { *result = HTTOPRIGHT;    return true; }
            else if (bottom && left)  { *result = HTBOTTOMLEFT;  return true; }
            else if (bottom && right) { *result = HTBOTTOMRIGHT; return true; }
            else if (top)             { *result = HTTOP;         return true; }
            else if (bottom)          { *result = HTBOTTOM;      return true; }
            else if (left)            { *result = HTLEFT;        return true; }
            else if (right)           { *result = HTRIGHT;       return true; }

            *result = HTCLIENT;
            return true;
        }
        if (msg->message == WM_NCCALCSIZE) {
            if (msg->wParam) {
                *result = 0;
                return true;
            }
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::saveLayout() {
    QSettings settings("SolEngine", "Editor");
    settings.setValue("geometry", saveGeometry());
}

void MainWindow::restoreLayout() {
    QSettings settings("SolEngine", "Editor");
    if (settings.contains("geometry"))
        restoreGeometry(settings.value("geometry").toByteArray());
}

void MainWindow::showProjectLauncher() {
    ProjectLauncherDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString newDir = dlg.selectedProject();
    if (newDir.isEmpty() || newDir == m_projectDir)
        return;

    if (m_host && m_host->is_open())
        m_host->close();

    auto* w = new MainWindow(newDir);
    w->show();
    close();
}
