#include "widgets/section_header.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>

SectionHeader::SectionHeader(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(32);
    setCursor(Qt::PointingHandCursor);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->setSpacing(6);

    m_chevron = new QLabel(this);
    m_chevron->setFixedWidth(14);
    m_chevron->setAlignment(Qt::AlignCenter);
    m_chevron->setProperty("sectionTitle", true);

    m_title = new QLabel(title, this);
    m_title->setProperty("sectionTitle", true);

    layout->addWidget(m_chevron);
    layout->addWidget(m_title);
    layout->addStretch();
    updateChevron();
}

bool SectionHeader::isCollapsed() const
{
    return m_collapsed;
}

void SectionHeader::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;
    updateChevron();
    emit toggled(m_collapsed);
}

void SectionHeader::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        setCollapsed(!m_collapsed);
    QWidget::mousePressEvent(event);
}

void SectionHeader::updateChevron()
{
    m_chevron->setText(m_collapsed ? QStringLiteral("▶") : QStringLiteral("▼"));
}
