# The `kewl.api` reference

What a plugin may call, what each call promises, and what it does not. Everything here is the
public surface plugin authors compile against: the eleven classes in `java/kewl/api/` plus the
`Skill` enum that lives beside them. `kewl.Plugin` itself (lifecycle, `config`, `later()`) is
covered in [plugin-system.md](plugin-system.md); this document is the world and act surface.

Status on **client-240-6**, the build every limitation below was checked against. A limitation is
a property of a not-yet-derived memory offset, not of the API shape — when the offset lands the
limitation disappears without the API changing. Each one names the offset or mechanism that has
to move first.

---

## Reading this client: four conventions

These four apply to every class. They are the things that, if you internalise them once, make the
per-class notes below mostly reminders.

**1. Everything is a snapshot.** `Game.refresh()` runs once per frame, before any plugin, and
every value you read was captured then. Entities do not move halfway through your `render()`, and
two plugins in the same frame see the same world. The flip side: values are stale by one frame at
most, and an `Entity` you keep across frames describes where it *was*.

**2. World and scene coordinates are different, and the API takes world.** World coordinates are
absolute — Lumbridge is around 3222, 3218, the numbers on your minimap. Scene coordinates are
0..103 inside the currently loaded chunk and are what the game's click function actually wants.
Everything a plugin touches is world; the one conversion lives in `Game.toScene()`. Mixing them
up is the single most common bug in this codebase, which is why the conversion happens in exactly
one place.

**3. Distances are Chebyshev (diagonal).** `distance()` counts the larger of the x and y gaps,
the way the game's own interaction ranges work — "within 1 tile" is a 3×3 box, not a circle.

**4. Null and false are honest answers, not errors.** A projection that fails returns null. An
action that was not issued returns false. Nothing in `kewl.api` throws for "the game is not in a
state where that makes sense" — you get the empty, zero, or absent answer, and the methods that
can fail meaningfully return a reason (see `Actions.WalkResult`).

Threading: `tick()` and `render(Graphics2D)` run on the overlay thread, ~30 times a second, tick
first. Neither may block. `kewl.api` calls are cheap reads off the snapshot and are fine from
either method; the one thing you must not do is pace a sequence with sleeps — keep a timestamp
and compare it, the way `kewl.plugins.Woodcutter` does.

---

## `Game` — the world, and the coordinate space

The entry point. Every other class hangs off it or beside it.

```java
Game.ready()                 // true once you are in the world (not login, not loading)
Game.me()                    // your Local — never null, check .exists()
Game.entities()              // every visible NPC and player except you
Game.npcs() / Game.players() // the two halves of that
Game.sceneBaseX()/Y()        // world coords of the scene's south-west corner
```

**Guarantees**

- The lists are refreshed once per frame, before plugins run, and are unmodifiable. Holding a
  list across frames is safe — it just describes the old frame.
- `toScene(worldX, worldY)` returns null outside the loaded 104×104 chunk — a real answer, not an
  error: you cannot click a tile the client has not loaded. (The bound is strict: index 104 does
  not count as loaded. That off-by-one used to be a bug here.)
- Entity name caching is handled for you: the cache is keyed by (kind, uid) and cleared whenever
  the entity tables can have been rebuilt underneath it (scene move, world hop standing still,
  login, your own uid changing). An NPC that inherited a despawned player's uid cannot inherit
  its name.
- Projection (`projectFine`, `projectTile`, `projectWorld`, `tileOutline…`) goes through the
  game's own projection function, so results match what is on screen by construction, at any
  camera angle. `tileOutline`'s four corners each project independently, so a marker lies on the
  ground in perspective rather than stuck to the screen.

**Known limitations**

- **No terrain heightmap.** The height axis of the client is readable for *entities* (each knows
  the ground under its own feet) but the terrain itself is not. So:
  - `projectTile`/`tileOutlineWorld` draw at a *guessed* height: `groundHeightGuess()` is the
    height under you, and `heightNear(sceneX, sceneY)` samples the nearest entity standing on or
    next to the tile — exact on your tile, right on flat ground near you, off by the slope
    elsewhere, and a full floor (~240 units) out on upper floors.
  - `Entity.screen()` is *not* affected — entities carry their own exact height, so markers on
    entities land at their feet, not at datum 0.
  - Fixing this means deriving the heightmap offset in `client/offsets.hpp`; `projectFine`'s
    javadoc tracks the residual so the fix can be measured in-game.
- **Scene is 104×104 and that is all there is.** Anything beyond the loaded chunk is genuinely
  invisible: no lookup, no click, no projection.

---

## `WorldPoint` — a tile in world coordinates

A dumb, final, value class: `x()`, `y()`, `distanceTo` (Chebyshev), `within`, value equality.
It exists so a tile in your plugin's code has a name, and so the world-vs-scene bug has nowhere
to hide — if a method takes a `WorldPoint`, it takes world coordinates.

**Guarantees** — immutable, `equals`/`hashCode` work, safe as a map key.
**Known limitations** — no plane (floor) field. The plane exists on `Local` but a `WorldPoint`
cannot say which floor it means. Multi-floor positions (stairs, ladders) are on you.

---

## `Local` — you

`Game.me()`. Never null: before you spawn it is a placeholder whose `exists()` is false and whose
numbers are all zero, so a plugin that forgets to check gets harmless zeros instead of an NPE.

```java
Local me = Game.me();
me.exists()        // false until you have spawned — gate on this
me.worldX()/Y()    // minimap coordinates
me.location()      // that, as a WorldPoint
me.plane()         // 0 = ground floor
me.animation()     // -1 when standing still; me.isIdle()
me.orientation()   // facing, 0..2047, 0 = south, clockwise
me.runEnergy()     // 0..100
me.health() / me.maxHealth() / me.healthPercent()
me.fineX()/Y()     // rendered position, 128 fine units per tile
me.height()        // ground height under you — the terrain-height stand-in, see Game
```

**Guarantees**

- `health`/`maxHealth` are `Skills.effective/level` of `Skill.HITPOINTS` — there is no separate
  health read anywhere in the client, because none is needed.
- `healthPercent()` returns 100 (not an exception, not NaN) when max health is unreadable before
  spawn.

**Known limitations**

- `isIdle()` is the classic double-click trap: the animation does not start on the same tick as
  the click that caused it. A plugin that acts, then immediately checks `isIdle()`, sees idle and
  acts again. Give it a moment — `Woodcutter` shows the timestamp pattern.
- No run-or-walk state, no special-attack energy, no prayer points, no weighted inventory: none of
  those offsets are derived yet.

---

## `Entity` — one NPC or player

A frozen snapshot of one visible entity, taken at the top of the frame. You never construct one;
you get them from `Game`, `Npcs`, `Players`.

```java
Entity cow = Npcs.nearestWithin(10, 2790);
cow.uid()          // the game's handle — stable while it is on screen, reused after despawn
cow.id()           // NPC type id (what you filter on); -1 for players, see limitations
cow.name()         // the client's own name, "" when unreadable (mid-spawn, despawned this frame)
cow.isPlayer() / cow.isNpc()
cow.animation() / cow.isIdle() / cow.orientation()
cow.sceneX()/Y()   // 0..103; what Actions' natives ultimately want
cow.worldX()/Y() / cow.location()
cow.distance()     // Chebyshev tiles from you
cow.fineX()/Y()    // rendered position — between tiles while it walks
cow.screen()       // where to draw a marker, or null off screen
cow.tileOutline()  // the tile it stands on, in perspective, at its own height
```

**Guarantees**

- Immutable. The game mutates its own objects continuously; this is a copy, so your `render()`
  never races a walk.
- `screen()` projects from the entity's *rendered* position including its exact ground height, so
  the marker sits at its feet — this is the one projection that is exact everywhere, on any floor,
  because the entity carries its own height.
- `name()` resolves through a bounded cache (cleared on entity-table churn, see `Game`); the
  underlying native walk is too slow for per-entity-per-frame use, and the cache is why `name()`
  in a hot loop is fine anyway.
- `uid` identifies one entity *of a kind* for as long as it is on screen. NPC and player uids are
  separate keyspaces.

**Known limitations**

- **`id()` is -1 for players.** The combat-level offset the shim had is wrong on this build and is
  gated off (`PLAYER_COMBAT_LEVEL` in `client/offsets.hpp`) pending re-derivation. No player
  carries a combat level yet. (`Players`' class javadoc still claims combat level is readable —
  the `Entity` javadoc is the current truth.)
- **Player names**: per `Players`, names of other players are not usable from this API; `name()`
  is the NPC-facing path. Player name tags are listed in the README as a real contribution.
- `uid` is reused after despawn. Never keep a long-lived registry keyed by bare uid across a
  despawn — key by (kind, uid) the way the name cache does, or re-resolve every frame.
- No overhead-animation, no target/interacting-with, no health bar, no hitsplats: not derived.

---

## `Npcs` — finding NPCs

Filters over `Game.npcs()`, which is already this frame's snapshot, so every query is an ordinary
list scan and costs nothing worth thinking about.

```java
Npcs.all()                       // every visible NPC
Npcs.withId(2790, 2791)          // by type id(s); no ids = everything
Npcs.nearest(2790)               // closest of a type, or null — by tile distance, not screen
Npcs.nearestWithin(10, 2790)     // closest within 10 tiles, or null
Npcs.count(2790)                 // how many are visible
```

**Guarantees**

- `nearest` picks by *tile* distance deliberately: the NPC behind you on screen may be the one you
  can actually reach. If you want "closest to where I am looking", project candidates and compare
  screen points yourself.
- Returns alias the snapshot lists; the entities are the same objects `Game` handed out.

**Known limitations**

- Type-id filtering is all there is — no name-based queries (that would need `Entity.name()` per
  candidate, which works but costs a native walk per unknown id; filter by id when you can), and
  no "nearest walkable" or line-of-sight queries.

---

## `Players` — finding other players

Deliberately thin: `all()`, `nearest()`, `countWithin(radius)`. Same snapshot, same Chebyshev
distances, same guarantees as `Npcs`.

**Known limitations** — the interesting ones live here:

- No names (see `Entity`).
- No combat levels on this build (see `Entity.id()` — the class javadoc predates the gate-off and
  is stale on this one point).
- No skilling/activity visibility, no friends-list cross-reference.

---

## `Skills` / `Skill` — your stats

```java
Skills.level(Skill.ATTACK)       // what your xp has earned — the number that gates content
Skills.effective(Skill.ATTACK)   // after potions/prayers/drains — what the game checks right now
Skills.experience(Skill.ATTACK)  // total xp — snapshot at startup, subtract for xp/hour
Skills.boost(Skill.ATTACK)       // effective - level; negative when drained
Skills.totalLevel()              // summed across all skills
```

`Skill` is the enum of all skills, in the **client's array order** — not alphabetical, not the
skill tab's order. Do not reorder it; the index is what addresses the client's stat arrays.
`HITPOINTS` is index 3, which is why current/max health are just the effective/base of that skill.

**Guarantees**

- The whole stat table is one native call, cached per frame (`Skills.newFrame()` is called by the
  client) — asking for twenty skills costs the same as one.
- `effective` is what the game itself uses for gating ("can I chop this right now"), and what the
  skill tab shows in a different colour when boosted.

**Known limitations**

- No xp-to-next-level helper (derivable from `experience` if you embed an xp table), no skill
  *goals*, no total-xp.

---

## `Actions` — doing things

The one class that acts. Every method goes through the game's own menu-action function — the same
one that runs when you right-click and pick an option. **This client never builds a network
packet**, which is why it contains no protocol table and survives most game updates.

```java
Actions.walk(worldX, worldY)      // WalkResult — walk, saying how it went
Actions.walkTo(worldX, worldY)    // boolean — walk().moved()
Actions.object(id, x, y)          // scenery's first option: Chop down, Mine, ...
Actions.npc(entity)               // NPC's first option: Attack, or Talk-to on a shopkeeper!
Actions.npc(entity, option)       // option 1..5, as the right-click menu numbers them
Actions.aim(x, y)                 // where a click WOULD land, or why not — posts nothing
Actions.press(x, y) / release()   // hold-the-button split click; every press owes a release
```

**Guarantees**

- **`walk` tells you why, not just whether.** `WalkResult`: `ACTION` (the game's own function
  walked us), `CLICKED` (a left click was actually posted), `NOT_LOADED`, `OFF_SCREEN` (behind
  camera — turning it would help), `OFF_CANVAS` (refused: outside the canvas we know the size
  of), `NOT_POSTED` (aimed fine, but the message queue refused — nothing reached the game),
  `NOT_LOGGED_IN` (nothing is ever clicked at the title screen). `.moved()` is true for the first
  two only.
- **No stale points are ever clicked.** Scene conversion, ground height and projection happen
  inside the call, on the frame you call. A tile chosen seconds ago is clicked where it is *now*;
  if the camera swung since, you get `OFF_SCREEN` instead of a wrong walk. (`Actions.aim` exists
  so a driver can pre-check several candidate tiles on the same frame — but an `Aim` you keep and
  click later is a stale pixel. Re-aim on the frame you post.)
- Clicks are humanised by default: a per-click offset inside the destination tile (≤ ~⅓ tile,
  gaussian, applied in fine units so it is a fraction of a tile at every camera angle) plus an
  approach move before the landing move. `setHumanise(false)` for exact centres when debugging a
  projection.
- Refusals are refusals: not logged in, behind camera, or off canvas means **nothing is sent** —
  clamping into the canvas would silently walk somewhere you did not ask for.
- `press`/`release`: a failed release keeps the debt — `release()` returns false while the button
  is still down and must be retried next frame; it is a safe no-op (returning true) when nothing
  is pressed, so call it unconditionally on shutdown.
- `npc(entity, option)` with option outside 1..5 is ignored rather than sent — an out-of-range
  opcode lands on some unrelated action, so it refuses instead.
- Edge clicks are kept 4px clear of the canvas border (`EDGE_MARGIN`), where the game's hit test
  least agrees with ours.

**Known limitations**

- **Everything except walk is a no-op on client-240-6.** `DO_ACTION == 0`: the menu-action
  function could not be derived for this build (`client/offsets.hpp` names the hook candidates),
  so `object()`/`npc()` return false honestly, and the opcodes (`OPLOC1`, `OPNPC1`) are unverified
  — captured on an older build. The day the offset lands, all of it starts working with no API
  change. Until then `walk`/`walkTo` carry the weight, by posting ground clicks (posted clicks
  *are* honoured — autologin's Login and CLICK HERE TO PLAY clicks are the live proof).
- `walk` in FIXED display mode: inventory and minimap are part of the same canvas, so a tile that
  projects under them clicks the interface instead of the ground. The game's own hit test owns
  that; the cost is a wasted click, not a wrong walk.
- `npc(entity)` takes the *first* menu option — Attack on anything hostile, but Talk-to on a
  shopkeeper. A bot that assumes it always attacks will cheerfully greet a cow.
- Clicking has no rate limiting built in. These run on the overlay thread; pace yourself the way
  every plugin in this repo does.
- No player interactions, no inventory item use, no spell casting, no widget clicks via the action
  function: none of it until `DO_ACTION` lands.

---

## `Input` — raw input injection

The layer under `Actions`' click path: `WM_CHAR` for text, `WM_KEYDOWN`/`WM_KEYUP` for control
keys, `WM_MOUSEMOVE`/`WM_LBUTTONDOWN`/`WM_LBUTTONUP` for the mouse — all posted to the game's
render view.

```java
Input.typeChar('a')          // one character of text
Input.key(Input.VK_BACK, true)   // control key down; pair every down with an up
Input.click(x, y)            // move, down, up — one whole click, true only if ALL queued
Input.click(x, y, ax, ay)    // approach move first, so the cursor does not teleport
Input.mouseDown(x, y) / Input.mouseUp(x, y)
Input.mouseMove(x, y)
Input.target(grab)           // Input.Target: what window input goes to, canvas size, focus state
```

Coordinates are **canvas** coordinates — the space `Game.projectFine` answers in.

**Guarantees**

- Fire-and-forget, and honest about it: `true` means the message was queued, not that the game
  acted. Posted **clicks** are proven to work on client-240-6 (autologin), which is what makes
  click-to-walk possible while `DO_ACTION` is underived.
- `click()` posts move/down/up as three separate `PostMessageW` calls with distinct message times
  — the shape a fast human click produces. Returns false the moment any one was refused.
- The two-move `click` variant exists because a cursor teleporting to the exact pixel is the one
  thing an observer can see for free; two moves look like a hand.
- Nothing here logs what it types. The first caller was autologin's password path, and that
  shaped the API.
- `typeChar` vs `key`: text goes as `WM_CHAR` because the game's own `TranslateMessage` would
  otherwise decide case from the physical shift state and double every character.

**Known limitations**

- **There is deliberately no `typeText(String)`.** The frame thread may not block, and a
  whole-string call invites a burst the login screen may drop. Pace characters across frames —
  `kewl.plugins.autologin.LoginSequence` is the worked example.
- **A mouse move while any button is down is a DRAG** — including a button the *user* is holding
  on the same physical mouse (seen live: a posted move landed between the user's down and up on
  the world-map close button and panned the map). Before every posted move, decide whether any
  button is down right now; `AutoWalk.standDownReason` is the pattern, and it stands down for the
  whole span of the hold.
- `mouseUp` should come a frame or so after `mouseDown`, not in the same burst — the title screen
  dropped burst clicks; `LoginSequence` splits them across emission slots for that reason.
- No right-click, no mouse-wheel, no scroll: not posted, not derived.

---

## `Widgets` — the interface, by covered rectangle

Interface components identified by what they *cover*, so a button can be clicked at its own centre
instead of at a pixel somebody measured once on one window size.

```java
List<Widgets.Component> all = Widgets.loaded(Widgets.DEFAULT_MAX);   // whole tree — NOT per-frame
Widgets.Component c = Widgets.byId(id);                              // one packed id
Widgets.Component hit = Widgets.smallestContaining(all, x, y, 0, 0); // what covers this point?
c.centreX()/centreY()   // where to click
c.contains(px, py)      // half-open rectangle test
c.describe()            // "549:12 3,17 134x30 shown \"...\"" — for logs
```

**Guarantees**

- Sizes are trustworthy (verified live). A component's rectangle identifies it: resolve once from
  a known-good coordinate, keep the packed id, and from then on click the middle of whatever that
  component currently measures — window size stops mattering.
- `smallestContaining` returns the innermost covering component (least area wins, because the
  point is inside the button *and* every container above it), never a hidden one, and
  `maxW`/`maxH` (0 = unbounded) throw out full-canvas containers that would otherwise win.
- Every parse failure degrades to "found nothing" — `null`/empty list, never an exception on the
  frame thread. A mismatched DLL cannot crash a plugin through this class.
- Everything below the pure line (`parse`, `parseLine`, `smallestContaining`) has no natives and
  is fully unit-testable; `PlayButtonTest` drives it without a game.

**Known limitations**

- **Component position is not settled.** Whether stored x/y are canvas-absolute or parent-relative
  is unverified on this build, and there is no parent link to accumulate through. So every
  position-based answer is a *candidate* a caller must cross-check against something it already
  knows — which is exactly what `smallestContaining` is shaped for: hand it a point known to work
  and it hands back the covering component. If positions turn out to be parent-relative, the
  search comes back empty and the caller keeps its own coordinate; it never silently clicks
  elsewhere.
- **Not for the per-frame path.** `loaded(max)` walks the whole interface tree in the DLL. Resolve
  once, or from an explicit action — never every frame.
- Sprite components (the play button is one) carry no text; `text` is `""` for them, so find them
  by geometry, not label.

---

## Quick reference — "I want to…"

| I want to… | Call |
|---|---|
| Know if I'm in the world | `Game.ready()` |
| Get my position | `Game.me().worldX()/Y()` or `.location()` |
| Find the nearest cow | `Npcs.nearestWithin(10, 2790)` |
| Count nearby players | `Players.countWithin(8)` |
| My attack level (base / boosted) | `Skills.level(Skill.ATTACK)` / `Skills.effective(...)` |
| Current / max HP | `Game.me().health()` / `.maxHealth()` |
| Draw a box on an NPC | `Npcs.withId(id)` → `e.screen()` → draw in `render()` |
| Mark a tile | `Game.tileOutlineWorld(x, y)` or `e.tileOutline()` |
| Walk somewhere | `Actions.walkTo(x, y)` (check the boolean) |
| Walk with a status line | `Actions.walk(x, y)` → switch on `WalkResult` |
| Click a tree | `Actions.object(treeId, x, y)` — *no-op until DO_ACTION lands* |
| Attack an NPC | `Actions.npc(entity)` — *no-op until DO_ACTION lands* |
| Type a username | `Input.typeChar(c)`, paced across frames — see `LoginSequence` |
| Click a UI button reliably | `Widgets.byId(id)` → `centreX()/centreY()` → `Input.click` |
| Am I idle? | `Game.me().isIdle()` — *wait a beat after acting* |

---

## What is not in `kewl.api` on purpose

- **Inventory, bank, ground items, chat, quests, trade** — none of those offsets are derived yet;
  nothing here half-works at them.
- **Network, packets, protocol** — by design, permanently. The game builds and sends its own
  packets from the action function; this client will never contain a protocol table.
- **Other players' names and combat levels** — offsets not derived (see `Players`); a real
  contribution, listed in the README.
- **A `Stats` facade with per-skill getters** — `Skills.level(Skill.ATTACK)` is the spelling; if
  you want `Skills.attack()`, it is a three-line addition to `Skills.java`.

Drawing helpers (`Hud.entityBox`, and friends) live in `kewl.ui` and are documented by their own
javadoc; plugin lifecycle and settings are in [plugin-system.md](plugin-system.md).
