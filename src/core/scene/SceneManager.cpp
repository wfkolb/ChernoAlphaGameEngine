#include "core/scene/SceneManager.h"

namespace engine::core::scene {

// ── Helpers ───────────────────────────────────────────────────────────────────

SceneManager::Entry* SceneManager::findEntry(std::string_view name) noexcept {
    for (auto& e : scenes_) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

const SceneManager::Entry* SceneManager::findEntry(std::string_view name) const noexcept {
    for (const auto& e : scenes_) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

// ── Public API ────────────────────────────────────────────────────────────────

Scene* SceneManager::load(std::string_view name) {
    if (findEntry(name)) return nullptr; // already exists

    Entry& entry    = scenes_.emplace_back();
    entry.name      = std::string(name);
    entry.scene     = std::make_unique<Scene>();
    entry.scene->load(name);
    return entry.scene.get();
}

void SceneManager::unload(std::string_view name) {
    for (size_t i = 0; i < scenes_.size(); ++i) {
        if (scenes_[i].name == name) {
            scenes_[i].scene->unload();
            scenes_.erase(scenes_.begin() + static_cast<ptrdiff_t>(i));
            return;
        }
    }
}

bool SceneManager::activate(std::string_view name) {
    Entry* e = findEntry(name);
    if (!e) return false;
    if (!e->scene->isActive()) e->scene->activate();
    return true;
}

void SceneManager::deactivate(std::string_view name) {
    Entry* e = findEntry(name);
    if (e && e->scene->isActive()) e->scene->deactivate();
}

void SceneManager::tickActive(float dt) {
    for (auto& e : scenes_) {
        if (e.scene->isActive()) e.scene->tick(dt);
    }
}

Scene* SceneManager::getActive() noexcept {
    for (auto& e : scenes_) {
        if (e.scene->isActive()) return e.scene.get();
    }
    return nullptr;
}

const Scene* SceneManager::getActive() const noexcept {
    for (const auto& e : scenes_) {
        if (e.scene->isActive()) return e.scene.get();
    }
    return nullptr;
}

Scene* SceneManager::get(std::string_view name) noexcept {
    Entry* e = findEntry(name);
    return e ? e->scene.get() : nullptr;
}

const Scene* SceneManager::get(std::string_view name) const noexcept {
    const Entry* e = findEntry(name);
    return e ? e->scene.get() : nullptr;
}

Scene* SceneManager::getByName(std::string_view name) noexcept {
    return get(name);
}

std::vector<Scene*> SceneManager::getAllActive() {
    std::vector<Scene*> result;
    for (auto& e : scenes_) {
        if (e.scene->isActive()) result.push_back(e.scene.get());
    }
    return result;
}

} // namespace engine::core::scene
