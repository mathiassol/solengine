#pragma once

#include <QWidget>

class QLabel;

class SectionHeader : public QWidget
{
    Q_OBJECT

public:
    explicit SectionHeader(const QString& title, QWidget* parent = nullptr);

    bool isCollapsed() const;
    void setCollapsed(bool collapsed);

signals:
    void toggled(bool collapsed);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void updateChevron();

    QLabel* m_chevron{};
    QLabel* m_title{};
    bool m_collapsed{false};
};
