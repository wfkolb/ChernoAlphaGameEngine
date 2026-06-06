#pragma once
#ifdef ENGINE_DEVREL

#include <filesystem>
#include <string>

namespace engine::editor {

// Thin wrappers around Win32 GetOpenFileName / GetSaveFileName.
// Each function opens a modal dialog and returns the selected path,
// or an empty path if the user cancels.
namespace FileDialog {

// Open-file dialog. filter example: L"Scene Files\0*.scene\0All Files\0*.*\0"
std::filesystem::path openFile(const wchar_t* filter, const wchar_t* title = nullptr);

// Save-file dialog. defaultExt example: L"scene"
std::filesystem::path saveFile(const wchar_t* filter,
                               const wchar_t* defaultExt,
                               const wchar_t* title = nullptr);

} // namespace FileDialog

} // namespace engine::editor

#endif // ENGINE_DEVREL
