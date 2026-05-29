#pragma once
#include <QWidget>
#include <QString>
#include <QPoint>
#include <QTimer>
#include <functional>
#include <memory>
#include <chrono>
#include <glm/glm.hpp>

namespace sol { class EngineHost; }
namespace sol { class Node; }
class EditorUI;
class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QShowEvent;
class QResizeEvent;
class QPaintEvent;

class ViewportWidget : public QWidget {
    Q_OBJECT
public:
    explicit ViewportWidget(sol::EngineHost* host,
                            const QString&   projectDir = QString(),
                            QWidget*         parent     = nullptr);
    ~ViewportWidget() override;

    void startRendering();
    void stopRendering();

    QPaintEngine* paintEngine() const override { return nullptr; }

signals:
    void nodeSelected(sol::Node* node);
    void gizmoOpChanged(int op);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override {}
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onTimer();

private:
    sol::EngineHost* m_host;
    QString          m_projectDir;
    QTimer*          m_timer;
    bool             m_initialized = false;

    bool      m_fly_cam_active = false;
    QPoint    m_fly_last_mouse {};
    bool      m_key_w = false, m_key_a = false, m_key_s = false, m_key_d = false;
    bool      m_key_q = false, m_key_e = false;
    bool      m_key_up = false, m_key_down = false;
    bool      m_key_left = false, m_key_right = false;
    glm::vec3 m_editor_cam_pos {0.0f, 5.0f, 15.0f};
    float     m_editor_cam_yaw = 0.0f;
    float     m_editor_cam_pitch = -15.0f;
    float     m_fly_speed = 10.0f;
    float     m_mouse_sens = 0.2f;
    int       m_gizmo_op = 0;
    std::unique_ptr<EditorUI> m_editor_ui;

    std::function<void()>            m_begin_move_fn;
    std::function<void(Qt::Edges)>   m_begin_resize_fn;

    std::chrono::steady_clock::time_point m_last_tick;
};
