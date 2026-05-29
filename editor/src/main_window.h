#pragma once

#include <QMainWindow>
#include <QString>

namespace sol { class EngineHost; }
class QCloseEvent;
class ViewportWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& projectDir = QString(), QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private slots:
    void showProjectLauncher();

private:
    void saveLayout();
    void restoreLayout();

    QString          m_projectDir;
    ViewportWidget*  m_viewport{};
    sol::EngineHost* m_host{};

    static constexpr int kResizeBorder = 6;
};
