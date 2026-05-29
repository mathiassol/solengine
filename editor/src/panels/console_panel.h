#pragma once
#include <QWidget>
#include "sol/log.h"

class QTextEdit;
class QComboBox;
class QPushButton;
class QHBoxLayout;
class QApplication;

#include <vector>

class ConsolePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ConsolePanel(QWidget* parent = nullptr);
    ~ConsolePanel() override;

    // Singleton accessor — valid after construction, nullptr after destruction
    static ConsolePanel* instance();

public slots:
    void appendLog(QtMsgType type, const QString& msg);
    void appendEngineLog(sol::log::Level level, const QString& msg);
    void clear();

private:
    struct LogEntry {
        bool engine = false;
        int level = 0;
        QString text;
    };

    void setupUi();
    void rebuildDisplay();

    QTextEdit* m_text{};
    QComboBox* m_level_filter{};
    std::vector<LogEntry> m_entries;

    static ConsolePanel* s_instance;
};
