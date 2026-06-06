#include "physics/QueryFilter.h"

#include <toml++/toml.hpp>
#include <core/log.h>

namespace engine::physics {

bool loadQueryFilterFromToml(QueryFilter& filter,
                             const std::filesystem::path& path) {
    try {
        const auto tbl = toml::parse_file(path.string());

        const auto* rowArr = tbl["matrix"]["rows"].as_array();
        if (!rowArr) {
            LOG_WARN("QueryFilter: no [matrix].rows in '{}'", path.string());
            return false;
        }

        int i = 0;
        for (const auto& elem : *rowArr) {
            if (i >= kMaxPhysicsLayers) break;
            if (auto v = elem.value<int64_t>())
                filter.layerMask[static_cast<size_t>(i)] = static_cast<uint16_t>(*v);
            ++i;
        }
        return true;

    } catch (const toml::parse_error& e) {
        LOG_WARN("QueryFilter: parse error in '{}': {}", path.string(), e.description());
        return false;
    }
}

} // namespace engine::physics
