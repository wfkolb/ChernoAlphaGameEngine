#ifdef ENGINE_DEVREL

#include "editor/MetaFileWriter.h"

#include <toml++/toml.hpp>
#include <core/log.h>

#include <fstream>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace engine::editor::MetaFileWriter {

std::filesystem::path metaPath(const std::filesystem::path& eassetPath) {
    return std::filesystem::path(eassetPath.string() + ".meta");
}

static uint64_t getFileModTime(const std::filesystem::path& p) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) return 0;
    ULARGE_INTEGER li;
    li.LowPart  = fad.ftLastWriteTime.dwLowDateTime;
    li.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    return li.QuadPart;
#else
    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(p, ec);
    if (ec) return 0;
    return static_cast<uint64_t>(ftime.time_since_epoch().count());
#endif
}

void write(const std::filesystem::path& eassetPath, const AssetMeta& meta) {
    toml::table source;
    source.insert_or_assign("path",       meta.sourcePath);
    source.insert_or_assign("mod_time",   static_cast<int64_t>(meta.sourceModTime));
    source.insert_or_assign("sha256",     meta.sourceSha256);

    toml::table settings;
    settings.insert_or_assign("uniform_scale",      static_cast<double>(meta.settings.uniformScale));
    settings.insert_or_assign("up_axis_z",           meta.settings.upAxisZ);
    settings.insert_or_assign("generate_collision",  meta.settings.generateCollision);
    settings.insert_or_assign("collision_type",      static_cast<int64_t>(meta.settings.collisionType));
    settings.insert_or_assign("merge_meshes",        meta.settings.mergeMeshes);

    toml::table root;
    root.insert_or_assign("source",          source);
    root.insert_or_assign("import_settings", settings);

    std::ofstream out(metaPath(eassetPath));
    if (!out) {
        LOG_WARN("MetaFileWriter: cannot write meta for '{}'", eassetPath.string());
        return;
    }
    out << root;
}

AssetMeta read(const std::filesystem::path& eassetPath) {
    AssetMeta meta;
    const auto mp = metaPath(eassetPath);
    std::error_code ec;
    if (!std::filesystem::exists(mp, ec)) return meta;

    try {
        const auto tbl = toml::parse_file(mp.string());

        if (auto s = tbl["source"]["path"].value<std::string>())
            meta.sourcePath = *s;
        if (auto t = tbl["source"]["mod_time"].value<int64_t>())
            meta.sourceModTime = static_cast<uint64_t>(*t);
        if (auto h = tbl["source"]["sha256"].value<std::string>())
            meta.sourceSha256 = *h;

        if (auto v = tbl["import_settings"]["uniform_scale"].value<double>())
            meta.settings.uniformScale = static_cast<float>(*v);
        if (auto v = tbl["import_settings"]["up_axis_z"].value<bool>())
            meta.settings.upAxisZ = *v;
        if (auto v = tbl["import_settings"]["generate_collision"].value<bool>())
            meta.settings.generateCollision = *v;
        if (auto v = tbl["import_settings"]["collision_type"].value<int64_t>())
            meta.settings.collisionType = static_cast<tools::CollisionType>(*v);
        if (auto v = tbl["import_settings"]["merge_meshes"].value<bool>())
            meta.settings.mergeMeshes = *v;

    } catch (const toml::parse_error& e) {
        LOG_WARN("MetaFileWriter: parse error in '{}': {}", mp.string(), e.description());
    }
    return meta;
}

bool exists(const std::filesystem::path& eassetPath) {
    std::error_code ec;
    return std::filesystem::exists(metaPath(eassetPath), ec);
}

bool isStale(const std::filesystem::path& eassetPath) {
    const AssetMeta meta = read(eassetPath);
    if (meta.sourcePath.empty()) return true;
    const uint64_t current = getFileModTime(std::filesystem::path(meta.sourcePath));
    return current != meta.sourceModTime;
}

} // namespace engine::editor::MetaFileWriter

#endif // ENGINE_DEVREL
