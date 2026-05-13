#include "sol/reflect.h"

namespace sol {

ComponentRegistry& ComponentRegistry::instance() {
    static ComponentRegistry s_instance;
    return s_instance;
}

void ComponentRegistry::register_type(TypeDesc desc) {
    // Replace if already registered (allows hot-reload in the future).
    for (auto& t : m_types) {
        if (std::string_view(t.type_name) == std::string_view(desc.type_name)) {
            t = std::move(desc);
            return;
        }
    }
    m_types.push_back(std::move(desc));
}

const TypeDesc* ComponentRegistry::find(std::string_view type_name) const {
    for (const auto& t : m_types)
        if (std::string_view(t.type_name) == type_name)
            return &t;
    return nullptr;
}

} // namespace sol
