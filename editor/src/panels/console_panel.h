#pragma once
#include <QWidget>

class QTextEdit;
class QComboBox;

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
    void clear();

private:
    void setupUi();

    QTextEdit* m_textEdit{};
    QComboBox* m_levelFilter{};

    static ConsolePanel* s_instance;
};
