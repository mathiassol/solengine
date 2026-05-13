#include "widgets/number_field.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QStyle>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <type_traits>

namespace {

static void repolish(QWidget* widget)
{
    if (!widget) return;
    if (auto* style = widget->style()) {
        style->unpolish(widget);
        style->polish(widget);
    }
    widget->update();
}

template <typename T>
class DragSpinBoxImpl : public QObject {
public:
    DragSpinBoxImpl(QWidget* owner, const QString& label, T initial, std::function<void(T)> emit_changed)
        : QObject(owner)
        , m_owner(owner)
        , m_emit_changed(std::move(emit_changed))
        , m_label(label)
    {
        m_line_edit = new QLineEdit(owner);
        m_line_edit->setFrame(false);
        m_line_edit->setReadOnly(true);
        m_line_edit->setMouseTracking(false);
        m_line_edit->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_line_edit->setCursor(Qt::SizeHorCursor);
        m_line_edit->installEventFilter(this);

        auto* layout = new QHBoxLayout(owner);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(m_line_edit);

        m_owner->setFocusPolicy(Qt::StrongFocus);
        m_owner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_owner->setMinimumHeight(28);
        m_owner->setMinimumWidth(60);
        m_owner->setCursor(Qt::SizeHorCursor);
        m_owner->setProperty("editing", false);
        if (!m_label.isEmpty())
            m_owner->setToolTip(m_label);

        connect(m_line_edit, &QLineEdit::editingFinished, m_owner, [this] { commitEdit(); });
        connect(m_line_edit, &QLineEdit::returnPressed, m_owner, [this] { commitEdit(); });

        setValueSilent(initial);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_line_edit && m_editing && event->type() == QEvent::KeyPress) {
            auto* key_event = static_cast<QKeyEvent*>(event);
            if (key_event->key() == Qt::Key_Escape) {
                cancelEdit();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

    void setAxisColor(const QString& axis)
    {
        const QString normalized = axis.trimmed().toLower();
        if (normalized == QStringLiteral("x") || normalized == QStringLiteral("y")
            || normalized == QStringLiteral("z") || normalized == QStringLiteral("w")) {
            m_owner->setProperty("axis", normalized);
        } else {
            m_owner->setProperty("axis", QVariant());
        }
        repolish(m_owner);
    }

    void setValueSilent(T value)
    {
        m_value = clampValue(value);
        if (!m_editing)
            updateDisplay();
    }

    T value() const
    {
        return m_value;
    }

    void setStep(double step)
    {
        m_step = step;
    }

    void setRange(double min_value, double max_value)
    {
        if (min_value > max_value)
            std::swap(min_value, max_value);
        m_min = min_value;
        m_max = max_value;
        setValueSilent(m_value);
    }

    void mousePressEvent(QMouseEvent* event)
    {
        if (event->button() != Qt::LeftButton || m_editing) {
            event->ignore();
            return;
        }

        m_pressed = true;
        m_dragging = false;
        m_press_x = event->position().x();
        m_start_value = m_value;
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event)
    {
        if (!m_pressed || m_editing || !(event->buttons() & Qt::LeftButton)) {
            event->ignore();
            return;
        }

        const double dx = event->position().x() - m_press_x;
        if (!m_dragging && std::abs(dx) > 3.0)
            m_dragging = true;

        if (m_dragging) {
            const double raw_value = static_cast<double>(m_start_value) + dx * m_step;
            m_value = clampValue(castValue(raw_value));
            updateDisplay();
        }

        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event)
    {
        if (event->button() != Qt::LeftButton || m_editing) {
            event->ignore();
            return;
        }

        const bool was_pressed = m_pressed;
        const bool was_dragging = m_dragging;
        m_pressed = false;
        m_dragging = false;

        if (!was_pressed) {
            event->ignore();
            return;
        }

        if (was_dragging) {
            emitChanged();
        } else {
            beginEdit();
        }

        event->accept();
    }

    void keyPressEvent(QKeyEvent* event)
    {
        if (m_editing && event->key() == Qt::Key_Escape) {
            cancelEdit();
            event->accept();
            return;
        }
        event->ignore();
    }

private:
    static double defaultMin()
    {
        if constexpr (std::is_integral_v<T>)
            return -1000000000.0;
        return -1e9;
    }

    static double defaultMax()
    {
        if constexpr (std::is_integral_v<T>)
            return 1000000000.0;
        return 1e9;
    }

    static double defaultStep()
    {
        if constexpr (std::is_integral_v<T>)
            return 1.0;
        return 0.1;
    }

    T clampValue(T value) const
    {
        const double clamped = std::clamp(static_cast<double>(value), m_min, m_max);
        return castValue(clamped);
    }

    static T castValue(double value)
    {
        if constexpr (std::is_integral_v<T>)
            return static_cast<T>(std::llround(value));
        return static_cast<T>(value);
    }

    QString formatValue(T value) const
    {
        if constexpr (std::is_integral_v<T>)
            return QString::number(value);
        return QString::number(static_cast<double>(value), 'f', 3);
    }

    bool parseValue(const QString& text, T& out_value) const
    {
        bool ok = false;
        if constexpr (std::is_integral_v<T>) {
            const int parsed = text.toInt(&ok);
            if (ok)
                out_value = clampValue(static_cast<T>(parsed));
        } else {
            const double parsed = text.toDouble(&ok);
            if (ok)
                out_value = clampValue(static_cast<T>(parsed));
        }
        return ok;
    }

    void updateDisplay()
    {
        const QSignalBlocker blocker(m_line_edit);
        m_line_edit->setText(formatValue(m_value));
    }

    void beginEdit()
    {
        if (m_editing)
            return;

        m_editing = true;
        m_line_edit->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        m_line_edit->setReadOnly(false);
        m_line_edit->setMouseTracking(true);
        m_line_edit->setCursor(Qt::IBeamCursor);
        m_owner->setCursor(Qt::IBeamCursor);
        m_owner->setProperty("editing", true);
        repolish(m_owner);
        m_line_edit->setFocus();
        m_line_edit->selectAll();
    }

    void exitEditMode()
    {
        m_editing = false;
        m_line_edit->setReadOnly(true);
        m_line_edit->setMouseTracking(false);
        m_line_edit->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_line_edit->setCursor(Qt::SizeHorCursor);
        m_owner->setCursor(Qt::SizeHorCursor);
        m_owner->setProperty("editing", false);
        repolish(m_owner);
    }

    void commitEdit()
    {
        if (!m_editing)
            return;

        T parsed_value{};
        const bool valid = parseValue(m_line_edit->text().trimmed(), parsed_value);
        if (valid)
            m_value = parsed_value;
        updateDisplay();
        exitEditMode();
        if (valid)
            emitChanged();
    }

    void cancelEdit()
    {
        if (!m_editing)
            return;

        updateDisplay();
        exitEditMode();
    }

    void emitChanged()
    {
        if (m_emit_changed)
            m_emit_changed(m_value);
    }

    QWidget* m_owner = nullptr;
    QLineEdit* m_line_edit = nullptr;
    std::function<void(T)> m_emit_changed;
    QString m_label;
    T m_value {};
    T m_start_value {};
    double m_min = defaultMin();
    double m_max = defaultMax();
    double m_step = defaultStep();
    double m_press_x = 0.0;
    bool m_pressed = false;
    bool m_dragging = false;
    bool m_editing = false;
};

} // namespace

class NumberField::Impl final : public DragSpinBoxImpl<double> {
public:
    Impl(NumberField* owner, const QString& label, double initial)
        : DragSpinBoxImpl<double>(owner, label, initial, [owner](double value) { emit owner->valueChanged(value); })
    {
    }
};

class IntField::Impl final : public DragSpinBoxImpl<int> {
public:
    Impl(IntField* owner, const QString& label, int initial)
        : DragSpinBoxImpl<int>(owner, label, initial, [owner](int value) { emit owner->valueChanged(value); })
    {
    }
};

NumberField::NumberField(const QString& label, double initial, QWidget* parent)
    : QWidget(parent)
    , m_impl(new Impl(this, label, initial))
{
}

void NumberField::setAxisColor(const QString& axis)
{
    m_impl->setAxisColor(axis);
}

void NumberField::setValueSilent(double v)
{
    m_impl->setValueSilent(v);
}

double NumberField::value() const
{
    return m_impl->value();
}

void NumberField::setStep(double step)
{
    m_impl->setStep(step);
}

void NumberField::setRange(double min, double max)
{
    m_impl->setRange(min, max);
}

void NumberField::mousePressEvent(QMouseEvent* event)
{
    m_impl->mousePressEvent(event);
}

void NumberField::mouseMoveEvent(QMouseEvent* event)
{
    m_impl->mouseMoveEvent(event);
}

void NumberField::mouseReleaseEvent(QMouseEvent* event)
{
    m_impl->mouseReleaseEvent(event);
}

void NumberField::keyPressEvent(QKeyEvent* event)
{
    m_impl->keyPressEvent(event);
}

IntField::IntField(const QString& label, int initial, QWidget* parent)
    : QWidget(parent)
    , m_impl(new Impl(this, label, initial))
{
}

void IntField::setAxisColor(const QString& axis)
{
    m_impl->setAxisColor(axis);
}

void IntField::setValueSilent(int v)
{
    m_impl->setValueSilent(v);
}

int IntField::value() const
{
    return m_impl->value();
}

void IntField::mousePressEvent(QMouseEvent* event)
{
    m_impl->mousePressEvent(event);
}

void IntField::mouseMoveEvent(QMouseEvent* event)
{
    m_impl->mouseMoveEvent(event);
}

void IntField::mouseReleaseEvent(QMouseEvent* event)
{
    m_impl->mouseReleaseEvent(event);
}

void IntField::keyPressEvent(QKeyEvent* event)
{
    m_impl->keyPressEvent(event);
}
