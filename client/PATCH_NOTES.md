# KewlKlient Developer Inspector

Adds a permanent live developer inspector to the existing Debug route / diagnostics popout.

## What it shows

- Runtime capability states and every resolved offset (existing diagnostics retained).
- Live scene pointer/base/dimensions and current game cycle.
- Local player uid/name/scene/fine position/plane/animation/orientation.
- Scene object totals by category and all nearby object rows (bounded for bridge safety):
  - distance
  - Loc id
  - category (game/boundary/wall-deco/floor-deco)
  - scene position / plane
  - fine X/H/Y placement
  - native scene-record address
  - renderable pointer
  - renderable vtable
  - classified renderable kind (RuntimeModel / ModelData / DynamicLoc / unknown)
- Nearby players and NPCs with uid/name/type-id/scene/fine position/animation/orientation/address.

## Refresh behavior

The diagnostics tail now contains live scene data, so `bridge.hpp` republishes the same Java model revision every 500ms solely to refresh native diagnostics. `launcher/main.cpp` correspondingly reparses the shared model region every 500ms even if Java's plugin-model revision did not change.

This avoids making the normal Java plugin model churn at 2Hz while still giving the inspector live data.

## Launcher UI

The existing Debug route's `DLL diagnostics...` button becomes `Developer inspector...`.
The narrow sidebar shows the first four `live:` summary lines directly; the full popout contains nearby object/player/NPC rows plus the complete offsets table.
The popout is enlarged to 980x760.

## Files

- `client/diagnostics.hpp`
- `client/bridge.hpp`
- `launcher/panel_ui.hpp`
- `launcher/main.cpp`

## Compatibility

`diagnostics.hpp` expects the current object/Loc SDK native layer already present in the Object Highlighter branch (`SceneLoc`, `forEachSceneLoc`, `LocScene`, and Loc renderable-dispatch capability). It does not add or change those layouts.

## Current object-highlighter debugging use

Stand next to a tree and open Debug -> Developer inspector.

- `live: objects total=0` means scene enumeration is still broken.
- Nearby `object:` rows with sensible IDs/categories mean enumeration works.
- `rend=0` means scene-record -> renderable acquisition failed.
- `kind=unknown` with a nonzero vtable means a Renderable subclass is not classified yet.
- `kind=RuntimeModel`, `ModelData`, or `DynamicLoc` means the scene/renderable half is working; then focus on `objectHull` projection/geometry.
