#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace engine::physics {

struct PhysicsMaterial {
    std::string name;
    float friction    = 0.5f;
    float restitution = 0.1f;
};

class PhysicsMaterialTable {
public:
    // Load from TOML file. Returns false on parse failure; leaves default material intact.
    bool load(const std::string& path);

    const PhysicsMaterial& get(uint8_t index) const noexcept;
    uint8_t findByName(std::string_view name) const noexcept; // returns kDefaultMaterial if not found

    uint8_t count() const noexcept { return static_cast<uint8_t>(materials_.size()); }

    static constexpr uint8_t kDefaultMaterial = 0;

private:
    void ensureDefault();

    std::vector<PhysicsMaterial> materials_;
};

} // namespace engine::physics
