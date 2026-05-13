#include "widgets/toggle_switch.h"

#include <QPainter>
#include <QPropertyAnimation>
#include <QEasingCurve>

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(40, 22);
    setMaximumHeight(22);

    m_animation = new QPropertyAnimation(this, "thumbOffset", this);
    m_animation->setDuration(120);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);

    connect(this, &QAbstractButton::toggled, this, &ToggleSwitch::animateToState);
}

QSize ToggleSwitch::sizeHint() const
{
    return QSize(40, 22);
}

QSize ToggleSwitch::minimumSizeHint() const
{
    return QSize(40, 22);
}

qreal ToggleSwitch::thumbOffset() const
{
    return m_thumbOffset;
}

void ToggleSwitch::setThumbOffset(qreal offset)
{
    m_thumbOffset = offset;
    update();
}

void ToggleSwitch::nextCheckState()
{
    setChecked(!isChecked());
}

void ToggleSwitch::animateToState(bool checked)
{
    m_animation->stop();
    m_animation->setStartValue(m_thumbOffset);
    m_animation->setEndValue(checked ? 18.0 : 0.0);
    m_animation->start();
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF track(0.0, 0.0, 40.0, 22.0);
    const QColor trackColor = isChecked() ? QColor("#89b4fa") : QColor("#45475a");
    const QColor thumbColor = isChecked() ? QColor("#ffffff") : QColor("#a6adc8");

    p.setPen(Qt::NoPen);
    p.setBrush(trackColor);
    p.drawRoundedRect(track, 11.0, 11.0);

    p.setBrush(thumbColor);
    p.drawEllipse(QRectF(2.0 + m_thumbOffset, 2.0, 18.0, 18.0));
}
