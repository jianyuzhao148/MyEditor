# Cocos Studio CSD UI Editor

A lightweight static UI viewer/editor for Cocos Studio source documents. The
old screenshot annotation workflow has been removed.

## Supported

- Recursively discovers all `.csd` files under the configured UI root.
- Cocos node hierarchy, anchors, nested position/scale/rotation and visibility.
- Standalone PNG/JPG textures and plist atlas sub-images, including rotated
  atlas frames.
- Plist frames are extracted on demand into `%TEMP%\csd_editor_plist` and
  rendered from those temporary files.
- Static ImageView, Sprite, Button, Text, Panel, ScrollView, ListView and other
  container layouts.
- Canvas selection and dragging.
- Editing node name, visibility, position, size, scale, rotation and anchor.
- Saving changes directly to the selected `.csd` file (`Ctrl+S`).
- Undo/redo for property changes and canvas dragging (`Ctrl+Z` / `Ctrl+Y`).
- File filtering, fit-to-window (`F`) and mouse-wheel zoom.

Animation timelines are intentionally ignored. Scale-9 widgets are displayed
with a whole-image stretch in this version.

## Project layout

- `src/main.cpp` owns the desktop app shell, docking UI, panels, shortcuts,
  selection, undo/redo and property editing.
- `src/engine/ui_engine.h` defines the generic 2D UI engine adapter contract,
  document load/save/snapshot hooks, common node property hooks and the shared
  `UiNode` scene model used by the editor UI.
- `src/engines/cocos/` contains the Cocos Studio `.csd` adapter. CSD XML parsing,
  node refresh, anchor/layout rules and parser validation live here.
- `src/resources/` owns image loading, plist atlas lookup and temporary extracted
  atlas frame generation.
- `src/core/` contains small shared geometry and XML helpers.

To add another 2D engine later, create a new adapter that implements
`I2dUiEngine`, return the engine's source file extension, parse its document into
`UiNode`, implement its own layout rules, and provide document save/snapshot and
common property read/write behavior. The ImGui editor shell, file browser,
hierarchy tree, canvas, selection, dragging and undo/redo can then stay shared.

The current Cocos-specific property panel still edits some advanced CSD XML
fields directly. Moving those into an engine-owned property schema is the next
step before a JSON engine can support full right-panel editing without CSD/XML
knowledge in `main.cpp`.

## Build and run on Windows

```powershell
cd D:\MyEditor
cmake -S . -B build_test
cmake --build build_test --config Release -j 4
.\dist\csd_editor.exe
```

Use another Cocos Studio directory when needed:

```powershell
.\dist\csd_editor.exe --csd-root D:\path\to\cocosstudio
```

Run the parser against every CSD without opening a window:

```powershell
.\dist\csd_editor.exe --validate-csd
```

The editor stores the last opened UI root in `csd_editor_state.json`. If no root
has been configured, start the app and set one from the `CSD Root` field, or pass
one with `--csd-root`.

`tinyxml2` is vendored under `third_party/tinyxml2`; building the editor no
longer requires a cocos2d-x checkout.
