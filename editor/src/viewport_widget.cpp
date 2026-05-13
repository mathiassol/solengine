#include "viewport_widget.h"
#include "sol/host.h"
#include "sol/scene/node.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QUrl>
#include <QWheelEvent>
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
            m_host->set_editor_camera_active(true);
            m_host->set_editor_camera(m_editor_cam_pos, m_editor_cam_yaw, m_editor_cam_pitch);
            m_host->set_gizmo_operation(m_gizmo_op);
            m_host->imgui_viewport_size(width(), height());
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
            url.toLocalFile().endsWith(".gltf", Qt::CaseInsensitive)) {
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
            !localPath.endsWith(".gltf", Qt::CaseInsensitive)) continue;

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
    if (m_initialized && m_host->is_open()) {
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

    if (event->button() == Qt::RightButton) {
        m_fly_cam_active = true;
        m_fly_last_mouse = rect().center();
        setCursor(Qt::BlankCursor);
        grabKeyboard();
        QCursor::setPos(mapToGlobal(m_fly_last_mouse));
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && !m_fly_cam_active) {
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
        const QPoint center = rect().center();
        if (std::abs(delta.x()) > 0 || std::abs(delta.y()) > 0) {
            QCursor::setPos(mapToGlobal(center));
            m_fly_last_mouse = center;
        }

        m_editor_cam_yaw -= static_cast<float>(delta.x()) * m_mouse_sens;
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

    if (event->angleDelta().y() > 0) {
        m_fly_speed = std::min(m_fly_speed * 1.2f, 500.0f);
    } else if (event->angleDelta().y() < 0) {
        m_fly_speed = std::max(m_fly_speed / 1.2f, 0.5f);
    }

    QWidget::wheelEvent(event);
}

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
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

    if (m_host && m_host->is_open() && !event->isAutoRepeat()) {
        switch (event->key()) {
        case Qt::Key_T: m_gizmo_op = 0; m_host->set_gizmo_operation(0); break;
        case Qt::Key_R: m_gizmo_op = 1; m_host->set_gizmo_operation(1); break;
        case Qt::Key_Y: m_gizmo_op = 2; m_host->set_gizmo_operation(2); break;
        default: break;
        }
    }

    QWidget::keyPressEvent(event);
}

void ViewportWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (!event->isAutoRepeat()) {
        switch (event->key()) {
        case Qt::Key_W: m_key_w = false; break;
        case Qt::Key_A: m_key_a = false; break;
        case Qt::Key_S: m_key_s = false; break;
        case Qt::Key_D: m_key_d = false; break;
        case Qt::Key_Q: m_key_q = false; break;
        case Qt::Key_E: m_key_e = false; break;
        default: break;
        }
    }
    QWidget::keyReleaseEvent(event);
}

void ViewportWidget::onTimer()
{
    if (!m_initialized || !m_host->is_open()) return;

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - m_last_tick).count();
    m_last_tick = now;

    if (m_fly_cam_active && m_host) {
        const float yaw_rad = glm::radians(m_editor_cam_yaw);
        const float pitch_rad = glm::radians(m_editor_cam_pitch);

        glm::vec3 forward {
            std::cos(pitch_rad) * std::sin(-yaw_rad),
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

    m_host->tick(dt);
}
