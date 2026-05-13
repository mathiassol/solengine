#include "console_panel.h"

#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFont>
#include <QScrollBar>

// ---------------------------------------------------------------------------
ConsolePanel* ConsolePanel::s_instance = nullptr;

// Qt message handler — captureless lambda, safe to use as function pointer
static QtMessageHandler s_previousHandler = nullptr;

static void solMessageHandler(QtMsgType type,
                               const QMessageLogContext& ctx,
                               const QString& msg)
{
    // Forward to previous handler (default Qt stderr output) first
    if (s_previousHandler)
        s_previousHandler(type, ctx, msg);

    if (ConsolePanel* panel = ConsolePanel::instance())
        panel->appendLog(type, msg);
}

// ---------------------------------------------------------------------------
ConsolePanel::ConsolePanel(QWidget* parent)
    : QWidget(parent)
{
    s_instance = this;
    setupUi();

    // Install message handler after the UI is ready
    s_previousHandler = qInstallMessageHandler(solMessageHandler);
    qInfo("Console ready.");
}

ConsolePanel::~ConsolePanel()
{
    if (s_instance == this) {
        qInstallMessageHandler(s_previousHandler); // restore previous handler
        s_previousHandler = nullptr;
        s_instance = nullptr;
    }
}

ConsolePanel* ConsolePanel::instance()
{
    return s_instance;
}

// ---------------------------------------------------------------------------
void ConsolePanel::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Toolbar row
    auto* toolbar = new QWidget(this);
    auto* tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(4, 2, 4, 2);
    tbLayout->setSpacing(6);

    auto* clearBtn = new QPushButton("Clear", toolbar);
    clearBtn->setFixedHeight(28);
    connect(clearBtn, &QPushButton::clicked, this, &ConsolePanel::clear);

    auto* levelLabel = new QLabel("Level:", toolbar);

    m_levelFilter = new QComboBox(toolbar);
    m_levelFilter->addItems({"All", "Info", "Warn", "Error"});
    m_levelFilter->setFixedHeight(28);

    tbLayout->addWidget(clearBtn);
    tbLayout->addWidget(levelLabel);
    tbLayout->addWidget(m_levelFilter);
    tbLayout->addStretch();

    // Log output
    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    QFont monoFont("Cascadia Code", 9);
    monoFont.setFixedPitch(true);
    m_textEdit->setFont(monoFont);

    layout->addWidget(toolbar);
    layout->addWidget(m_textEdit);
}

// ---------------------------------------------------------------------------
void ConsolePanel::appendLog(QtMsgType type, const QString& msg)
{
    // Apply level filter
    const int filterIdx = m_levelFilter->currentIndex();
    if (filterIdx == 1 && (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg))
        return;
    if (filterIdx == 2 && type != QtWarningMsg)
        return;
    if (filterIdx == 3 && type != QtCriticalMsg && type != QtFatalMsg)
        return;

    // Pick colour by severity
    const char* color = "#cdd6f4";
    if (type == QtDebugMsg)
        color = "#6c7086";
    else if (type == QtWarningMsg)
        color = "#f9e2af";
    else if (type == QtCriticalMsg || type == QtFatalMsg)
        color = "#f38ba8";

    // Append as HTML so colour works; escape the message to prevent injection
    m_textEdit->append(
        QString("<font color='%1'>%2</font>")
            .arg(color, msg.toHtmlEscaped())
    );

    // Auto-scroll to bottom
    QScrollBar* sb = m_textEdit->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void ConsolePanel::clear()
{
    m_textEdit->clear();
}
