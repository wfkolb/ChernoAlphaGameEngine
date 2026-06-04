#include "physics/PhysicsMaterialTable.h"
#include <toml++/toml.hpp>
#include <core/log.h>

namespace engine::physics {

bool PhysicsMaterialTable::load(const std::string& path) {
    ensureDefault();
    try {
        auto tbl = toml::parse_file(path);
        auto arr = tbl["material"].as_array();
        if (!arr) {
            LOG_WARN("PhysicsMaterialTable: no [material] array in '{}'", path);
            return false;
        }

        for (auto& elem : *arr) {
            auto* t = elem.as_table();
            if (!t) continue;
            PhysicsMaterial mat;
            if (auto n = (*t)["name"].value<std::string>())       mat.name        = *n;
            if (auto f = (*t)["friction"].value<double>())        mat.friction    = static_cast<float>(*f);
            if (auto r = (*t)["restitution"].value<double>())     mat.restitution = static_cast<float>(*r);
            materials_.push_back(std::move(mat));
        }
        return true;
    } catch (const toml::parse_error& e) {
        LOG_WARN("PhysicsMaterialTable: parse error in '{}': {}", path, e.description());
        return false;
    }
}

const PhysicsMaterial& PhysicsMaterialTable::get(uint8_t index) const noexcept {
    if (index >= materials_.size()) return materials_[kDefaultMaterial];
    return materials_[index];
}

uint8_t PhysicsMaterialTable::findByName(std::string_view name) const noexcept {
    for (size_t i = 0; i < materials_.size(); ++i) {
        if (materials_[i].name == name) return static_cast<uint8_t>(i);
    }
    return kDefaultMaterial;
}

void PhysicsMaterialTable::ensureDefault() {
    if (materials_.empty()) {
        materials_.push_back({"default", 0.5f, 0.1f});
    }
}

} // namespace engine::physics
