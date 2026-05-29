#include "viewport_widget.h"
#include "editor_ui.h"
#include "sol/host.h"
#include "sol/scene/node.h"
#include "sol/scene/node3d.h"

#include <imgui.h>

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QUrl>
#include <QWheelEvent>
#include <QWindow>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <windows.h>

namespace {

int toImGuiMouseButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:   return 0;
    case Qt::RightButton:  return 1;
    case Qt::MiddleButton: return 2;
    default:               return -1;
    }
}

ImGuiKey qtKeyToImGui(int key) {
    switch (key) {
    case Qt::Key_A: return ImGuiKey_A;
    case Qt::Key_B: return ImGuiKey_B;
    case Qt::Key_C: return ImGuiKey_C;
    case Qt::Key_D: return ImGuiKey_D;
    case Qt::Key_E: return ImGuiKey_E;
    case Qt::Key_F: return ImGuiKey_F;
    case Qt::Key_G: return ImGuiKey_G;
    case Qt::Key_H: return ImGuiKey_H;
    case Qt::Key_I: return ImGuiKey_I;
    case Qt::Key_J: return ImGuiKey_J;
    case Qt::Key_K: return ImGuiKey_K;
    case Qt::Key_L: return ImGuiKey_L;
    case Qt::Key_M: return ImGuiKey_M;
    case Qt::Key_N: return ImGuiKey_N;
    case Qt::Key_O: return ImGuiKey_O;
    case Qt::Key_P: return ImGuiKey_P;
    case Qt::Key_Q: return ImGuiKey_Q;
    case Qt::Key_R: return ImGuiKey_R;
    case Qt::Key_S: return ImGuiKey_S;
    case Qt::Key_T: return ImGuiKey_T;
    case Qt::Key_U: return ImGuiKey_U;
    case Qt::Key_V: return ImGuiKey_V;
    case Qt::Key_W: return ImGuiKey_W;
    case Qt::Key_X: return ImGuiKey_X;
    case Qt::Key_Y: return ImGuiKey_Y;
    case Qt::Key_Z: return ImGuiKey_Z;
    case Qt::Key_0: return ImGuiKey_0;
    case Qt::Key_1: return ImGuiKey_1;
    case Qt::Key_2: return ImGuiKey_2;
    case Qt::Key_3: return ImGuiKey_3;
    case Qt::Key_4: return ImGuiKey_4;
    case Qt::Key_5: return ImGuiKey_5;
    case Qt::Key_6: return ImGuiKey_6;
    case Qt::Key_7: return ImGuiKey_7;
    case Qt::Key_8: return ImGuiKey_8;
    case Qt::Key_9: return ImGuiKey_9;
    case Qt::Key_Return:
    case Qt::Key_Enter: return ImGuiKey_Enter;
    case Qt::Key_Escape: return ImGuiKey_Escape;
    case Qt::Key_Backspace: return ImGuiKey_Backspace;
    case Qt::Key_Delete: return ImGuiKey_Delete;
    case Qt::Key_Tab: return ImGuiKey_Tab;
    case Qt::Key_Space: return ImGuiKey_Space;
    case Qt::Key_Left: return ImGuiKey_LeftArrow;
    case Qt::Key_Right: return ImGuiKey_RightArrow;
    case Qt::Key_Up: return ImGuiKey_UpArrow;
    case Qt::Key_Down: return ImGuiKey_DownArrow;
    case Qt::Key_Home: return ImGuiKey_Home;
    case Qt::Key_End: return ImGuiKey_End;
    case Qt::Key_PageUp: return ImGuiKey_PageUp;
    case Qt::Key_PageDown: return ImGuiKey_PageDown;
    case Qt::Key_Insert: return ImGuiKey_Insert;
    case Qt::Key_F2: return ImGuiKey_F2;
    case Qt::Key_Control: return ImGuiKey_LeftCtrl;
    case Qt::Key_Shift: return ImGuiKey_LeftShift;
    case Qt::Key_Alt: return ImGuiKey_LeftAlt;
    default: return ImGuiKey_None;
    }
}

bool hasImGuiContext(sol::EngineHost* host) {
    return host && host->is_open() && host->engine().imgui_context();
}

bool imguiWantsMouse(sol::EngineHost* host) {
    if (!hasImGuiContext(host)) {
        return false;
    }
    ImGui::SetCurrentContext(host->engine().imgui_context());
    return ImGui::GetIO().WantCaptureMouse;
}

bool imguiWantsKeyboard(sol::EngineHost* host) {
    if (!hasImGuiContext(host)) {
        return false;
    }
    ImGui::SetCurrentContext(host->engine().imgui_context());
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard || io.WantTextInput;
}

} // namespace

ViewportWidget::ViewportWidget(sol::EngineHost* host, const QString& projectDir, QWidget* parent)
    : QWidget(parent), m_host(host), m_projectDir(projectDir)
{
    setAcceptDrops(true);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);

    m_timer = new QTimer(this);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, &ViewportWidget::onTimer);
}

ViewportWidget::~ViewportWidget()
{
    stopRendering();
    if (m_host) {
        m_host->set_editor_draw_fn({});
    }
    m_editor_ui.reset();
}

void ViewportWidget::startRendering()
{
    m_last_tick = std::chrono::steady_clock::now();
    m_timer->start();
}

void ViewportWidget::stopRendering()
{
    m_timer->stop();
}

void ViewportWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_initialized && m_host && isVisible()) {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        HINSTANCE hinst = GetModuleHandle(nullptr);
        const std::string projDir = m_projectDir.isEmpty() ? "." : m_projectDir.toStdString();
        const bool ok = m_host->open_for_editor(hwnd, hinst, width(), height(), projDir);
        if (ok) {
            // Sync the EXE's static GImGui pointer to the DLL's ImGuiContext.
            // Without this, the EXE's imgui.lib copy has a null GImGui and
            // every ImGui call from editor_ui.cpp triggers an ACCESS_VIOLATION.
            ImGui::SetCurrentContext(m_host->imgui_get_context());

            m_host->set_editor_camera_active(true);
            m_host->set_editor_camera(m_editor_cam_pos, m_editor_cam_yaw, m_editor_cam_pitch);
            m_host->set_gizmo_operation(m_gizmo_op);
            m_host->imgui_viewport_size(width(), height());
            m_editor_ui = std::make_unique<EditorUI>(m_host);
            m_editor_ui->setWindowMoveCallback([this]() {
                if (auto* win = window()) {
                    if (auto* handle = win->windowHandle())
                        handle->startSystemMove();
                }
            });
            m_editor_ui->setWindowMaximizeCallback([this]() {
                QWidget* w = window();
                if (w->isMaximized()) w->showNormal();
                else                   w->showMaximized();
            });
            m_editor_ui->setWindowMinimizeCallback([this]() { window()->showMinimized(); });
            m_editor_ui->setWindowCloseCallback   ([this]() { window()->close(); });
            m_editor_ui->setIsMaximizedCallback   ([this]() { return window()->isMaximized(); });
            // Camera state round-trip for per-tab scene switching
            m_editor_ui->setGetCameraCallback([this]() {
                return std::make_tuple(m_editor_cam_pos, m_editor_cam_yaw, m_editor_cam_pitch);
            });
            m_editor_ui->setSetCameraCallback([this](glm::vec3 pos, float yaw, float pitch) {
                m_editor_cam_pos   = pos;
                m_editor_cam_yaw   = yaw;
                m_editor_cam_pitch = pitch;
                if (m_host && m_host->is_open())
                    m_host->set_editor_camera(pos, yaw, pitch);
            });
            m_host->set_editor_draw_fn(m_editor_ui->drawFn());
            m_initialized = true;
            startRendering();
        }
    }
}

void ViewportWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls()) { event->ignore(); return; }
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.toLocalFile().endsWith(".glb", Qt::CaseInsensitive) ||
            url.toLocalFile().endsWith(".gltf", Qt::CaseInsensitive) ||
            url.toLocalFile().endsWith(".fbx", Qt::CaseInsensitive) ||
            url.toLocalFile().endsWith(".blend", Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void ViewportWidget::dragMoveEvent(QDragMoveEvent* event)
{
    event->acceptProposedAction();
}

void ViewportWidget::dropEvent(QDropEvent* event)
{
    if (!m_host || !m_host->is_open()) return;
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString localPath = url.toLocalFile();
        if (!localPath.endsWith(".glb", Qt::CaseInsensitive) &&
            !localPath.endsWith(".gltf", Qt::CaseInsensitive) &&
            !localPath.endsWith(".fbx", Qt::CaseInsensitive) &&
            !localPath.endsWith(".blend", Qt::CaseInsensitive)) continue;

        const QString relPath = m_projectDir.isEmpty()
            ? localPath
            : QDir(m_projectDir).relativeFilePath(localPath);

        (void)m_host->instantiate_model(relPath.toStdString());
    }
    event->acceptProposedAction();
}

void ViewportWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_initialized && m_host && m_host->is_open()) {
        m_host->imgui_viewport_size(event->size().width(), event->size().height());
        m_host->resize(event->size().width(), event->size().height());
    }
}

void ViewportWidget::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    if (m_initialized && m_host) m_host->set_imgui_focused(true);
}

void ViewportWidget::focusOutEvent(QFocusEvent* event)
{
    m_fly_cam_active = false;
    m_key_w = m_key_a = m_key_s = m_key_d = m_key_q = m_key_e = false;
    m_key_up = m_key_down = m_key_left = m_key_right = false;
    setCursor(Qt::ArrowCursor);
    releaseKeyboard();
    if (m_host) m_host->set_imgui_focused(false);
    QWidget::focusOutEvent(event);
}

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);

    if (m_host && m_host->is_open()) {
        const QPoint pos = event->pos();
        m_host->imgui_mouse_pos(static_cast<float>(pos.x()), static_cast<float>(pos.y()));
        const int button = toImGuiMouseButton(event->button());
        if (button >= 0) m_host->imgui_mouse_button(button, true);
    }

    const bool wantsMouse = imguiWantsMouse(m_host);

    if (event->button() == Qt::RightButton && !wantsMouse) {
        m_fly_cam_active = true;
        m_fly_last_mouse = event->pos();
        setCursor(Qt::BlankCursor);
        grabKeyboard();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && !m_fly_cam_active && !wantsMouse) {
        if (m_host && m_host->is_open()) {
            sol::Node* hit = m_host->pick_node(static_cast<float>(event->pos().x()),
                                               static_cast<float>(event->pos().y()));
            m_host->set_selected_node(hit);
            emit nodeSelected(hit);
        }
    }

    QWidget::mousePressEvent(event);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_host && m_host->is_open()) {
        const int button = toImGuiMouseButton(event->button());
        if (button >= 0) m_host->imgui_mouse_button(button, false);
    }

    if (event->button() == Qt::RightButton && m_fly_cam_active) {
        m_fly_cam_active = false;
        m_key_w = m_key_a = m_key_s = m_key_d = m_key_q = m_key_e = false;
        m_key_up = m_key_down = m_key_left = m_key_right = false;
        setCursor(Qt::ArrowCursor);
        releaseKeyboard();
    }

    QWidget::mouseReleaseEvent(event);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->pos();

    if (m_host && m_host->is_open()) {
        m_host->imgui_mouse_pos(static_cast<float>(pos.x()), static_cast<float>(pos.y()));
    }

    if (m_fly_cam_active) {
        const QPoint delta = pos - m_fly_last_mouse;
        m_fly_last_mouse = pos;

        m_editor_cam_yaw   -= static_cast<float>(delta.x()) * m_mouse_sens;
        m_editor_cam_pitch -= static_cast<float>(delta.y()) * m_mouse_sens;
        m_editor_cam_pitch = std::clamp(m_editor_cam_pitch, -89.0f, 89.0f);

        if (m_host && m_host->is_open()) {
            m_host->set_editor_camera(m_editor_cam_pos, m_editor_cam_yaw, m_editor_cam_pitch);
        }
    } else {
        m_fly_last_mouse = pos;
    }

    QWidget::mouseMoveEvent(event);
}

void ViewportWidget::wheelEvent(QWheelEvent* event)
{
    if (m_host && m_host->is_open()) {
        m_host->imgui_mouse_wheel(static_cast<float>(event->angleDelta().y()) / 120.0f);
    }

    if (!imguiWantsMouse(m_host)) {
        if (event->angleDelta().y() > 0) {
            m_fly_speed = std::min(m_fly_speed * 1.2f, 500.0f);
        } else if (event->angleDelta().y() < 0) {
            m_fly_speed = std::max(m_fly_speed / 1.2f, 0.5f);
        }
    }

    QWidget::wheelEvent(event);
}

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
    if (m_host && m_host->is_open() && !event->isAutoRepeat()) {
        const ImGuiKey imKey = qtKeyToImGui(event->key());
        if (imKey != ImGuiKey_None) m_host->set_imgui_key(imKey, true);
        if (!m_fly_cam_active && !event->text().isEmpty()) {
            const QByteArray utf8 = event->text().toUtf8();
            m_host->imgui_add_text(utf8.constData());
        }
    }

    if (m_host && m_host->is_open() && event->matches(QKeySequence::Save)) {
        m_host->save_scene();
        event->accept();
        return;
    }

    // Arrow keys: always control camera look rotation (remote desktop friendly)
    if (!event->isAutoRepeat()) {
        bool handled = false;
        switch (event->key()) {
        case Qt::Key_Up:    m_key_up    = true; handled = true; break;
        case Qt::Key_Down:  m_key_down  = true; handled = true; break;
        case Qt::Key_Left:  m_key_left  = true; handled = true; break;
        case Qt::Key_Right: m_key_right = true; handled = true; break;
        default: break;
        }
        if (handled) { event->accept(); return; }
    }

    if (m_fly_cam_active && !event->isAutoRepeat()) {
        switch (event->key()) {
        case Qt::Key_W: m_key_w = true; break;
        case Qt::Key_A: m_key_a = true; break;
        case Qt::Key_S: m_key_s = true; break;
        case Qt::Key_D: m_key_d = true; break;
        case Qt::Key_Q: m_key_q = true; break;
        case Qt::Key_E: m_key_e = true; break;
        default: break;
        }
        event->accept();
        return;
    }

    if (m_host && m_host->is_open() && !event->isAutoRepeat() && !imguiWantsKeyboard(m_host)) {
        switch (event->key()) {
        case Qt::Key_T: m_gizmo_op = 0; m_host->set_gizmo_operation(0); emit gizmoOpChanged(0); break;
        case Qt::Key_R: m_gizmo_op = 1; m_host->set_gizmo_operation(1); emit gizmoOpChanged(1); break;
        case Qt::Key_Y: m_gizmo_op = 2; m_host->set_gizmo_operation(2); emit gizmoOpChanged(2); break;
        case Qt::Key_F: {
            sol::Node* sel = m_host->selected_node();
            if (sel) {
                glm::vec3 target(0.0f);
                if (auto* n3d = dynamic_cast<sol::Node3D*>(sel))
                    target = n3d->position;
                const float dist = 5.0f;
                m_editor_cam_yaw = 0.0f;
                m_editor_cam_pitch = -20.0f;
                m_editor_cam_pos = target + glm::vec3(0.0f, dist * 0.3f, dist);
                m_host->set_editor_camera(m_editor_cam_pos, m_editor_cam_yaw, m_editor_cam_pitch);
            }
            break;
        }
        default: break;
        }
    }

    QWidget::keyPressEvent(event);
}

void ViewportWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (m_host && m_host->is_open() && !event->isAutoRepeat()) {
        const ImGuiKey imKey = qtKeyToImGui(event->key());
        if (imKey != ImGuiKey_None) m_host->set_imgui_key(imKey, false);
    }

    if (!event->isAutoRepeat()) {
        switch (event->key()) {
        case Qt::Key_W: m_key_w = false; break;
        case Qt::Key_A: m_key_a = false; break;
        case Qt::Key_S: m_key_s = false; break;
        case Qt::Key_D: m_key_d = false; break;
        case Qt::Key_Q: m_key_q = false; break;
        case Qt::Key_E: m_key_e = false; break;
        case Qt::Key_Up:    m_key_up    = false; break;
        case Qt::Key_Down:  m_key_down  = false; break;
        case Qt::Key_Left:  m_key_left  = false; break;
        case Qt::Key_Right: m_key_right = false; break;
        default: break;
        }
    }

    QWidget::keyReleaseEvent(event);
}

void ViewportWidget::onTimer()
{
    if (!m_initialized || !m_host || !m_host->is_open()) return;

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - m_last_tick).count();
    m_last_tick = now;

    if (m_fly_cam_active && m_host) {
        const float yaw_rad = glm::radians(m_editor_cam_yaw);
        const float pitch_rad = glm::radians(m_editor_cam_pitch);

        glm::vec3 forward {
            std::cos(pitch_rad) * std::sin(yaw_rad),
            std::sin(pitch_rad),
            std::cos(pitch_rad) * std::cos(yaw_rad)
        };
        if (glm::dot(forward, forward) > 0.0f) {
            forward = glm::normalize(forward);
        }

        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f}));
        glm::vec3 up {0.0f, 1.0f, 0.0f};

        const float speed = m_fly_speed * dt;
        if (m_key_w) m_editor_cam_pos += forward * speed;
        if (m_key_s) m_editor_cam_pos -= forward * speed;
        if (m_key_d) m_editor_cam_pos += right * speed;
        if (m_key_a) m_editor_cam_pos -= right * speed;
        if (m_key_e) m_editor_cam_pos += up * speed;
        if (m_key_q) m_editor_cam_pos -= up * speed;

        m_host->set_editor_camera(m_editor_cam_pos, m_editor_cam_yaw, m_editor_cam_pitch);
    }

    // Arrow keys: camera look rotation (always active, no fly mode required)
    if (m_key_up || m_key_down || m_key_left || m_key_right) {
        constexpr float look_speed = 60.0f; // degrees per second
        if (m_key_up)    m_editor_cam_pitch = std::min(m_editor_cam_pitch + look_speed * dt, 89.0f);
        if (m_key_down)  m_editor_cam_pitch = std::max(m_editor_cam_pitch - look_speed * dt, -89.0f);
        if (m_key_left)  m_editor_cam_yaw  += look_speed * dt;
        if (m_key_right) m_editor_cam_yaw  -= look_speed * dt;
        m_host->set_editor_camera(m_editor_cam_pos, m_editor_cam_yaw, m_editor_cam_pitch);
    }

    m_host->tick(dt);
}
