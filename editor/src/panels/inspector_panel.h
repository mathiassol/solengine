#pragma once
#include <QWidget>
#include "sol/host.h"
#include "sol/scene/node.h"

#include <vector>
#include <string>

class QScrollArea;
class QFormLayout;

namespace sol { struct FieldDesc; }

class InspectorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit InspectorPanel(sol::EngineHost* host, QWidget* parent = nullptr);

    void clear();
    void showNode(sol::Node* node);
    void refresh();

private:
    struct FieldEntry {
        const sol::FieldDesc* desc;
        QWidget*              widget;
    };

    void buildFields(sol::Node* node);

    sol::EngineHost*      m_host{};
    sol::Node*            m_current_node{};
    QScrollArea*          m_scroll_area{};
    QWidget*              m_content{};
    QFormLayout*          m_form{};
    std::vector<FieldEntry> m_field_entries;
};
