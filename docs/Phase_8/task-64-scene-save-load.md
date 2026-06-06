# Task #64 — Scene Save & Load in the Editor

**Phase 8 — tools / editor — Version 0.8.x**
**Audience:** Editor developer, Tools Lead
**Depends on:** Task #54 (SceneSerializer), Task #58 (EngineEditor)
**Resolves:** TD-10 (add warning log for skipped unknown components)
**Must precede:** Task #65 (Prefab system reuses the file-dirty tracking built here)

---

## Table of Contents

1. [Goal](#1-goal)
2. [Current State](#2-current-state)
3. [File Menu](#3-file-menu)
4. [Scene State Tracking — Dirty Flag](#4-scene-state-tracking--dirty-flag)
5. [Save Flow](#5-save-flow)
6. [Load Flow](#6-load-flow)
7. [Recent Scenes](#7-recent-scenes)
8. [New Scene](#8-new-scene)
9. [Scene Properties Panel](#9-scene-properties-panel)
10. [Error Handling & Validation UI](#10-error-handling--validation-ui)
11. [TD-10 Fix — Log Unknown Components](#11-td-10-fix--log-unknown-components)
12. [New Files](#12-new-files)
13. [Undo/Redo Integration](#13-undoredo-integration)
14. [Tests](#14-tests)

---

## 1. Goal

The editor has a working **File** menu. A user can:

- Create a new empty scene (Ctrl+N)
- Open a `.scene` file from disk (Ctrl+O)
- Save the current scene (Ctrl+S) with an unsaved-changes prompt on close
- Save As… to a new path (Ctrl+Shift+S)
- See a **Recent Scenes** list
- Edit `SceneGlobals` properties (gravity, fog, game mode, etc.) in a dedicated panel

All of this goes through the existing `SceneSerializer::save()` / `SceneSerializer::load()` API. No changes to the serialization format.

---

## 2. Current State

- `SceneSerializer` — fully implemented (Task #54); `save()`, `load()`, `loadAsync()`, `validate()` all exist
- `SceneManager` — `load()`, `activate()`, `getActive()`, etc. are implemented
- `EditorApp` — has a `SceneManager` but no File menu; the editor opens a hardcoded empty scene at startup
- `InspectorPanel` — no widget for `SceneGlobals`
- No dirty-flag tracking exists anywhere

---

## 3. File Menu

Add to `EditorApp`'s main menu bar: **File** menu.

| Item | Shortcut | Behavior |
|------|----------|----------|
| New Scene | Ctrl+N | Prompt if dirty; create empty scene |
| Open Scene… | Ctrl+O | File dialog → load; prompt if dirty |
| Save | Ctrl+S | Save to current path; opens Save As if no path |
| Save As… | Ctrl+Shift+S | File dialog → save to new path |
| Recent Scenes | (submenu) | Last 10 scenes; click to open |
| ─── | | Separator |
| Exit | Alt+F4 | Prompt if dirty; close |

Use `ImGui::BeginMenu("File")` / `ImGui::MenuItem()`. The file dialog should use the Win32 `GetOpenFileName` / `GetSaveFileName` API (already available since the editor is Win32) wrapped in a thin helper `src/editor/FileDialog.h`.

---

## 4. Scene State Tracking — Dirty Flag

`EditorApp` owns a `SceneFileState` struct:

```cpp
struct SceneFileState {
    std::filesystem::path currentPath;   // empty = "untitled"
    bool                  isDirty = false;
    std::string           displayName;   // "MyMap.scene" or "Untitled*"
};
```

`isDirty` is set to `true` whenever any `ICommand` is pushed onto the `UndoStack`. It is cleared on successful save.

Display the dirty indicator in the window title:
```
EngineEditor — MyMap.scene*   ← asterisk when dirty
EngineEditor — MyMap.scene    ← no asterisk when clean
```

Set the Win32 title bar via `SetWindowTextW(hwnd, ...)` in `EditorApp::onFrameEnd()` when `displayName` changes.

---

## 5. Save Flow

```
User: Ctrl+S
│
├─ if currentPath is empty → open SaveAs dialog
│
├─ SceneSerializer::save(activeScene, currentPath)
│         │
│         ├─ success → isDirty = false; update window title
│         └─ failure → show error dialog (see Section 10)
│
└─ Add currentPath to recentScenes list → write editor_prefs.toml
```

**Atomic write:** `SceneSerializer::save()` already writes to `<path>.tmp` then renames — this is the existing implementation from Task #54. No changes needed to the serializer.

**Save As dialog:** Filter: `Engine Scene (*.scene)\0*.scene\0All Files (*.*)\0*.*\0`

---

## 6. Load Flow

```
User: Ctrl+O or clicks Recent Scene
│
├─ if isDirty → show "Unsaved Changes" dialog
│     [Save]      → save, then proceed
│     [Discard]   → proceed without saving
│     [Cancel]    → abort load
│
├─ SceneSerializer::validate(path) → if invalid, show error and abort
│
├─ SceneManager::unload(activeScene)
│
├─ future = SceneSerializer::loadAsync(newScene, path)
│         │
│         ├─ show progress bar modal while future is pending
│         └─ on complete:
│               ├─ success → SceneManager::activate(newScene)
│               │            UndoStack::clear()
│               │            isDirty = false
│               │            currentPath = path
│               └─ failure → show error dialog; restore previous empty scene
```

**Loading on the main thread** is acceptable for Phase 8 (scene files will be small — < 50 entities). `loadAsync` is called anyway to keep the API consistent; the future completes before the next frame due to negligible load time on dev hardware.

---

## 7. Recent Scenes

Store in `editor_prefs.toml` under a `[recent_scenes]` key:

```toml
[recent_scenes]
paths = [
    "C:/projects/mygame/maps/test_arena.scene",
    "C:/projects/mygame/maps/lobby.scene",
]
```

- Maximum 10 entries; oldest entry dropped when the list exceeds 10
- Paths are absolute; display in the menu as filename only (full path in tooltip)
- Remove stale entries (path no longer exists) on load; update the file

`EditorPrefs` class (new, see Section 12) owns read/write of `editor_prefs.toml`.

---

## 8. New Scene

**New Scene** creates an empty scene with default `SceneGlobals`:

```cpp
SceneGlobals defaults {
    .gravity          = {0.f, -9.81f, 0.f},
    .ambientColor     = {0.05f, 0.05f, 0.07f},
    .fogDensity       = 0.0f,
    .sceneName        = "Untitled",
    .matchTimeLimit   = 600,   // 10 minutes
    .maxPlayers       = 16,
    .gameMode         = "Deathmatch",
    .navmeshAsset     = "",    // no navmesh
};
```

The UndoStack is cleared. `currentPath` is set to empty. `isDirty` starts as false (a new empty scene is not considered modified until the user makes a change).

---

## 9. Scene Properties Panel

**New panel:** `src/editor/panels/ScenePropertiesPanel.h/.cpp`

Add to editor menu: **Window → Scene Properties** (also reachable by double-clicking the scene root in the `SceneHierarchyPanel`).

### Fields

| Field | Widget | Notes |
|-------|--------|-------|
| Scene Name | `InputText` | Sets `SceneGlobals.sceneName`; also suggest renaming the file on save |
| Gravity | `DragFloat3` | Default (0, −9.81, 0); unit: m/s² |
| Ambient Color | `ColorEdit3` | `SceneGlobals.ambientColor` |
| Fog Density | `DragFloat` | 0.0 = no fog |
| Match Time Limit | `DragInt` | seconds; displayed as mm:ss |
| Max Players | `DragInt` (1–64) | |
| Game Mode | `InputText` | String name; validated against registered `IGameMode` names |
| Navmesh Asset | Asset drop target (`.nav`) | Read-only in Phase 8 (no baking tool yet); shows path or "None" |
| Spawn Points | Read-only list | Counts entities with `SpawnPointEntity` archetype in the scene |

All edits push a `SceneGlobalsCommand` onto the undo stack.

---

## 10. Error Handling & Validation UI

| Failure | Dialog | Recovery |
|---------|--------|----------|
| `SceneSerializer::validate()` fails (bad magic, corrupt CRC) | Error dialog: "Scene file is corrupt or from a newer engine version. Details: [error string]" | Offer to open a different file |
| Save fails (disk full, permission denied) | Error dialog with OS error message | isDirty stays true; path unchanged |
| Load partially succeeds (some components skipped) | Warning dialog: "X component(s) were skipped during load — see the console for details." | Scene is loaded with missing components; user can inspect the console |
| File no longer exists in Recent Scenes | Menu item is greyed out with "(not found)" suffix | Removed from list on next `editor_prefs.toml` write |

---

## 11. TD-10 Fix — Log Unknown Components

In `SceneSerializer.cpp`, in the component-skip branch (where an unknown `ComponentTypeId` is encountered during load), add:

```cpp
LOG_WARN("SceneSerializer: skipping unknown component type ID %u on entity %u. "
         "Was the component registered before loading this scene?",
         typeId, entityHandle);
```

This change is one line in the existing serializer. It satisfies TD-10 and makes the "X component(s) were skipped" count in the warning dialog accurate.

---

## 12. New Files

```
src/editor/
├── FileDialog.h / .cpp           — Win32 GetOpenFileName/GetSaveFileName wrapper
├── EditorPrefs.h / .cpp          — Read/write editor_prefs.toml (recent scenes, layout prefs)
└── panels/
    └── ScenePropertiesPanel.h / .cpp
```

### `EditorPrefs` key responsibilities

- `loadFromDisk()` / `saveToDisk()` — called at editor startup and shutdown
- `addRecentScene(path)` — prepends to list, caps at 10, deduplicates
- `getRecentScenes() -> std::vector<path>` — filtered to existing files
- `getPIEPort() -> uint16_t` (resolves TD-19: default 57300, configurable in prefs)
- Stores ImGui layout: delegates to ImGui's `ImGui::SaveIniSettingsToDisk()` / `ImGui::LoadIniSettingsFromDisk()`

### Modify Existing Files

- `src/editor/EditorApp.cpp` — add File menu; add `SceneFileState`; call `EditorPrefs::loadFromDisk()` at startup
- `src/editor/EditorApp.h` — add `SceneFileState m_fileState`; add `EditorPrefs m_prefs`
- `src/editor/UndoStack.cpp` — add `onCommandPushed` callback that sets `m_fileState.isDirty = true`
- `src/tools/SceneSerializer.cpp` — add `LOG_WARN` in skip branch (TD-10)
- `src/editor/PIEController.cpp` — read port from `EditorPrefs::getPIEPort()` (resolves TD-18, TD-19)

---

## 13. Undo/Redo Integration

| User action | Effect on dirty state |
|-------------|-----------------------|
| Any command pushed | `isDirty = true` |
| Undo all commands (back to clean state) | `isDirty = false` |
| Save | `isDirty = false`; snapshot undo stack position |
| Open new scene | Undo stack cleared; `isDirty = false` |

"Back to clean state" tracking: record the undo stack index at the last save. Compare current index to saved index — if equal, `isDirty = false`. This handles the case where a user makes a change, undoes it, and the file is logically unchanged.

---

## 14. Tests

**File:** `tests/editor/SceneSaveLoadTests.cpp` (label: unit)

- Save a scene with 10 entities; load it back; verify entity count and component values match
- Save As to a new path; verify original path is unchanged
- Dirty flag: starts false on new scene; true after any `UndoStack::push()`; false after save
- Dirty flag: false after undo back to initial state (if undo-index tracking is implemented)
- Recent scenes list: add 11 scenes; verify list caps at 10 and oldest entry is dropped
- `SceneSerializer` skip-log: create a scene file with a foreign component type ID; load it; verify `LOG_WARN` was called (use a log capture test helper)
- `EditorPrefs` round-trip: write PIE port 57400; reload from disk; verify 57400 is returned
