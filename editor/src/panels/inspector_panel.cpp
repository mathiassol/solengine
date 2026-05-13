#include "inspector_panel.h"
#include "sol/reflect.h"
#include "widgets/asset_path_field.h"
#include "widgets/number_field.h"
#include "widgets/section_header.h"
#include "widgets/toggle_switch.h"
#include "widgets/vec3_field.h"

#include <glm/glm.hpp>

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
QLabel* makeFieldLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("fieldLabel", true);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
}

QFormLayout* makeFormLayout(QWidget* parent)
{
    auto* form = new QFormLayout(parent);
    form->setContentsMargins(8, 8, 8, 12);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    // Fixed label column prevents it from hogging space; widget column gets the rest.
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    return form;
}
}

// ---------------------------------------------------------------------------
InspectorPanel::InspectorPanel(sol::EngineHost* host, QWidget* parent)
    : QWidget(parent), m_host(host)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_scroll_area = new QScrollArea(this);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setFrameShape(QFrame::NoFrame);

    m_content = new QWidget(m_scroll_area);
    m_form = makeFormLayout(m_content);

    m_scroll_area->setWidget(m_content);
    outer->addWidget(m_scroll_area);

    showNode(nullptr);
}

// ---------------------------------------------------------------------------
void InspectorPanel::clear()
{
    while (m_form->rowCount() > 0)
        m_form->removeRow(0);
    m_field_entries.clear();
}

// ---------------------------------------------------------------------------
void InspectorPanel::showNode(sol::Node* node)
{
    m_current_node = node;
    clear();

    if (!node) {
        auto* lbl = new QLabel("No node selected", m_content);
        lbl->setProperty("muted", true);
        lbl->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        m_form->addRow(lbl);
        return;
    }

    buildFields(node);
}

// ---------------------------------------------------------------------------
void InspectorPanel::buildFields(sol::Node* node)
{
    auto* nameEdit = new QLineEdit(QString::fromStdString(node->name), m_content);
    nameEdit->setReadOnly(true);
    nameEdit->setMinimumHeight(28);
    m_form->addRow(makeFieldLabel("Entity", m_content), nameEdit);

    auto* separator = new QFrame(m_content);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    m_form->addRow(separator);

    const sol::TypeDesc* desc = sol::ComponentRegistry::instance().find(node->type_name());
    if (!desc)
        return;

    auto* header = new SectionHeader(QString(node->type_name()), m_content);
    auto* body = new QWidget(m_content);
    auto* bodyForm = makeFormLayout(body);
    bodyForm->setContentsMargins(8, 4, 0, 8);

    m_form->addRow(header);
    m_form->addRow(body);

    connect(header, &SectionHeader::toggled, body, [body](bool collapsed) {
        body->setVisible(!collapsed);
    });

    for (const auto& field : desc->fields) {
        const std::string fname = field.name;
        const QString label = QString(field.name);
        QWidget* w = nullptr;

        switch (field.type) {

        case sol::FieldType::Float: {
            auto* sb = new NumberField(QString(), *static_cast<float*>(field.ptr(node)), body);
            connect(sb, QOverload<double>::of(&NumberField::valueChanged),
                    this, [this, fname](double v) {
                if (!m_host || !m_current_node) return;
                float fv = static_cast<float>(v);
                m_host->set_field(m_current_node, fname, &fv);
            });
            w = sb;
            break;
        }

        case sol::FieldType::Bool: {
            auto* toggle = new ToggleSwitch(body);
            toggle->setChecked(*static_cast<bool*>(field.ptr(node)));
            connect(toggle, &ToggleSwitch::toggled, this, [this, fname](bool v) {
                if (!m_host || !m_current_node) return;
                m_host->set_field(m_current_node, fname, &v);
            });
            w = toggle;
            break;
        }

        case sol::FieldType::Int: {
            auto* sb = new IntField(QString(), *static_cast<int*>(field.ptr(node)), body);
            connect(sb, QOverload<int>::of(&IntField::valueChanged),
                    this, [this, fname](int v) {
                if (!m_host || !m_current_node) return;
                m_host->set_field(m_current_node, fname, &v);
            });
            w = sb;
            break;
        }

        case sol::FieldType::EnumInt: {
            auto* cb = new QComboBox(body);
            cb->setMinimumHeight(28);
            for (auto* lbl : field.enum_labels)
                cb->addItem(QString(lbl));
            cb->setCurrentIndex(*static_cast<int*>(field.ptr(node)));
            connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this, fname](int v) {
                if (!m_host || !m_current_node) return;
                m_host->set_field(m_current_node, fname, &v);
            });
            w = cb;
            break;
        }

        case sol::FieldType::Vec3:
        case sol::FieldType::Color3: {
            auto* vec = new Vec3Field(body);
            vec->setValue(*static_cast<glm::vec3*>(field.ptr(node)));
            connect(vec, &Vec3Field::valueChanged, this, [this, fname](const glm::vec3& v) {
                if (!m_host || !m_current_node) return;
                glm::vec3 value = v;
                m_host->set_field(m_current_node, fname, &value);
            });
            w = vec;
            break;
        }

        case sol::FieldType::Vec4:
        case sol::FieldType::Color4: {
            auto* vec = new Vec4Field(body);
            vec->setValue(*static_cast<glm::vec4*>(field.ptr(node)));
            connect(vec, &Vec4Field::valueChanged, this, [this, fname](const glm::vec4& v) {
                if (!m_host || !m_current_node) return;
                glm::vec4 value = v;
                m_host->set_field(m_current_node, fname, &value);
            });
            w = vec;
            break;
        }

        case sol::FieldType::String: {
            auto* le = new QLineEdit(body);
            le->setMinimumHeight(28);
            le->setText(QString::fromStdString(*static_cast<std::string*>(field.ptr(node))));
            connect(le, &QLineEdit::editingFinished, this, [this, fname, le] {
                if (!m_host || !m_current_node) return;
                std::string v = le->text().toStdString();
                m_host->set_field(m_current_node, fname, &v);
            });
            w = le;
            break;
        }

        case sol::FieldType::AssetPath: {
            auto* path = new AssetPathField(body);
            path->setPathSilent(QString::fromStdString(*static_cast<std::string*>(field.ptr(node))));
            connect(path, &AssetPathField::pathChanged, this, [this, fname](const QString& text) {
                if (!m_host || !m_current_node) return;
                std::string v = text.toStdString();
                m_host->set_field(m_current_node, fname, &v);
            });
            w = path;
            break;
        }

        } // switch

        if (w) {
            bodyForm->addRow(makeFieldLabel(label, body), w);
            m_field_entries.push_back({ &field, w });
        }
    }
}

// ---------------------------------------------------------------------------
// Returns true if `w` or any of its children currently has keyboard focus.
static bool widgetHasFocus(QWidget* w)
{
    QWidget* fw = QApplication::focusWidget();
    if (!fw) return false;
    while (fw) {
        if (fw == w) return true;
        fw = fw->parentWidget();
    }
    return false;
}

void InspectorPanel::refresh()
{
    if (!m_current_node) return;

    for (auto& entry : m_field_entries) {
        // Never stomp a widget the user is actively editing.
        if (widgetHasFocus(entry.widget)) continue;

        const sol::FieldDesc* d = entry.desc;
        void* ptr = d->ptr(m_current_node);

        switch (d->type) {

        case sol::FieldType::Float: {
            auto* sb = static_cast<NumberField*>(entry.widget);
            const float v = *static_cast<float*>(ptr);
            if (qAbs(sb->value() - v) > 1e-6f)
                sb->setValueSilent(v);
            break;
        }
        case sol::FieldType::Bool: {
            auto* toggle = static_cast<ToggleSwitch*>(entry.widget);
            const bool checked = *static_cast<bool*>(ptr);
            // Only update if value actually changed — avoids thumb animation flash.
            if (toggle->isChecked() != checked) {
                QSignalBlocker blk(toggle);
                toggle->setChecked(checked);
                // Snap thumb to final position (no animation on external refresh).
                toggle->setThumbOffset(checked ? 18.0 : 0.0);
            }
            break;
        }
        case sol::FieldType::Int: {
            auto* sb = static_cast<IntField*>(entry.widget);
            const int v = *static_cast<int*>(ptr);
            if (sb->value() != v)
                sb->setValueSilent(v);
            break;
        }
        case sol::FieldType::EnumInt: {
            auto* cb = static_cast<QComboBox*>(entry.widget);
            const int v = *static_cast<int*>(ptr);
            if (cb->currentIndex() != v) {
                QSignalBlocker blk(cb);
                cb->setCurrentIndex(v);
            }
            break;
        }
        case sol::FieldType::Vec3:
        case sol::FieldType::Color3: {
            auto* vec = static_cast<Vec3Field*>(entry.widget);
            const glm::vec3 v = *static_cast<glm::vec3*>(ptr);
            const glm::vec3 cur = vec->value();
            if (glm::any(glm::notEqual(cur, v)))
                vec->setValue(v);
            break;
        }
        case sol::FieldType::Vec4:
        case sol::FieldType::Color4: {
            auto* vec = static_cast<Vec4Field*>(entry.widget);
            const glm::vec4 v = *static_cast<glm::vec4*>(ptr);
            const glm::vec4 cur = vec->value();
            if (glm::any(glm::notEqual(cur, v)))
                vec->setValue(v);
            break;
        }
        case sol::FieldType::String: {
            auto* le = static_cast<QLineEdit*>(entry.widget);
            const QString v = QString::fromStdString(*static_cast<std::string*>(ptr));
            if (le->text() != v) {
                QSignalBlocker blk(le);
                le->setText(v);
            }
            break;
        }
        case sol::FieldType::AssetPath: {
            auto* path = static_cast<AssetPathField*>(entry.widget);
            const QString v = QString::fromStdString(*static_cast<std::string*>(ptr));
            if (path->path() != v)
                path->setPathSilent(v);
            break;
        }

        } // switch
    }
}
