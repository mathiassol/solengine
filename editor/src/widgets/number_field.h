#pragma once

#include <QWidget>
#include <QString>

class QKeyEvent;
class QMouseEvent;

class NumberField : public QWidget
{
    Q_OBJECT

public:
    explicit NumberField(const QString& label = QString(), double initial = 0.0, QWidget* parent = nullptr);

    void setAxisColor(const QString& axis);
    void setValueSilent(double v);
    double value() const;
    void setStep(double step);
    void setRange(double min, double max);

signals:
    void valueChanged(double v);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    class Impl;
    Impl* m_impl;
};

class IntField : public QWidget
{
    Q_OBJECT

public:
    explicit IntField(const QString& label = QString(), int initial = 0, QWidget* parent = nullptr);

    void setAxisColor(const QString& axis);
    void setValueSilent(int v);
    int value() const;

signals:
    void valueChanged(int v);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    class Impl;
    Impl* m_impl;
};
