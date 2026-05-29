#include "console_panel.h"

#include <QApplication>
#include <QClipboard>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFont>
#include <QScrollBar>
#include <QMetaObject>

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

static sol::log::Level normalizedLevel(bool engine, int level)
{
    if (engine) {
        if (level == static_cast<int>(sol::log::Level::Error)) return sol::log::Level::Error;
        if (level == static_cast<int>(sol::log::Level::Warn)) return sol::log::Level::Warn;
        return sol::log::Level::Info;
    }

    if (level == QtCriticalMsg || level == QtFatalMsg) return sol::log::Level::Error;
    if (level == QtWarningMsg) return sol::log::Level::Warn;
    return sol::log::Level::Info;
}

static bool passesFilter(int filter, bool engine, int level)
{
    const sol::log::Level normalized = normalizedLevel(engine, level);
    if (filter == 1) return normalized == sol::log::Level::Info;
    if (filter == 2) return normalized == sol::log::Level::Warn;
    if (filter == 3) return normalized == sol::log::Level::Error;
    return true;
}

static QString colorFor(bool engine, int level)
{
    switch (normalizedLevel(engine, level)) {
    case sol::log::Level::Error: return "#f38ba8";
    case sol::log::Level::Warn:  return "#f9e2af";
    case sol::log::Level::Info:  return "#a6e3a1";
    }
    return "#cdd6f4";
}

static QString prefixFor(bool engine, int level)
{
    if (!engine) return {};
    if (level == static_cast<int>(sol::log::Level::Error)) return "[error] ";
    if (level == static_cast<int>(sol::log::Level::Warn)) return "[warn]  ";
    return "[info]  ";
}

// ---------------------------------------------------------------------------
ConsolePanel::ConsolePanel(QWidget* parent)
    : QWidget(parent)
{
    s_instance = this;
    setupUi();

    // Install Qt message handler
    s_previousHandler = qInstallMessageHandler(solMessageHandler);

    // Install engine log sink — engine logs route here via queued invoke
    // (engine may log from any thread, Qt UI must be touched on main thread)
    sol::log::set_sink([](sol::log::Level level, std::string_view msg) {
        if (ConsolePanel* panel = ConsolePanel::instance()) {
            QString qmsg = QString::fromUtf8(msg.data(), static_cast<int>(msg.size()));
            QMetaObject::invokeMethod(panel, [panel, level, qmsg]() {
                panel->appendEngineLog(level, qmsg);
            }, Qt::QueuedConnection);
        }
    });

    qInfo("Console ready.");
}

ConsolePanel::~ConsolePanel()
{
    if (s_instance == this) {
        sol::log::set_sink({});                      // clear engine log sink
        qInstallMessageHandler(s_previousHandler);   // restore previous Qt handler
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

    auto* filterLabel = new QLabel("Filter:", toolbar);
    m_level_filter = new QComboBox(toolbar);
    m_level_filter->addItems({"All", "Info", "Warning", "Error"});
    m_level_filter->setFixedHeight(28);
    m_level_filter->setMaximumWidth(100);

    auto* clearBtn = new QPushButton("Clear", toolbar);
    clearBtn->setFixedHeight(28);
    clearBtn->setMaximumWidth(60);
    connect(clearBtn, &QPushButton::clicked, this, &ConsolePanel::clear);

    auto* copyBtn = new QPushButton("Copy", toolbar);
    copyBtn->setFixedHeight(28);
    copyBtn->setMaximumWidth(60);
    connect(copyBtn, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_text->toPlainText());
    });

    connect(m_level_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConsolePanel::rebuildDisplay);

    tbLayout->addWidget(filterLabel);
    tbLayout->addWidget(m_level_filter);
    tbLayout->addWidget(clearBtn);
    tbLayout->addWidget(copyBtn);
    tbLayout->addStretch();

    // Log output
    m_text = new QTextEdit(this);
    m_text->setReadOnly(true);
    QFont monoFont("Cascadia Code", 9);
    monoFont.setFixedPitch(true);
    m_text->setFont(monoFont);

    layout->addWidget(toolbar);
    layout->addWidget(m_text);
}

// ---------------------------------------------------------------------------
void ConsolePanel::appendLog(QtMsgType type, const QString& msg)
{
    m_entries.push_back({false, static_cast<int>(type), msg});
    if (passesFilter(m_level_filter->currentIndex(), false, static_cast<int>(type))) {
        m_text->append(QString("<font color='%1'>%2</font>")
            .arg(colorFor(false, static_cast<int>(type)), msg.toHtmlEscaped()));
        m_text->verticalScrollBar()->setValue(m_text->verticalScrollBar()->maximum());
    }
}

// ---------------------------------------------------------------------------
void ConsolePanel::appendEngineLog(sol::log::Level level, const QString& msg)
{
    const int value = static_cast<int>(level);
    m_entries.push_back({true, value, msg});
    if (passesFilter(m_level_filter->currentIndex(), true, value)) {
        m_text->append(QString("<font color='%1'>%2%3</font>")
            .arg(colorFor(true, value), prefixFor(true, value), msg.toHtmlEscaped()));
        m_text->verticalScrollBar()->setValue(m_text->verticalScrollBar()->maximum());
    }
}

void ConsolePanel::clear()
{
    m_entries.clear();
    m_text->clear();
}

void ConsolePanel::rebuildDisplay()
{
    m_text->clear();
    const int filter = m_level_filter->currentIndex();
    for (const auto& entry : m_entries) {
        if (!passesFilter(filter, entry.engine, entry.level)) continue;
        m_text->append(QString("<font color='%1'>%2%3</font>")
            .arg(colorFor(entry.engine, entry.level),
                 prefixFor(entry.engine, entry.level),
                 entry.text.toHtmlEscaped()));
    }
}
