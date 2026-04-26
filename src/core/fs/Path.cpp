#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>
#include "core/fs/Path.h"

namespace engine::core::fs {

// All constructors, operators, and query methods are inline in the header.
// Only the two static helpers that call Win32 APIs need a .cpp body.

Path Path::exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return Path(std::filesystem::path(buf).parent_path());
}

Path Path::localAppData(std::wstring_view subpath) {
    PWSTR raw = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw);
    std::filesystem::path base(raw);
    CoTaskMemFree(raw);
    base /= L"engine";
    if (!subpath.empty()) base /= subpath;
    return Path(std::move(base));
}

} // namespace engine::core::fs
