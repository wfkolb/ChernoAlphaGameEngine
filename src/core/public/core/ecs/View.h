#pragma once

#include "core/ecs/World.h"

#include <tuple>
#include <vector>

namespace engine::core::ecs {

    template<typename... Components>
    class View {
    public:
        explicit View(World& world) : world_(world) {
            // Build the required mask from each component's static kComponentId.
            ComponentMask required;
            (required.set(Components::kComponentId), ...);

            for (const auto& archPtr : world_.archetypes()) {
                if ((archPtr->mask & required) == required) {
                    matching_.push_back(archPtr.get());
                }
            }
        }

        // ---- Iterator ----

        struct Iterator {
            using ArchVec   = std::vector<Archetype*>;
            using ValueType = std::tuple<Entity, Components&...>;

            const ArchVec* archetypes = nullptr;
            size_t         archIdx    = 0;
            uint32_t       rowIdx     = 0;

            // Advance past archetypes that have no rows.
            void skipEmpty() {
                while (archIdx < archetypes->size() &&
                       rowIdx >= (*archetypes)[archIdx]->rowCount)
                {
                    ++archIdx;
                    rowIdx = 0;
                }
            }

            Iterator& operator++() {
                ++rowIdx;
                skipEmpty();
                return *this;
            }

            bool operator==(const Iterator& o) const noexcept {
                return archIdx == o.archIdx && rowIdx == o.rowIdx;
            }
            bool operator!=(const Iterator& o) const noexcept { return !(*this == o); }

            ValueType operator*() const {
                Archetype* arch = (*archetypes)[archIdx];
                Entity e = arch->entities[rowIdx];
                return { e, getComponent<Components>(arch, rowIdx)... };
            }

        private:
            template<typename C>
            static C& getComponent(Archetype* arch, uint32_t row) {
                auto& col = arch->columns.at(C::kComponentId);
                return *reinterpret_cast<C*>(col.data() + row * sizeof(C));
            }
        };

        Iterator begin() {
            Iterator it;
            it.archetypes = &matching_;
            it.archIdx    = 0;
            it.rowIdx     = 0;
            it.skipEmpty();
            return it;
        }

        Iterator end() {
            Iterator it;
            it.archetypes = &matching_;
            it.archIdx    = matching_.size();
            it.rowIdx     = 0;
            return it;
        }

    private:
        World& world_;
        std::vector<Archetype*> matching_;
    };

} // namespace engine::core::ecs
