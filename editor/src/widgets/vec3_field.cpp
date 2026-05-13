#include "widgets/vec3_field.h"
#include "widgets/number_field.h"

#include <QHBoxLayout>
#include <QLabel>

static QLabel* makeAxisLabel(const QString& text, const QString& color)
{
    auto* lbl = new QLabel(text);
    lbl->setFixedWidth(14);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet(QString("color: %1; font-weight: 700; font-size: 11px;").arg(color));
    return lbl;
}

Vec3Field::Vec3Field(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_x = new NumberField(QString(), 0.0, this);
    m_y = new NumberField(QString(), 0.0, this);
    m_z = new NumberField(QString(), 0.0, this);

    // No per-spinbox axis color property needed — we use external labels.
    m_x->setMinimumWidth(50);
    m_y->setMinimumWidth(50);
    m_z->setMinimumWidth(50);
    m_x->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_y->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_z->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    layout->addWidget(makeAxisLabel("X", "#f38ba8"));
    layout->addWidget(m_x);
    layout->addSpacing(4);
    layout->addWidget(makeAxisLabel("Y", "#a6e3a1"));
    layout->addWidget(m_y);
    layout->addSpacing(4);
    layout->addWidget(makeAxisLabel("Z", "#89dceb"));
    layout->addWidget(m_z);

    connect(m_x, QOverload<double>::of(&NumberField::valueChanged), this, [this](double) { emit valueChanged(value()); });
    connect(m_y, QOverload<double>::of(&NumberField::valueChanged), this, [this](double) { emit valueChanged(value()); });
    connect(m_z, QOverload<double>::of(&NumberField::valueChanged), this, [this](double) { emit valueChanged(value()); });
}

void Vec3Field::setValue(const glm::vec3& v)
{
    m_x->setValueSilent(v.x);
    m_y->setValueSilent(v.y);
    m_z->setValueSilent(v.z);
}

glm::vec3 Vec3Field::value() const
{
    return glm::vec3(static_cast<float>(m_x->value()),
                     static_cast<float>(m_y->value()),
                     static_cast<float>(m_z->value()));
}

Vec4Field::Vec4Field(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_x = new NumberField(QString(), 0.0, this);
    m_y = new NumberField(QString(), 0.0, this);
    m_z = new NumberField(QString(), 0.0, this);
    m_w = new NumberField(QString(), 0.0, this);

    for (auto* f : {m_x, m_y, m_z, m_w}) {
        f->setMinimumWidth(44);
        f->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    layout->addWidget(makeAxisLabel("X", "#f38ba8"));
    layout->addWidget(m_x);
    layout->addSpacing(2);
    layout->addWidget(makeAxisLabel("Y", "#a6e3a1"));
    layout->addWidget(m_y);
    layout->addSpacing(2);
    layout->addWidget(makeAxisLabel("Z", "#89dceb"));
    layout->addWidget(m_z);
    layout->addSpacing(2);
    layout->addWidget(makeAxisLabel("W", "#cba6f7"));
    layout->addWidget(m_w);

    connect(m_x, QOverload<double>::of(&NumberField::valueChanged), this, [this](double) { emit valueChanged(value()); });
    connect(m_y, QOverload<double>::of(&NumberField::valueChanged), this, [this](double) { emit valueChanged(value()); });
    connect(m_z, QOverload<double>::of(&NumberField::valueChanged), this, [this](double) { emit valueChanged(value()); });
    connect(m_w, QOverload<double>::of(&NumberField::valueChanged), this, [this](double) { emit valueChanged(value()); });
}

void Vec4Field::setValue(const glm::vec4& v)
{
    m_x->setValueSilent(v.x);
    m_y->setValueSilent(v.y);
    m_z->setValueSilent(v.z);
    m_w->setValueSilent(v.w);
}

glm::vec4 Vec4Field::value() const
{
    return glm::vec4(static_cast<float>(m_x->value()),
                     static_cast<float>(m_y->value()),
                     static_cast<float>(m_z->value()),
                     static_cast<float>(m_w->value()));
}
