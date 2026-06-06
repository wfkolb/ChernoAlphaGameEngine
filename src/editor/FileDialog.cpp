#ifdef ENGINE_DEVREL

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>

#include "editor/FileDialog.h"

namespace engine::editor::FileDialog {

std::filesystem::path openFile(const wchar_t* filter, const wchar_t* title) {
    wchar_t buf[MAX_PATH] = {};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = title;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return {};
    return std::filesystem::path(buf);
}

std::filesystem::path saveFile(const wchar_t* filter,
                               const wchar_t* defaultExt,
                               const wchar_t* title) {
    wchar_t buf[MAX_PATH] = {};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize    = sizeof(ofn);
    ofn.lpstrFilter    = filter;
    ofn.lpstrFile      = buf;
    ofn.nMaxFile       = MAX_PATH;
    ofn.lpstrDefExt    = defaultExt;
    ofn.lpstrTitle     = title;
    ofn.Flags          = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&ofn)) return {};
    return std::filesystem::path(buf);
}

} // namespace engine::editor::FileDialog

#endif // ENGINE_DEVREL
