#pragma once
#include <string>
#include <filesystem>

namespace engine::core::fs {

// Thin wrapper over std::filesystem::path with engine-specific helpers.
class Path {
public:
    Path() = default;
    explicit Path(std::wstring_view s) : p_(s) {}
    explicit Path(std::string_view  s) : p_(std::filesystem::path(s)) {}

    Path operator/(std::wstring_view segment) const { return Path(p_ / segment); }
    Path operator/(std::string_view  segment) const { return Path(p_ / std::filesystem::path(segment)); }

    bool         exists()      const { return std::filesystem::exists(p_); }
    bool         isFile()      const { return std::filesystem::is_regular_file(p_); }
    bool         isDirectory() const { return std::filesystem::is_directory(p_); }
    std::wstring wstr()        const { return p_.wstring(); }
    std::string  str()         const { return p_.string(); }  // UTF-8

    // Resolve exe-relative path: Path::exeDir() / segment
    static Path exeDir();
    // Resolve %LOCALAPPDATA%\engine\ prefix
    static Path localAppData(std::wstring_view subpath = {});

    const std::filesystem::path& native() const { return p_; }

private:
    explicit Path(std::filesystem::path p) : p_(std::move(p)) {}

    std::filesystem::path p_;
};

} // namespace engine::core::fs
