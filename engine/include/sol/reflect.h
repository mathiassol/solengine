#pragma once
#include "sol/export.h"
#include <string>
#include <vector>
#include <functional>
#include <string_view>

// ---------------------------------------------------------------------------
// SolEngine reflection — lightweight field metadata for editor + serializer.
//
// Usage (in any .cpp, at namespace scope):
//
//   SOL_REFLECT_BEGIN(MyNode)
//     SOL_FIELD(some_float, sol::FieldType::Float)
//     SOL_FIELD(some_vec3,  sol::FieldType::Vec3)
//     SOL_FIELD_ENUM(my_enum, "Option A", "Option B", "Option C")
//   SOL_REFLECT_END()
// ---------------------------------------------------------------------------

namespace sol {

// What kind of data a field contains.
enum class FieldType {
    Bool,
    Int,
    Float,
    Vec3,
    Vec4,
    Color3,    // vec3 displayed as a colour picker
    Color4,    // vec4 displayed as a colour picker
    String,
    AssetPath, // string path — shows file-browse button in editor
    EnumInt,   // int with named values (see enum_labels)
};

// Description of one serialisable / editable field on a node type.
struct SOL_API FieldDesc {
    const char* name;     // JSON key and inspector label
    FieldType   type;
    // Returns a raw pointer to the field inside the given node instance.
    // Caller casts the pointer according to `type` before reading or writing.
    std::function<void*(void*)> ptr;
    // For EnumInt: names corresponding to integer values 0, 1, 2, ...
    std::vector<const char*> enum_labels;
};

// Description of a node type — mirrors Node::type_name().
struct SOL_API TypeDesc {
    const char*            type_name;
    std::vector<FieldDesc> fields;
};

// Global registry populated at static-init time via SOL_REFLECT_BEGIN macros.
class SOL_API ComponentRegistry {
public:
    static ComponentRegistry& instance();

    void               register_type(TypeDesc desc);
    const TypeDesc*    find(std::string_view type_name) const;
    const std::vector<TypeDesc>& types() const { return m_types; }

private:
    std::vector<TypeDesc> m_types;
};

} // namespace sol

// ---------------------------------------------------------------------------
// Convenience macros — place at file scope in a single .cpp per type.
// ---------------------------------------------------------------------------

// Begin registration of class sol::ClassName.
#define SOL_REFLECT_BEGIN(ClassName)                                          \
    namespace {                                                                \
    static int _sol_reg_##ClassName = []() noexcept -> int {                  \
        using _T = ::sol::ClassName;                                           \
        ::sol::TypeDesc _desc;                                                 \
        _desc.type_name = #ClassName;

// Register a plain field.
#define SOL_FIELD(fname, ftype)                                               \
        _desc.fields.push_back({ #fname, (ftype),                            \
            [](void* _n) -> void* {                                           \
                return &static_cast<_T*>(_n)->fname;                          \
            }, {} });

// Register an enum int field with variadic string labels.
#define SOL_FIELD_ENUM(fname, ...)                                            \
        _desc.fields.push_back({ #fname, ::sol::FieldType::EnumInt,          \
            [](void* _n) -> void* {                                           \
                return &static_cast<_T*>(_n)->fname;                          \
            }, { __VA_ARGS__ } });

// End registration — submits the descriptor to the global registry.
#define SOL_REFLECT_END()                                                     \
        ::sol::ComponentRegistry::instance().register_type(std::move(_desc)); \
        return 0;                                                              \
    }(); }
