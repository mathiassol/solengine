#pragma once

#include <QAbstractButton>

class QPropertyAnimation;

class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal thumbOffset READ thumbOffset WRITE setThumbOffset)

public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    qreal thumbOffset() const;
    void setThumbOffset(qreal offset);

protected:
    void paintEvent(QPaintEvent* event) override;
    void nextCheckState() override;

private slots:
    void animateToState(bool checked);

private:
    qreal m_thumbOffset{0.0};
    QPropertyAnimation* m_animation{};
};
