# NPC render-path reverse engineering — Windows client 240-7

Status: analysis only. No production native reads, model snapshot API, hull renderer, or renderer hook has been added.

This document records the current IDA findings for the eventual goal of replacing the prism approximation used by `Actor.getConvexHull()` with a model-derived projected hull or outline.

## 1. Current objective

The desired eventual path is:

```text
NPC/entity
  -> current render component
  -> current animated/renderable object
  -> geometry consumer
  -> transformed/projected points
  -> ImGui hull or outline
```

The existing Java API exposes `NpcType.getModelId()`, but that is definition metadata and not the current animated model. The existing `Actor.getConvexHull()` is a prism approximation built from actor position, footprint, and a guessed logical height.

MobileOSRS and the r216 debug client are semantic references only. Their layouts and addresses are not used for the Windows client.

## 2. Confirmed scene traversal

The high-level scene pass is:

```text
sub_140073A90
  -> sub_1400750D0
  -> sub_140063BC0
```

`sub_140073A90` invokes `sub_1400750D0` for several entity-ID arrays stored in scene/world state. It also invokes `sub_140063BC0` for individual actor/entity updates.

### `sub_1400750D0`

Address:

```text
0x1400750D0
```

This function:

1. Iterates an integer entity-ID array.
2. Looks up each ID in a hash table.
3. Obtains two related pointers from the hash node.
4. Validates the entity's definition and scene bounds.
5. Reads fine actor coordinates and footprint/state fields.
6. Calls the scene insertion path.

Important observed accesses:

```asm
mov rdi, [hashNode+8]
mov rbx, [hashNode+16]
mov rax, [rbx+730h]
call sub_1405F54D0
mov eax, [rbx+268h]
mov ecx, [rbx+26Ch]
...
call sub_14069EC60
```

The object in `rbx` is strongly correlated with the existing NPC/entity object because it has the known definition pointer at `+0x730`, fine position fields, footprint data, and is passed into the same scene path used by actor processing.

### `sub_140063BC0`

Address:

```text
0x140063BC0
```

This is an alternate/individual actor scene path. It performs a similar hash lookup and then uses the entity object in the scene submission path.

Important behavior:

```cpp
renderComponent = *(entity + 0x7E0);       // observed in the actor path
fineX          = *(entity + 0x268);
fineY          = *(entity + 0x26C);
sceneObject    = *(entity + 0x7E0-related state);
```

The function also reads a separate pointer at approximately `entity + 0x7E0`/nearby render state and passes shared render state into `sub_14069EE90` or `sub_14069EC60`. The exact ownership relationship between the entity field and the generic render wrapper described below still requires confirmation; it must not yet be treated as a production model pointer.

## 3. Entity/NPC provenance

The entity object used by the scene path is supported by multiple independent observations:

| Evidence | Address/field | Confidence |
|---|---:|---|
| NPC definition pointer | `entity + 0x730` | Confirmed |
| Fine X/Y used by scene logic | `entity + 0x268`, `entity + 0x26C` | Confirmed in scene path |
| Existing binding-derived scene coordinates | `entity + 0x3F0`, `entity + 0x418` | Confirmed by existing resolver/bindings |
| Footprint/size-related accesses | around `entity + 0x278` | Strongly supported |
| Render-state/component-related access | around `entity + 0x7E0` and `entity + 0x7E0`-adjacent state | Strongly suspected; lifecycle still unresolved |

The `+0x730` definition pointer is not itself a renderable. It is used for definition/model metadata and validation.

## 4. Scene submission chain

The confirmed scene submission chain is:

```text
sub_140063BC0 / sub_1400750D0
  -> sub_14069EE90 or sub_14069EC60
  -> sub_14069F000
  -> scene-cell insertion
```

### `sub_14069EC60`

Address:

```text
0x14069EC60
```

This function receives actor/world coordinates, dimensions, flags, and a managed shared object. It computes tile bounds and forwards the object to `sub_14069F000`.

It performs coordinate/tile work, including division by 128, but does not expose model vertices.

### `sub_14069EE90`

Address:

```text
0x14069EE90
```

This is a closely related wrapper around `sub_14069F000`. It receives a managed pair and forwards it to the same scene-cell machinery.

### `sub_14069F000`

Address:

```text
0x14069F000
```

This function:

- validates scene bounds;
- creates or reuses a scene render wrapper;
- stores tile bounds and scene coordinates;
- stores transform/descriptor data;
- attaches the managed shared object;
- inserts the result into scene-cell lists.

It is scene submission infrastructure. It does not directly traverse model vertices.

### `sub_14069F6A0`

Address:

```text
0x14069F6A0
```

This is another scene-cell insertion function used from `sub_1400757A0`. It also stores coordinates, dimensions, and managed render objects into scene structures. It has not produced a vertex source.

## 5. Generic render wrapper: `sub_14002BB40`

Address:

```text
0x14002BB40
```

The function is a generic interface wrapper, conceptually:

```cpp
if (!object->cachedObject)
{
    object->cachedObject =
        object->interface->vtable[3](
            object->interface,
            object + renderState);
}

return object->interface->vtable[1](
    object->interface,
    object->cachedObject,
    ...);
```

The decompiler shows:

```cpp
if ( !*(_QWORD *)(a1 + 32) )
    *(_QWORD *)(a1 + 32) =
        (*(__int64 (__fastcall **)(_QWORD, __int64))(**(_QWORD **)(a1 + 24) + 24LL))(
            *(_QWORD *)(a1 + 24),
            a1 + 40);

return (*(__int64 (__fastcall **)(...))(**(_QWORD **)(a1 + 24) + 8LL))(
    *(_QWORD *)(a1 + 24),
    *(_QWORD *)(a1 + 32),
    ...);
```

Therefore the generic wrapper fields are:

```text
wrapper +0x18  -> interface/object pointer
wrapper +0x20  -> cached object returned by interface slot 3
wrapper +0x28  -> render-state/auxiliary storage passed to slot 3
```

The virtual calls are:

```text
interface vtable +0x18 / slot 3
    returns or creates the cached object

interface vtable +0x08 / slot 1
    consumes/submits the cached object
```

### Important qualification

`sub_14002BB40` is demonstrably generic. Its direct callers include multiple subsystems, and it is not yet proven that every `wrapper +0x18` instance is the same object as the NPC entity's `+0x7E0` field. The NPC relationship is strong enough to prioritize, but not strong enough to justify production reads.

## 6. Vtables observed so far

### Render wrapper vtables

`sub_14069F000` initializes scene wrapper objects with:

```text
off_140C000F8
off_140C000E8
off_140C000C8
```

Observed entries include:

```text
off_140C000E8:
  slot 0 -> 0x140699290
  slot 1 -> 0x14002BC20
  slot 2 -> 0x14068ED90
  slot 3 -> 0x14068EDE0
  slot 5 -> 0x14063FCB0
  slot 6 -> 0x14063FBB0
  slot 8 -> 0x14063FAB0
  slot 9 -> 0x14063F9B0
```

These are scene/render-wrapper methods, not yet the concrete interface behind the generic `sub_14002BB40` wrapper.

### `off_140B93150` / `off_140B93160`

These appear in the construction/destruction of nested render/resource objects. They contain reference-list and lifetime behavior. They are not yet proven to be current NPC model vtables.

## 7. Render-wrapper virtual methods inspected

The methods at:

```text
0x14063FCB0
0x14063FBB0
0x14063FAB0
0x14063F9B0
```

read descriptor fields from nested objects and call:

```text
0x14065C570
0x14065C320
0x14065C150
0x14065BDE0
```

The arguments include:

- tile/scene dimensions;
- transform matrices or 4x4-like data;
- flags;
- material/descriptor values;
- integer coordinate values.

The inspected methods do not directly reveal a bounded vertex-count/pointer pair. They look like scene primitive submission and transform dispatch rather than an accessible CPU model array.

## 8. Geometry status

No validated current model geometry has been found yet.

Not yet proven:

```text
current animated model pointer
vertex count
vertex X/Y/Z arrays
index/triangle arrays
animation-applied vertex buffer
model-local -> actor/world transform
```

The following are specifically not sufficient:

```text
NPC definition pointer +0x730
NpcType.getModelId()
sub_1405F7480 definition/cache lookup
scene-cell objects from sub_14069F000
```

Those provide metadata or scene submission state, not proven current geometry.

## 9. Coordinate-space findings

The scene path uses fine coordinates and converts to tiles with 128-unit shifts/division:

```cpp
tileX = fineX / 128;
tileY = fineY / 128;
```

The existing project `WORLD_TO_SCREEN` helper is already resolved and working for the approximation path. It should be reused only after the model data's coordinate space is proven.

A future model hull will need one of these validated inputs:

```text
animation-transformed model-local vertices + actor orientation + fine translation
```

or:

```text
already scene/world-transformed vertices
```

No such vertex source has been identified yet.

## 10. Confirmed vs suspected vs unknown

### Confirmed

- `sub_140073A90` orchestrates scene/entity passes.
- `sub_1400750D0` iterates entity IDs and submits entity-associated render state.
- `sub_140063BC0` is an actor/entity-specific scene path.
- Entity definition and coordinate fields are used in the render path.
- `sub_14069EC60`, `sub_14069EE90`, and `sub_14069F000` are scene submission wrappers.
- `sub_14002BB40` is a generic lazy-render-object/interface wrapper.
- Generic wrapper slot 3 is at vtable offset `+0x18`.
- Generic wrapper slot 1 is at vtable offset `+0x08`.
- The generic wrapper caches the slot-3 result at `+0x20`.

### Strongly suspected

- The NPC entity has a render-component/state relationship around `+0x7E0`.
- The managed object passed from the NPC path is related to the generic render wrapper.
- The slot-3 result is a renderable/resource wrapper rather than raw geometry.
- The rendering architecture uses CPU-side scene preparation before later renderer submission.

### Unknown

- The concrete vtable stored in the NPC-associated component's interface field.
- The concrete slot-3 implementation used for NPCs.
- Whether slot 3 returns animated geometry, a render command, or another wrapper.
- The concrete slot-1 implementation used for NPCs.
- Whether `component +0x20` is actor-specific or shared by type.
- The first function that consumes vertex/index data.
- Whether usable vertices remain available after animation or are uploaded to GPU buffers.

## 11. Additional findings from the virtual-wrapper pass

### `sub_14002BC60` is not yet the NPC interface implementation

The constructor at:

```text
0x14002BC60
```

is called directly from `sub_14069F000`, the scene-cell wrapper. It initializes:

```text
object +0x00 -> off_140B93160
object +0x20 -> off_140B93150
object +0x18 -> 0 initially
```

The `off_140B93160` table has pure-virtual entries in the slot positions that would be needed to identify the NPC interface. Therefore this constructor is a scene/resource wrapper or abstract base construction, not proof of the concrete NPC interface behind `sub_14002BB40`.

This prevents an unsafe conclusion that `off_140B93160` is the NPC renderable vtable.

### Direct callers of `sub_14002BB40`

The wrapper is used by several unrelated-looking systems:

```text
0x14002BE90
0x1400699290
0x14006BB8A0
0x14006BBB90
0x14006BBE10
0x14006BC010
0x14006BC190
0x14006BC390
```

The `0x14006BBxx` callers operate on resource/file-like objects and call virtual slot `+0x10` with paths or resource metadata. They are not NPC render proof.

`0x1400699290` also uses the same wrapper shape but its surrounding logic is resource/render-state management. It cannot be assumed to be the NPC path without an object provenance link.

The only direct constructor reference found for `sub_14002BC60` is from `sub_14069F000`, further separating that constructor from the unresolved NPC interface.

### `entity +0x7E0` lifecycle evidence

The current IDA scan found these relevant direct accesses:

```text
0x14002C8E0  initializes entity +0x7E0 to zero
0x140031460  writes zero to +0x7E0
0x1400582B0  touches a different offset (`+0x47C4` / decimal `18324`), not actor `+0x7E0`
0x140063BC0  reads +0x7E0 during actor processing
0x1400A3C70  clears `+0x7E0` in a separate reset/update path
0x140190650  numeric-offset hit not yet actor-proven; excluded from lifecycle conclusions
```

Because `+0x7E0` also occurs in unrelated object types, its NPC meaning is supported primarily by the read in `sub_140063BC0`, not by the numeric offset alone. The next required proof is the construction/update function that writes the nonzero NPC render component and the concrete vtable of the object stored there.

## 12. Exact next IDA targets

The next investigation should remain narrowly focused on the generic wrapper and its concrete interface:

```text
sub_14002BB40
  callers:
    0x14002BE90
    0x1400699290
    0x14006BB8A0
    0x14006BBB90
    0x14006BBE10
    0x14006BC010
    0x14006BC190
    0x14006BC390
```

Next steps:

1. Decompile/classify each caller and identify the actual object passed as `a1`.
2. Trace construction of the object at `a1 + 0x18`.
3. Find constructor writes of its concrete vtable.
4. Devirtualize vtable slot 3 (`+0x18`).
5. Track its returned pointer into the cached field at `a1 + 0x20`.
6. Devirtualize slot 1 (`+0x08`) using the same concrete interface.
7. Follow the cached object until a real count/pointer geometry loop or GPU upload is reached.
8. Validate the path against NPC provenance before considering any snapshot API.

## 13. Narrow virtual-call investigation — current result

The latest decompilation materially narrows the interpretation of the earlier `sub_14002BB40` hypothesis.

### `sub_14002BB40` is a generic resource/interface helper, not yet an NPC render class

Its exact body is:

```cpp
if (!wrapper->cached)
    wrapper->cached = wrapper->interface->vtable[3](wrapper->interface,
                                                     wrapper + 0x28);

return wrapper->interface->vtable[1](wrapper->interface,
                                     wrapper->cached,
                                     ...render/resource arguments...);
```

The offsets in this function are confirmed as:

```text
wrapper +0x18  interface pointer
wrapper +0x20  lazy cached result
wrapper +0x28  state passed to slot 3
vtable +0x08   slot 1
vtable +0x18   slot 3
```

However, its callers are heterogeneous. The direct callers at `0x1406BB8A0`, `0x1406BBB90`, `0x1406BBE10`, `0x1406BC010`, `0x1406BC190`, and `0x1406BC390` operate on file/resource-like objects and pass path/descriptor data. They are not evidence of the NPC render implementation. `0x14002BE90` is a thin generic caller, and `0x140699290` is resource/render-state management. Therefore no concrete NPC vtable, slot-1 target, or slot-3 target has been proven from this helper.

### The NPC scene path uses a different nested component shape

In `sub_140063BC0` (`0x140063BC0`), the object obtained from the scene hash is held in `v11`. The relevant path is:

```cpp
// v11 is the object correlated with scene/entity state in this function
component = v11 + 0x40;
interface  = *(component + 0x18);
// interface virtual calls occur through component + 0x18
// cached/auxiliary values occur at component + 0x20 and nearby fields
```

The decompiler shows the following cleanup/invalidation sequence:

```cpp
if (v11->field96)
{
    interface->vtable[4](interface);
    component->field32 = 0;
}

// unlink component->field8 from its intrusive list
```

The same function also invokes `sub_1400A5350(v11)`, `sub_1400A4AD0(v11 + 608)`, `sub_1400A5410(v11, ...)`, and `sub_14009A1F0(...)` before calling the scene insertion wrappers. These are higher-value targets than the unrelated resource callers because they are reached after the entity hash lookup and use the same object whose fields include:

```text
v11 +0x268 / +0x26C  fine position
v11 +0x278          footprint/state-related data
v11 +0x730          definition-related field in the broader entity path
v11 +0x7E0          previously correlated render/state field, still requiring object-identity proof
v11 +0x40           nested scene/render component candidate in this path
```

The apparent `+0x40` component relationship must be treated as a function-local object interpretation until the constructor and allocation origin of `v11` are proven. It is not safe to collapse it into the earlier `entity + 0x7E0` description yet.

### Scene submission does not consume geometry in the inspected function

The actor path calls:

```text
sub_1400A5410(v11, temporary_state)
sub_14009A1F0(...)
sub_14069EE90(...)
sub_14069EC60(...)
```

The last two functions perform scene/tile insertion and managed-object lifetime handling. They receive fine coordinates, dimensions, flags, and shared objects, but the inspected bodies do not contain a bounded vertex count/pointer loop. `sub_1400750D0` follows the same scene submission family.

This establishes the current best supported chain only as:

```text
scene hash lookup
 → v11 actor/entity-associated object
 → v11 + 0x40 nested component candidate
 → component interface virtual calls / invalidation
 → sub_1400A5410 and sub_14009A1F0
 → sub_14069EE90 or sub_14069EC60
 → scene-cell insertion
```

The link from the component's virtual result to model geometry remains unresolved.

### Vtable/constructor correction

`sub_14002BC60` (`0x14002BC60`) is not the NPC interface constructor. It is called from `sub_14069F000` while building scene-cell wrappers. It writes:

```text
object +0x00 -> off_140B93160 initially
object +0x20 -> off_140B93150
```

and the caller subsequently overlays scene wrapper tables:

```text
object +0x00 -> off_140C000F8
object +0x20 -> off_140C000E8
object +0x238 -> off_140C000C8
```

Those tables implement scene-wrapper operations. They should not be used as the NPC's current model vtable.

The only concrete constructors currently found for the unrelated `sub_14002BB40` interface family are resource/global initialization routines such as `sub_1400265E0` and `sub_140029AE0`; their vtables (`off_140C0E628`, `off_140BF8308`) are not proven to populate the NPC scene component.

## 14. Current investigation status

### Confirmed

- `sub_140063BC0` is reached from the actor/entity scene path and performs entity-hash lookup, bounds/visibility work, coordinate extraction, and scene submission.
- `sub_14002BB40` has lazy slot-3/slot-1 dispatch with cached result at `+0x20`, but is generic.
- `sub_14069EE90` and `sub_14069EC60` are scene submission wrappers, not proven geometry consumers.
- `sub_14069F000` constructs scene-cell wrapper objects with `off_140C000F8`, `off_140C000E8`, and `off_140C000C8`.
- The current decompiled scene path still contains no validated vertex count + vertex storage relationship.

### Strongly suspected

- `sub_1400A5410` and `sub_14009A1F0` are closer to the actor render-state/model preparation boundary than the scene-cell functions.
- The nested component at `v11 + 0x40` is an actor-associated render/state wrapper in this particular path.
- The render architecture prepares an opaque managed scene object before scene insertion, rather than passing raw vertices directly through `sub_14069EC60`.

### Unknown

- The concrete vtable stored at the NPC-path component's interface field.
- Which virtual slot produces the current actor render object.
- Whether the produced object is a renderable, animated model, scene command, or managed resource.
- The first consumer of model vertices or GPU geometry.
- Whether CPU-side transformed vertices remain accessible.

## 15. Exact next IDA targets

The next pass should decompile and inspect these exact functions, in this order:

```text
0x1400A5410  sub_1400A5410
0x14009A1F0  sub_14009A1F0
0x1400A5350  sub_1400A5350
0x140063BC0  callers that create/populate v11
0x1400582B0  only if it is proven to construct the same v11 object
```

For each, trace:

1. The origin of `v11` and the object at `v11 + 0x40`.
2. Any vtable write to the component/interface pointer.
3. The exact virtual slot and target used on that object.
4. Whether the result is forwarded into `sub_14069EE90/EC60` or into another render function.
5. Any count-bounded pointer arithmetic or transform loop.

Only after those functions establish a live model/renderable provenance should the investigation proceed to vertex fields. No production snapshot, native memory reader, or ImGui model hull has been added.

## 16. Latest Windows 240-7 pass: `actor + 0x728` lifecycle

This pass specifically followed the field checked by `sub_1400A5350` and corrected its interpretation. The field is at decimal offset `1832`, i.e. hexadecimal `0x728`.

### Confirmed presence check

```cpp
bool __fastcall sub_1400A5350(void* actor)
{
    return *(void**)((char*)actor + 0x728) != nullptr;
}
```

This proves only that the field is a nullable actor-associated resource/state pointer. It does not prove that it is a current model or renderable.

### Confirmed non-null assignment

A meaningful write occurs in:

```text
0x14009B740  sub_14009B740
```

The relevant packet/state-decoding branch is guarded by a flag bit (`v16 & 8`). It reads a two-byte resource/definition ID from the input stream, resolves it through `sub_1405F3550`, and assigns the resulting managed object to the actor field:

```cpp
uint16_t id = read_u16_with_client_encoding(stream);
SharedObject resolved;
sub_1405F3550(&resolved, id);
sub_14003F330((char*)actor + 0x728, resolved);
```

The decompiler's exact sequence is:

```cpp
v212 = sub_1405F3550(&v281, decodedId);
sub_14003F330(v13 + 1832, v212);
```

The surrounding cleanup decrements the shared object's reference counts and invokes vtable slots at `+0x08` and `+0x10` when the last reference is released. This establishes managed/shared ownership rather than a raw vertex pointer:

```cpp
if (resolved)
{
    release_control_block(resolved + 8);
    if (last_reference)
    {
        resolved->vtable[1](resolved);
        release_control_block(resolved + 12);
        if (last_owner)
            resolved->vtable[2](resolved);
    }
}
```

The exact implementation behind `sub_1405F3550` was not yet decompiled in the saved IDA output, so the resource's concrete class/vtable is still unknown. However, the input is a compact definition/resource ID and the object is acquired through the client's shared-resource lookup path. That is strong evidence against treating `+0x728` as actor-specific animated geometry.

### Clearing/reset behavior

A separate function at:

```text
0x1400A3C70  sub_1400A3C70
```

writes zero to the same field. This is consistent with actor/resource reset or replacement, not evidence of model geometry storage.

Other numeric `+0x728` hits were rejected as unrelated object layouts:

- `0x140058590` uses offset `1832` for a SQL/schema string (`"ID"`), not the actor object.
- `0x1400582B0` initializes a much larger unrelated object and touches `18324`, not `1832`.

Therefore the actor-provenance lifecycle currently established is:

```text
actor/state update at 0x14009B740
  -> decode resource/definition ID
  -> sub_1405F3550 lookup
  -> sub_14003F330 shared assignment to actor + 0x728
  -> sub_1400A5350 tests presence during scene processing
  -> sub_1400A3C70 can clear/reset the field
```

### What happens after the positive check

In `sub_140063BC0`, after the hash lookup identifies the actor object `v11`, the positive path is:

```cpp
if (!sub_1400A5350(v11) || v11->stateByte1992)
    release_or_unlink_actor_component(v11);
else
{
    int planeOrLayer = sub_1400A4AD0(v11 + 608);
    // culling/scene tile checks
    // fine coordinates at +616/+620
    // optional bounds rectangle and terrain sampling
    // sub_14069EE90 or sub_14069EC60 scene insertion
}
```

The positive branch does not dereference `v11 + 0x728` as a model, does not call a model accessor on it, and does not expose a vertex count or geometry pointer. It uses the presence result to permit scene placement. The actual scene path continues through coordinate/terrain and managed scene-wrapper handling.

Notably, the current saved decompilation does not show a call from the `+0x728` object into a current-model selector. This eliminates `+0x728` as the leading model-selector hypothesis unless a separate consumer is found elsewhere.

### Current classification

| Item | Finding | Confidence |
|---|---|---|
| `actor + 0x728` | Nullable managed resource/definition handle | High |
| Non-null source | `sub_14009B740` decodes an ID and calls `sub_1405F3550` | Confirmed |
| Ownership | Shared/control-block lifetime with refcount release | Confirmed |
| Used by `sub_1400A5350` | Presence/readiness gate before scene placement | Confirmed |
| Current animated model | Not proven; current evidence argues against it | High confidence negative |
| Vertex storage | Not found | Confirmed unknown |
| Concrete vtable | Not yet resolved | Unknown |

The best pivot is now the actor's other state fields and the first render-resource object passed into `sub_14069EE90/EC60`, rather than forcing `+0x728` to be a model pointer.

## 17. Updated investigation status and next targets

The `+0x728` result changes the model search direction:

```text
actor + 0x728
  -> packet/resource ID lookup
  -> managed definition/resource handle
  -> presence gate for scene placement
```

It is not currently a credible current animated-model pointer. The actor scene path reads the same field at `actor + 0x7E0` before constructing the state passed to `sub_14069EE90/EC60`. Decimal `2016` is only an alternate decompiler display; there is no separate `+0x2016` field.

### Confirmed

- `sub_14009B740` is an actor/state decoder that assigns `actor + 0x728` from a two-byte ID resolved by `sub_1405F3550`.
- `sub_14003F330` performs managed replacement of that field; the surrounding release sequence proves shared ownership/refcounting.
- `sub_1400A3C70` clears/resets the field.
- `sub_1400A5350` only tests field presence.
- The positive branch in `sub_140063BC0` uses the result for scene eligibility and then performs culling, terrain sampling, and scene insertion; it does not call a model accessor on the field.

### Strongly suspected

- `actor + 0x728` is a definition/resource readiness handle, not animated geometry.
- The current-model path is either behind the separately managed actor render/state component or in a renderer stage not reached by the scene-cell insertion functions.
- `actor + 0x7E0` is read as a pointer to a small descriptor-like value in `sub_140063BC0`; its first qword and a dword at pointed-object offset `+0x08` are copied into scene-call state. Its semantic type remains unknown.

### Unknown

- Concrete type/vtable returned by `sub_1405F3550`.
- Which actor field owns the animation wrapper/current model.
- Whether the scene descriptor created by `sub_1400C8F90` references CPU geometry, GPU resources, or only placement metadata.
- First vertex/count consumer and native highlight consumer.

### Exact next IDA targets (maximum three)

1. **`sub_1405F3550` and all actor-provenance callers** — decompile the lookup and identify the vtable/type of the object assigned to `+0x728`; this closes the definition/resource question without assuming it is a model.
2. **The `actor + 0x7E0` read and `sub_1400C8F90` call inside `sub_140063BC0`** — determine the pointed descriptor's construction, ownership, and consumers.
3. **`sub_1400757A0` reached after successful `sub_14069EE90/EC60` insertion** — inspect whether its per-actor list/descriptor path is the first non-terrain route into renderable/model submission.

No vertex layout, current-model selector, transform loop, or native outline path is proven yet.

## 18. Exact `+0x7E0` data flow and `sub_1400C8F90`

### Offset correction

The prior notes used both `2016` and `0x7E0`. They are identical:

```text
2016 decimal = 0x7E0
```

The relevant actor instruction is represented in the decompiler as:

```asm
mov rdx, [v11+7E0h]
```

and the corresponding decompiler expression is:

```cpp
v22 = *(__int64 **)(v11 + 0x7E0);
```

These do not identify two fields.

### Exact use in `sub_140063BC0`

The relevant data flow is:

```cpp
__int64* descriptor = *(__int64**)((char*)actor + 0x7E0);

if (descriptor)
{
    qwordState = descriptor[0];
    dwordState = *((uint32_t*)descriptor + 2); // pointed-object +0x08
}
else
{
    qwordState = 0;
    dwordState = 1; // together with the default 256/zero state
}

// The descriptor values are copied into local scene-call state.
// They are passed as the final auxiliary/state argument to EC60/EE90.
```

The field itself is not passed to `sub_1400C8F90`. The `sub_1400C8F90` input in this actor path is the managed pair held in locals originating from the hash node:

```cpp
// v63 = control/shared owner from the hash node
// v64 = actor/entity object
sub_1400C8F90(&scenePair, &v63);
```

Therefore the previously suspected direct chain:

```text
actor +0x7E0 -> sub_1400C8F90
```

is disproven for this call site. Both values contribute to scene submission, but through different arguments.

### `sub_1400C8F90` effective signature

The function is a two-word managed-pair copy helper:

```cpp
struct SharedPair
{
    void* control;
    void* object;
};

SharedPair* __fastcall sub_1400C8F90(
    SharedPair* out,
    const SharedPair* in)
{
    out->control = in->control;
    out->object  = in->object;

    if (out->control)
        InterlockedIncrement(
            (volatile LONG*)sub_140044D30(
                (char*)out->control + 8));

    return out;
}
```

Confirmed output layout:

```text
output +0x00  copied from input +0x00; control/owner pointer
output +0x08  copied from input +0x08; managed object pointer
```

The function has no model-specific logic, no animation selection, no transform, no vertex access, and no vtable dispatch other than the control-block helper `sub_140044D30`. It is a refcounted pair-copy constructor.

### Scene-submission slice

For the actor path the proven relationship is:

```text
hash node
  -> control pointer + actor/entity pointer
  -> local SharedPair
  -> sub_1400C8F90
  -> temporary SharedPair
  -> sub_14069EE90 / sub_14069EC60 argument a7
  -> sub_14069F000 scene-cell wrapper
```

Separately:

```text
actor +0x7E0
  -> descriptor pointer
  -> descriptor[0] and descriptor[2] copied to local auxiliary state
  -> final auxiliary arguments of sub_14069EE90/EC60
```

`sub_14069EE90` and `sub_14069EC60` copy/refcount the `a7` pair and forward it to `sub_14069F000`. The inspected path still does not dereference the managed object's contents as model geometry.

### `sub_1400757A0` classification

The saved decompilation shows `sub_1400757A0` walking the actor's intrusive list range:

```cpp
for (entry = *(a3 + 1776); entry != *(a3 + 1784); entry += 24)
{
    control = *(entry + 8);
    object  = *(entry + 16);
    if (control)
        increment_control_refcount(control + 8);

    // reads actor/entity state such as object + 620
    // prepares placement/descriptor values
    // calls sub_14069F6A0
    // releases the temporary shared reference
}
```

Its known downstream call is:

```text
sub_1400757A0 -> sub_14069F6A0
```

The inspected behavior is another scene-cell insertion/list-processing layer. No model vtable, vertex loop, batch traversal, or current-model selector has been proven there. It should not yet be treated as the renderer entry point.

### Current conclusion

`+0x7E0` is not resolved to an animation wrapper. At this call site it is a pointer to a descriptor-like object whose compact state is forwarded alongside, but separately from, the managed scene pair. `sub_1400C8F90` is generic shared-pair plumbing, not the model builder.

### `sub_1406BFAC0` correction

The vtable-reference result for `sub_1406BFAC0` was a false lead for scene rendering. The concrete vtable dump shows that this address is slot 1 of:

```text
off_140BFF348
```

which IDA identifies as an STL-style:

```text
std::_Ref_count_del_alloc<__ExceptionPtr, ...>
```

The surrounding slots are reference-count/deallocator routines such as:

```text
0x140048DC0
0x140044E40
0x1406C6180
0x1406C6170
0x1406C61F0
```

That is strong evidence that `sub_1406BFAC0` belongs to generic exception/reference-count destruction or allocation machinery, not a visible-scene traversal. The raw address references near the scene vtable scan were not sufficient to establish scene-wrapper provenance and are rejected as a model-path lead.

No actual scene-wrapper consumer has yet been identified. The relevant scene tables remain:

```text
off_140C000F8
  slots include 0x14063FCB0, 0x14063FBB0, 0x14063FAB0, 0x14063F9B0

off_140C000E8
  slots include 0x14063FCB0, 0x14063FBB0, 0x14063FAB0, 0x14063F9B0

off_140C000C8
  slots include the same scene descriptor/transform family
```

Those function addresses are table entries, not proof that they have been called by a later frame traversal. The next pass must locate indirect call sites on already-instantiated wrapper objects rather than relying on address references alone.

### Exact next IDA targets (maximum three)

1. **Indirect-call xrefs for `off_140C000E8`/`off_140C000F8` slots 0–10** — identify call sites that load a scene-wrapper vtable from an existing object and dispatch with camera/renderer state.
2. **`sub_14068EDE0`** — classify the common scene-wrapper type-switch/installation path and determine whether it only installs vtables or also invokes render methods.
3. **`sub_14063FCB0`, `sub_14063FBB0`, `sub_14063FAB0`, and `sub_14063F9B0` call sites** — follow the first caller carrying a scene-cell wrapper's retained object into these descriptor/transform methods.

## 19. Latest scene-wrapper pass

### `sub_14068EDE0` classification

Address:

```text
0x14068EDE0
```

This is a wrapper constructor/type-state setup function, not a frame renderer. Its decompiled behavior is:

```cpp
_QWORD* __fastcall sub_14068EDE0(_QWORD* object, char flags)
{
    object[0] = &off_140C000F8;
    object[4] =  off_140C000E8;
    object[71] = &off_140C000C8;

    if (object[70])
        object[70]->vtable[6](object[70]); // +48 byte slot

    sub_14003E580(object + 72);

    object[4] = off_140B93150;
    unlink_and_clear(object + 5);

    if (object[7])
        object[6]->vtable[4](object[6]); // +32 byte slot

    if (flags & 1)
        sub_140AFC28C(object, 600);

    return object;
}
```

Confirmed behavior:

- installs the three scene-wrapper tables;
- releases/unlinks nested intrusive-list state;
- calls cleanup virtuals on nested objects;
- resets one embedded resource/list table to `off_140B93150`;
- optionally frees a 600-byte wrapper allocation.

There is no visible scene-cell traversal, camera argument, renderer context, draw queue, vertex access, or model selection in this function.

**Classification:** wrapper construction/reset/destruction support. **Render relevance:** indirect only; not a proven renderer entry point.

### Scene-wrapper method semantics from the saved decompilation

The table methods remain descriptor-to-render-dispatch helpers, but no proven scene-cell caller has yet been found.

`0x14063FCB0` reads a nested descriptor at `wrapper + 0x28`, extracts flag-dependent byte/word values, copies a 4x4-like argument, reads wrapper fields at `+0x08`, `+0x0C`, `+0x10`, `+0x1C`, and `+0x20`, then calls:

```text
0x14065C570
```

`0x14063FBB0` performs similar flag extraction and calls:

```text
0x14065C320
```

`0x14063FAB0` reads a nested descriptor at `wrapper + 0x20`, copies matrix-like input, and calls:

```text
0x14065C150
```

`0x14063F9B0` reads the nested descriptor at `wrapper + 0x20` and calls:

```text
0x14065BDE0
```

These functions have renderer-shaped arguments and compact flags, but the current evidence proves only table membership and downstream dispatch. It does not prove that any call is made from a visible scene-cell traversal.

`0x140699290`, another table entry, invokes the generic `sub_14002BB40` helper using its own cached/interface fields. That is a potentially important resource-render path, but its receiver has not been proven to originate from an NPC scene cell.

### Scene-cell reader status

The currently saved IDA results prove writers:

```text
sub_14069F000
sub_14069F6A0
```

and their insertion-related callers, but do not yet identify a later reader loop that walks the populated scene-cell lists and dispatches a wrapper virtual method with camera/frame state.

No first proven indirect call on an already-instantiated scene wrapper has been established in this pass.

### SharedPair.object status

The insertion path still proves:

```text
hash-node SharedPair
  -> sub_1400C8F90
  -> sub_14069EE90 / sub_14069EC60
  -> sub_14069F000
```

The wrapper retains the pair and refcounts the control pointer. The actual `SharedPair.object` field has not yet been observed in a later frame traversal, so no current-model or batch interpretation is justified.

### Exact next IDA targets (maximum three)

1. **Readers of the scene-cell list fields written by `sub_14069F000` and `sub_14069F6A0`** — locate a real visible-cell/frame loop before following any virtual call.
2. **Indirect call sites for the wrapper tables' slots 0–10** — require receiver provenance from a populated scene-cell object, then record slot/arguments and camera/render context.
3. **`0x140699290` callers with proven scene-wrapper provenance** — determine whether its `sub_14002BB40` dispatch is the handoff from a scene wrapper to an opaque render resource.

## 20. Scene-cell storage map from insertion decompilation

The insertion decompilations provide the first concrete storage layout. This is storage provenance only; a later frame reader has not yet been proven.

### `sub_14069F000` primary cell list

For the tile selected by the function's coordinate calculations, the scene grid is selected from:

```text
scene +0x968   // decimal 2408: alternate/plane cell-array base
scene +0x980   // decimal 2432: second cell-array base in the other plane form
scene +0x948   // decimal 2376: row/plane dimension used in index arithmetic
scene +0x94C   // decimal 2380: column dimension
```

The decompiler's effective cell calculation is:

```cpp
cell = cellArray + 16 * cellIndex;
cellState = *(uint8_t**)(cell + 8);
```

The wrapper-containing list is inside `cellState`:

```text
cellState +0x38  -> entry-array/list storage pointer
cellState +0x34  -> entry count
```

For each insertion, the function computes:

```cpp
entry = (SharedPairEntry*)(cellState->entryStorage
                         + 16 * cellState->entryCount);
```

and writes:

```text
entry +0x00  <- retained control pointer
entry +0x08  <- scene wrapper object pointer
```

The wrapper object pointer is the allocated wrapper's inner object at allocation base `+0x20`, not the control block itself. The function also updates a per-entry flag/metadata array at:

```text
cellState +0xB0  // base inferred from [cellState + 176]
```

and increments the count at:

```text
cellState +0x34  // [cellState + 52]
```

The exact type names are intentionally omitted. The important proven reader target is:

```text
cellState->entryStorage + 16*i + 0x08
```

### `sub_14069F6A0` secondary cell list

This function resolves the same scene cell using the same scene grid fields and reads:

```text
cellState +0xF0  -> secondary list end/current pointer
cellState +0xF8  -> secondary list capacity/end bound
```

It stores entries as two qwords:

```text
secondaryEntry +0x00  <- retained control pointer
secondaryEntry +0x08  <- wrapper object pointer
```

When capacity is exhausted it calls:

```text
sub_1400D1C40(cellState + 0xF0, &temporaryPair)
```

Otherwise it appends directly and advances the end pointer by 16 bytes. After insertion it invokes a virtual method at:

```text
(wrapper object +0x40)->vtable[1]
```

The call is a list/resource notification or state operation in the available decompilation; its receiver is a nested wrapper field, not yet proven to be a model renderer.

### Common storage conclusion

Both insertion functions store a pair of:

```text
control pointer
wrapper object pointer
```

The primary and secondary lists appear to be separate scene-cell collections, but their later readers have not yet been identified. This materially narrows the reader search to these exact patterns:

```text
cellState + 0x38 + 16*i + 0x08
cellState + 0xF0-list entries + 0x08
```

### First-reader status

No saved IDA result yet proves a later function that walks either list in a camera/visibility/frame-render loop. Therefore there is still no proven indirect call on an already-instantiated scene wrapper.

The known wrapper table methods and `sub_140699290` remain candidates only until one of these storage paths is observed flowing into their receiver argument.

### Updated exact next IDA targets (maximum three)

1. **Readers of `cellState + 0x38` primary entries** — specifically loads from `entryStorage + 16*i + 8`, then classify whether the loop is visibility, cleanup, or rendering.
2. **Readers of the secondary list at `cellState + 0xF0`** — follow the stored wrapper pointer at each entry `+0x08` and identify any indirect vtable dispatch.
3. **The virtual call through `(wrapper + 0x40)->vtable[1]` in `sub_14069F6A0`** — determine whether this is only list bookkeeping or the first wrapper/resource consumer.

## 21. Production-code decision

No production code was changed in this investigation.

A `ModelSnapshot` or ImGui model hull must not be added until all of these are proven:

```text
concrete NPC interface vtable
slot-3 target
slot-1 target
cached-object identity
vertex source
vertex count and stride
coordinate space
animation/orientation state
bounded pointer validation
```

Until then, the safe fallback remains the existing prism approximation.

## 22. Windows actor-constructor audit against the r216 fingerprint

The available decompilation of:

```text
0x14002C8E0  sub_14002C8E0
```

does **not** show the r216-style inline AnimatedModel construction pattern.

### Constructor call map

The function performs extensive scalar/pointer initialization across the actor allocation. Its only visible nontrivial callees are:

```text
sub_14004D140(actor + 0xAF0, global_or_static_source)
sub_1400406C0(actor + 0xB80, 11)
```

There is no visible sequence of:

```text
lea rcx, [actor + candidateOffset]
call substantial BaseAnimatedModelCtor
mov [actor + candidateOffset], derivedVtable
mov [actor + candidateOffset + ownerField], actor
```

No substantial embedded constructor, derived-vtable overwrite, or owner-binding write was proven in this function. The function looks like a large plain actor-state initializer with selected container/helper initialization.

### `+0x7E0` in the constructor

The saved assembly includes:

```text
0x14002CEC7  mov [rcx+7E0h], rdi
```

This is **not a nonzero assignment**. The same constructor has already zeroed `edi`, and the instruction is part of a contiguous block clearing fields from `+0x780` through `+0x858`:

```text
[rcx+780h] = rdi
[rcx+790h] = rdi
...
[rcx+7D8h] = rdi
[rcx+7E0h] = rdi
[rcx+7F0h] = rdi
...
[rcx+858h] = rdi
```

Therefore `sub_14002C8E0` provides initialization-to-null evidence for `+0x7E0`, not a constructor-time model/resource assignment. The real non-null assignment must be located elsewhere, likely in an actor update/resource path.

### Helper-call classification

`sub_14004D140` is not an AnimatedModel constructor. Saved cross-references show it being used throughout the client to construct/copy short-string objects such as error text, service names, configuration keys, and URLs. It is a string/value helper and is rejected as the actor animation component.

`sub_1400406C0` is also generic container/storage initialization. The same helper is called with capacities such as `11`, `250`, and `1000` in unrelated large objects. Its use at `actor + 0xB80` does not provide a model/animation constructor fingerprint.

### Actor-relative region result

The constructor initializes many nearby regions, but the saved output does not provide a matched inline component constructor/destructor pair with r216's behavior. In particular, it does not yet prove a cluster containing:

```text
animation state provider
separate default/override model handles
current-model replacement
owner callback binding
```

This is a negative result for `sub_14002C8E0`, not evidence that Windows has no inline component. The component may be initialized by a later actor-type constructor, factory, update routine, or a helper called through one of the two generic initialization functions.

### Updated Windows targets from this audit

The highest-value follow-up is now the provenance of the two helper calls and the `rdi` source at `0x14002CEC7`, followed by actor animation-update routines. `sub_14002C8E0` itself should not be treated as the Windows AnimatedModel constructor unless those helpers reveal the missing component construction.

## 23. Windows caller/hierarchy audit after rejecting `sub_14002C8E0` as AnimatedModel construction

### Caller hierarchy currently proven

The only saved direct actor-initialization bridge is:

```text
callers
  → 0x140048590  sub_140048590
      mov edx, [rdx]
      jmp sub_14002C8E0
  → 0x14002C8E0
```

`sub_140048590` is a seven-byte thunk that forwards the second argument by loading `[rdx]` into `edx`; it does not add a post-construction component layer itself.

The saved xref set for this thunk is:

```text
0x1400373BD
0x14019EA3E
0x14019EE34
0x14019F424
0x14019F79C
0x1405B6884
0x1405CCFD3
```

The available saved output does not contain complete decompilations for those callers, so their actor-type classification and post-thunk instruction sequences remain unproven. They are the correct next constructor-layer targets; the thunk and base initializer should not be treated as sufficient NPC/PathingEntity constructor evidence.

### Embedded-constructor result

`sub_14002C8E0` itself contains only two nontrivial helper calls:

```text
sub_14004D140(actor + 0xAF0, ...)
sub_1400406C0(actor + 0xB80, 11)
```

The surrounding assembly is mostly direct scalar/pointer initialization. No substantial interior-pointer constructor call, derived-vtable overwrite, or owner-binding write was proven inside it.

The two helpers are rejected:

```text
sub_14004D140
  → generic short-string/value construction

sub_1400406C0
  → generic container/storage initialization
```

### `+0x7E0` source correction

The write at `0x14002CEC7` is one instruction in a zero-fill block. `rdi` is zero and the adjacent writes cover a broad run of actor fields. It is therefore an initialization/reset write, not an unresolved nonzero source assignment.

The actual later non-null assignment observed in the separate update decoder remains:

```text
sub_14009B740
  → decode resource/definition ID
  → sub_1405F3550
  → sub_14003F330(actor + 0x728, ...)
```

That is `+0x728`, not `+0x7E0`. The current evidence still does not identify a nonzero `+0x7E0` assignment connected to AnimatedModel state.

### Destructor symmetry status

No matched Windows actor destructor/embedded-component destructor pair has yet been proven from the saved IDA results. In particular, there is no confirmed reverse-order cleanup sequence corresponding to an actor-owned animation state provider, default/override model handles, or particle/model resource cache.

This is a significant negative result: the Windows AnimatedModel-like component has not yet been located through the common actor constructor or its teardown.

### Animation-update status

The saved results do not yet provide a proven actor animation update call graph of the form:

```text
sequence/frame write
  → actor-relative component
  → model/resource state update
```

The next pass should inspect the seven real callers of `sub_140048590` first, then shared NPC/player sequence-update routines. This preserves actor provenance without returning to scene insertion.

### Exact next targets (maximum three)

1. **The seven callers of `sub_140048590`** — decompile each caller's post-thunk instructions and classify base actor/NPC/player construction; record any interior-pointer constructors and vtable writes.
2. **The actor teardown/reset callers corresponding to those constructor paths** — map reverse-order cleanup and identify any substantial embedded object.
3. **Shared NPC/player sequence-update routines** — trace sequence/frame writes into subsequent helper calls and identify actor-relative component arguments.

No production code was changed and no Windows layout was inferred from r216.

## 24. Seven-caller follow-up status

The saved IDA corpus currently contains the direct thunk relationship and caller addresses, but not complete decompilations for the seven caller bodies. The proven hierarchy remains:

```text
0x1400373BD
0x14019EA3E
0x14019EE34
0x14019F424
0x14019F79C
0x1405B6884
0x1405CCFD3
    → sub_140048590
        mov edx, [rdx]
        jmp sub_14002C8E0
```

`sub_140048590` itself has no post-call code because it tail-jumps into `sub_14002C8E0`. Consequently, actor type, allocation source, final vtable, and post-initialization components must be recovered from the seven callers, not from the thunk.

No saved evidence currently proves that these callers construct NPCs, players, or a shared pathing-actor type. No interior-pointer constructor, derived actor-vtable installation, owner binding, or destructor symmetry has been established from them yet.

The two helper calls inside `sub_14002C8E0` remain rejected as AnimatedModel construction:

```text
sub_14004D140 → generic short-string/value helper
sub_1400406C0 → generic container/storage helper
```

The next analysis must decompile the seven caller functions and inspect instructions after the thunk call. This is a strict provenance gap, not a reason to infer a Windows layout from r216.

### Exact next targets (maximum three)

1. **The seven caller function bodies listed above** — recover allocation, post-thunk initialization, actor vtable, and registry/list insertion.
2. **The final actor-vtable destructor slots discovered from those callers** — map reverse-order embedded cleanup and find substantial animation/resource subobjects.
3. **Shared NPC/player sequence-update callers** — follow confirmed sequence/frame writes into subsequent actor-relative helper calls.

No production code was modified.

## 25. LostCity r216: `PathingEntityAnimatedModel` / `AnimatedModel`

This section records the detailed actor/model relationship from the r216 reference. Its addresses, offsets, vtables, and sizes are semantic guidance only and must not be copied into Windows 240-7.

### PathingEntity ownership and placement

The r216 `PathingEntity` constructor is `0x1003EFF50`. After base entity, movement, route, and facing initialization, it constructs an animated-model component inline in the entity:

```cpp
PathingEntityAnimatedModel* animated =
    (PathingEntityAnimatedModel*)((char*)this + 0x360);

PathingEntityAnimatedModel::PathingEntityAnimatedModel(
    animated, core, assetBuilder, config, loopMode,
    false, callback, *this);

PathingEntityModels::PathingEntityModels((char*)this + 0xCA8);
PathingEntityModels::PathingEntityModels((char*)this + 0xCD8);
```

The semantic relationship is:

```text
PathingEntity
  ├─ inline PathingEntityAnimatedModel
  │    └─ AnimatedModel base state
  ├─ separate PathingEntityModels build-request components
  ├─ model/render matrices
  ├─ fine-coordinate state
  └─ animation/overlay/spot-animation state
```

The animated-model component is not simply a separately allocated pointer stored in the actor. The constructor passes the owning `PathingEntity` into the component, allowing animation callbacks/model preparation to access actor state.

### PathingEntityAnimatedModel constructor

The meaningful r216 constructor body is `0x1003FE9D0`; `0x1003F0610` is a wrapper/thunk. The body:

```cpp
game::AnimatedModel::AnimatedModel(this, ...);
*(void***)this = derivedAnimatedModelVtable;
*(uint64_t*)((char*)this + derivedTailField) = callbackOrOwnerBinding;
```

This gives the useful fingerprint:

```text
PathingEntity constructor
  → inline PathingEntityAnimatedModel constructor
  → AnimatedModel base constructor
  → derived vtable replacement
  → owner/callback binding in derived state
```

### AnimatedModel constructor and ownership

`jag::game::AnimatedModel::AnimatedModel` is `0x100666C90`. It initializes an actor-local state machine containing:

```text
asset/config dependencies
animation-wrapper LRU cache
shared AnimationState provider
animation-loop behavior and callback
Default AnimationDetails
Override AnimationDetails
default model state
override model state
current model shared state
particle-system collections
transform/optional state
```

The model component owns/holds managed model resources and animation state. The r216 `PathingEntity` does not expose raw vertex arrays in its own base construction. Geometry is behind `graphics::Model`/render-resource objects.

Related `PathingEntityModels` objects are separate build-request/asset state holders. Their constructors initialize a `BuildRequest<graphics::Model,...>` and a shared metadata field; their destructors release those managed resources.

### Current-model selection

The simple current-model accessor is:

```text
AnimatedModel::GetCurrentModel() const
  0x10042F350
```

The stateful selector is:

```text
AnimatedModel::GetCurrentModel(unsigned int)
  0x100668620
```

Its behavior is proven as:

```cpp
if (overrideModelPresent && overrideAnimation.IsCycleReady(cycle))
    return overrideModel;

if (defaultModelPresent)
    return defaultModel;

if (!defaultAnimation.HasRequestedAnimation()
    && !overrideAnimation.HasRequestedAnimation())
    return fallbackCurrentModel;

return graphics::Model::NullModel;
```

The exact r216 fields are intentionally not Windows candidates. The behavioral fingerprint is:

```text
override model + readiness
  → default model
  → requested-animation checks
  → fallback or NullModel
```

Related accessors:

```text
GetOverrideAnimationModel  0x100418BD0
GetDefaultAnimationModel   0x100418BE0
IsDefaultAnimationInUse    0x10042EDC0
IsOverrideAnimationInUse   0x10042EDE0
HasRequestedOverride       0x1004312C0
```

### Current-model replacement

`AnimatedModel::SetCurrentModel` is `0x1006679F0`. Its behavior establishes the resource ownership boundary:

```cpp
if (currentModel != next)
{
    if (next)
        next->SetGeometryQuery(...);

    if (currentModel && next)
    {
        currentModel->ClearHighlight();
        next->SetHighlight(
            currentModel->GetHighlightScale(),
            currentModel->GetMapSquarePointLights());
    }

    animationState->WaitForAsyncTasks();
    animationState->InvalidateAsyncUpdate();
    animationState->InvalidateSampled();
    currentModel = next;
}
```

Thus current-model replacement is a managed shared-resource transition with animation-state invalidation and highlight-state transfer, not an integer model-ID assignment.

### r216 caller relationships

The generated corpus contains the selector and surrounding animation methods, but its exported `referencedBy` data does not provide a complete reliable xref list for every `GetCurrentModel` call. The proven caller families are nevertheless clear:

```text
actor/model update
  → default/override readiness
  → current-model selection
  → SetCurrentModel
  → animation-state invalidation

render/model preparation
  → current-model retrieval
  → graphics::Model transform/batch preparation

highlight/model replacement
  → current-model replacement
  → highlight transfer between model resources
```

The important relationship is that selection happens inside the actor-owned AnimatedModel component before downstream model/batch rendering.

### Windows semantic fingerprint

The Windows equivalent should be sought in actor construction/update code, not scene insertion, by looking for a cluster with these properties:

```text
actor/base construction
  → inline or adjacent animation/model component construction
  → owner/actor pointer passed to component
  → multiple animation/resource handles
  → default/override-like readiness branches
  → pointer-returning current-model/resource selection
  → separate matrix/fine-position state
```

A single nullable field such as Windows `actor +0x728` is not enough. Windows `actor +0x7E0` is currently only compact scene auxiliary state and is also not enough.

### Windows actor-construction comparison

The saved Windows assembly for `sub_14002C8E0` confirms actor-relative initialization around this state block. In particular:

```text
0x14002CEC7  mov [rcx+7E0h], rdi
```

The same constructor/update routine also contains broad actor-state initialization. Since `edi` is zero during this contiguous field-clearing block, this instruction is an initialization-to-null write, not a nonzero-write lead. The exact field must be followed from the source assignment and any ownership operations around it.

This is the important contrast with r216:

```text
r216: PathingEntity constructor
  → inline AnimatedModel constructor
  → owner binding
  → multiple animation/model state objects

Windows current evidence
  → actor state constructor/update
  → [actor +0x7E0] cleared at 0x14002CEC7
  → later non-null source still unresolved
```

The Windows constructor evidence is therefore useful only as a field-boundary/initialization clue. It does not identify `+0x7E0` as AnimatedModel state. The real non-null assignment must be located elsewhere, likely in an actor update/resource path.

### Windows comparison and next targets

```text
Windows +0x728
  → managed resource/readiness handle
  → rejected as primary AnimatedModel candidate

Windows +0x7E0
  → descriptor/scene auxiliary state
  → no model-selection behavior proven

Windows sub_1400C8F90
  → generic managed-pair copy
  → rejected as a current-model selector

Windows sub_14069F000/sub_14069F6A0
  → scene insertion
  → rejected as the immediate AnimatedModel component
```

The next Windows targets are:

1. Actor construction around `sub_14002C8E0`, looking for an adjacent component constructor and owner binding.
2. Actor animation/update routines that consume sequence/frame fields and choose among multiple resource pointers.
3. Actor-provenance callers of `sub_1405F54D0`, to determine whether its definition resource feeds a separate animation wrapper or remains immutable metadata.

No Windows offsets were inferred from r216 and no production code was modified.

## 26. LostCity r216 debug reference

This section records the semantic findings from the macOS x86-64 LostCity r216 debug client at:

```text
C:\\Users\\trump\\Documents\\MobileOSRS\\reference\\lostcity-r216
```

The r216 binary is a behavioral reference only. Do not copy its addresses, offsets, vtables, structure sizes, or calling conventions into the Windows client.

### r216 model chain

The named r216 architecture supports this conceptual path:

```text
ClientNpc / PathingEntity
  -> PathingEntityAnimatedModel
  -> AnimatedModel
  -> current animation model selection
  -> graphics::Model / dash3d model
  -> drawable batches
  -> renderer submission
```

Important symbols:

```text
PathingEntityAnimatedModel
  0x1003F0610 / 0x1003FE9D0

AnimatedModel constructor
  0x100666C90

AnimatedModel::GetCurrentModel() const
  0x10042F350

AnimatedModel::GetCurrentModel(unsigned int)
  0x100668620

AnimatedModel::GetDefaultAnimationModel()
  0x100418BE0

AnimatedModel::GetOverrideAnimationModel()
  0x100418BD0
```

The direct `ClientNpc` methods recovered in the available corpus primarily expose identity and highlight state. The current model is selected through the common pathing-entity animation layer, not through a simple immutable NPC-definition accessor.

`AnimatedModel::GetCurrentModel(unsigned int)` selects between synchronized/override, default-animation, and null/fallback model paths depending on animation readiness and requested animation state. This proves that the current render model is actor-state-dependent and is not equivalent to `NpcType.getModelId()`.

### r216 AnimatedModel state

The `AnimatedModel` constructor establishes conceptual state for:

```text
asset/config dependencies
animation wrapper cache
AnimationState
separate default AnimationDetails
separate override AnimationDetails
default/current model storage
override model storage
particle systems
```

Relevant methods include:

```text
GetCurrentAnimation()
GetDefaultAnimation()
GetOverrideAnimation()
GetOverrideAnimationModel()
GetDefaultAnimationModel()
IsDefaultAnimationInUse()
IsOverrideAnimationInUse()
HasRequestedDefaultAnimation()
HasRequestedOverrideAnimation()
SetCurrentModel()
RequestOverrideAnimation()
SetOverrideSourceTicks()
```

The supported semantic model is:

```text
base model/resource + animation state
  -> selected current model object
```

It is not yet proven whether every animation transform is stored permanently in the selected model or whether some transforms remain in renderer/model-preparation state.

### r216 vertex and transform fingerprints

The r216 corpus contains two relevant model abstractions. The older `jag::Model` exposes the clearest CPU transform fingerprint:

```text
jag::Model::GetNumVerts()
  0x1001C4D20
```

Its body returns a count-like field multiplied by three:

```cpp
return *(int*)(this + 8) * 3;
```

That multiplication is semantic evidence only; it is not a Windows offset or a guaranteed Windows representation.

The key transform path is:

```text
jag::Model::TransformVertices()
  0x1001B9AA0

jag::Model::TransformVerticesScalar()
  0x1001B9B00
```

`TransformVertices()` forwards to the scalar implementation. The scalar implementation has this behavioral shape:

```text
bounded vertex loop
  -> three indexed integer coordinate arrays
  -> fixed-point rotation
  -> translation
  -> second camera/orientation transform
  -> depth calculation
  -> perspective division
  -> projected integer x/y
  -> projected floating-point x/y
```

Distinctive operations include:

```text
integer multiply/subtract pairs
16.16-style shifts (>> 16)
integer translations
second rotation stage
depth/sort-depth writes
perspective divide
parallel integer and float screen-coordinate outputs
```

This is the preferred behavioral fingerprint for locating a stripped Windows equivalent. Search for the operation/data-flow pattern rather than the r216 bytes or field offsets.

### r216 transform spaces

The r216 `ResolvedModel` transform functions are:

```text
GetLocalTransform()
  0x100369720

GetWorldTransform(int, int, int, int)
  0x10036A7B0
```

`GetLocalTransform()` copies a matrix-like local transform. `GetWorldTransform()` builds a Y-axis rotation and combines it with the local transform and other render-related state.

The semantic space sequence is:

```text
model-local
  -> animation-transformed/model state
  -> actor/world rotation and translation
  -> camera transform
  -> screen coordinates
```

The Windows hull implementation must not feed model-local vertices directly into `WORLD_TO_SCREEN` until the equivalent actor/world transform is proven.

### r216 graphics::Model and renderer boundary

The higher-level r216 `jag::graphics::Model` exposes renderer-facing batch state:

```text
Model::GetBatchList()
  0x10011CED0

Model::GetBatchCount()
  0x1001D0A30

Model::GatherDrawableElements(...)
  0x1001D1720
```

`GetBatchList()` returns the model's batch collection, and `GetBatchCount()` queries that collection. `GatherDrawableElements()` is the model-to-renderer boundary:

```text
graphics::Model
  -> geometry batches
  -> DrawableQueue
  -> renderer-facing drawable elements
```

The r216 symbols also expose `ConstructFinishAndUpload`, `GetGeometryQuery`, and `SetGeometryQuery`, indicating a hybrid CPU-model/GPU-upload architecture. A Windows implementation should prefer finding the pre-upload CPU geometry rather than attempting GPU readback.

### r216 NPC highlighting/cache path

The r216 highlighting system is cached and event/group oriented.

Individual NPC highlighting:

```text
AddNPCHighlight
  0x100011000
```

It creates a unique NPC key, adds entity highlight state, and updates/removes the cached entry if the key has been reused.

NPC-type highlighting:

```text
AddNPCTypeHighlight
  0x1000115F0
```

It creates or updates a type group, walks currently known NPCs, compares their type IDs, caches matching NPC UIDs, and calls `UpdateCachedHighlights`.

Cache rebuilding:

```text
UpdateCachedHighlights
  0x10000EA30
```

It walks NPCs and other highlighted entity categories through callbacks rather than performing full name/type matching during every render frame.

The NPC-specific association is represented by:

```text
ClientNpc::SetHighlight
  0x1000133A0

ClientNpc::GetHighlight
  0x1000FC660
```

The r216 field locations are intentionally not recorded as Windows layout candidates.

### r216 model overlay properties

Relevant `EntityHighlight` methods:

```text
EntityHighlight::GetModelOutlineWidth()
  0x1001D15C0

EntityHighlight::GetModelTintAlpha()
  0x1001D15F0

EntityHighlight::GetModelOverlayProperties()
  0x10034AC90
```

`GetModelOutlineWidth()` returns zero unless model outline is enabled; otherwise it returns the configured width. `GetModelTintAlpha()` returns zero unless model fill is enabled; otherwise it returns the configured alpha.

`GetModelOverlayProperties()` packs model highlight state, outline width, and tint alpha into a compact property value.

Other relevant options include:

```text
HasModelHighlight()
HasModelOutline()
HasModelFill()
AlwaysOnTop()
ShowInsideEdges()
```

The r216 graphics model also carries native highlight state:

```text
Model::SetTint()
  0x10091B490

Model::HasHighlight()
  0x100432550

Model::ClearHighlight()
  0x100431550

Model::GetHighlightSilhouetteAlpha()
  0x1008139E0

Model::GetHighlightsIgnoreDepth()
  0x100814970

Model::GetHighlightedSectionScale()
  0x100815480
```

This strongly suggests that r216 model outlining/fill is integrated into model/render state, rather than being only an external projected polygon. The inspected files do not yet prove whether the final outline uses a stencil pass, expanded geometry, a shader silhouette, a second model pass, or another renderer technique.

### r216 behavioral fingerprints for Windows matching

The highest-value stripped-equivalent searches are:

1. **Current animation-model selector**
   - checks default/override animation state;
   - selects among multiple model/resource pointers;
   - handles animation readiness or null model state.

2. **Vertex-count accessor**
   - reads a count-like integer from a model/render object;
   - is called by bounds, transform, upload, or batch preparation code.

3. **Vertex-transform loop**
   - three coordinate sources;
   - integer rotation/multiply pairs;
   - fixed-point shifts or equivalent scaling;
   - translation;
   - depth and perspective division;
   - projected coordinate writes.

4. **Model-overlay consumer**
   - extracts outline width and tint alpha or equivalent compact flags;
   - tests model outline/fill state;
   - writes tint/silhouette/depth-related state;
   - passes model/batch data into renderer preparation.

These are behavioral fingerprints only. No r216 address, offset, vtable, or structure size is a Windows implementation candidate.

### Candidate Windows correspondences — hypotheses only

| Windows function | Possible r216 role | Supporting evidence | Contradictions | Confidence |
|---|---|---|---|---|
| `0x1400A5410` | Actor render/model preparation or animation-state boundary | Called on the actor-associated object immediately before scene submission | No current-model selector or vertex loop proven | Low–medium |
| `0x14009A1F0` | Scene/render-state preparation or model placement | Called after actor state and position are prepared | May only calculate culling/scene coordinates | Low |
| `0x1400A5350` | Actor readiness/visibility/model availability check | Called before deeper actor scene processing | Could be generic entity state rather than animation readiness | Low |
| `0x140063BC0` | Broad pathing-entity scene update | Performs entity lookup, position/definition processing, and scene submission | Too large and indirect to identify as a model accessor | Medium for scene role; low for model role |

No direct r216-to-Windows mapping is asserted.

### Prioritized Windows next targets

1. `0x1400A5410` — trace model-pointer selection, animation flags, and return values.
2. `0x14009A1F0` — inspect consumers for opaque model/render objects and fixed-point geometry loops.
3. The first non-scene callee after those functions — search for the r216 transform fingerprint rather than model-related strings.

The existing Windows scene wrappers remain lower priority:

```text
sub_14069EE90
sub_14069EC60
sub_14069F000
```

They are already established as scene/tile submission infrastructure and have not exposed a validated vertex source.

### LostCity debug conclusion

The r216 reference establishes the semantic map we want to find in Windows:

```text
NPC/pathing entity
  -> animation wrapper
  -> default/override current model
  -> model geometry and transform pipeline
  -> geometry batches/drawable queue
  -> renderer
```

It also confirms that model highlighting is integrated through tint, silhouette alpha, outline width, and depth behavior. It does not provide reusable Windows layouts.

No production code was changed or rebuilt for this r216 comparison.

---

# sub_1400A4D80 — recovered calling convention (2026-09-16)

Recovered from the IDA decompilation in `build/dist/actor_animation_core_dump.txt:1546`.
This is the item that was blocking every model-derived highlight (hull, outline, recolour).

## Signature

```
_QWORD *__fastcall sub_1400A4D80(__int64 a1, _QWORD *a2, int a3)
```

MSVC x64 member function returning a 16-byte struct by value, so the registers are:

```
RCX  = a1  this          the actor
RDX  = a2  hidden retbuf caller-allocated 16 bytes
R8D  = a3  int           game cycle
RAX  =     returns a2
```

Proof that `a2` is the out-buffer and not an input: every early-out path writes
`*a2 = 0; a2[1] = 0; return a2;`, and the success path writes `*a2 = v29; a2[1] = v30;`.

In C++ terms:

```cpp
struct ManagedPair { std::uintptr_t a, b; };   // see "which half" below
using GetActorModel = ManagedPair* (__fastcall*)(std::uintptr_t actor,
                                                 ManagedPair* out,
                                                 int cycle);
```

## a3 is the game cycle

It is only ever compared against actor cycle-range fields:
`a1+0x558`/`a1+0x55C` and `a1+0x78C`/`a1+0x790`. Pass the client's current cycle,
which the resolver already has as `off::CYCLE` (client object + 0x2164).

## Do NOT call it by RVA

`npc_model_resolution_dump.txt:9`: **`sub_1400A4D80` is slot 0 of the derived actor
vtable.** Call it through the actor's own vtable (`(*(void***)actor)[0]`), not through
`off::NPC_GET_MODEL_ENTRY`. Two reasons:

* it dispatches to the correct concrete override for that actor's real type, which is
  also what makes the *player* path work without proving a second entry point;
* it survives a client update that moves the function, because the vtable index is
  structural rather than an address.

`off::NPC_GET_MODEL_ENTRY` should stay a diagnostic cross-check: assert that slot 0 of
a candidate actor's vtable equals `moduleBase + 0xA4D80` on the validated build, and
refuse the capability when it does not.

## Guard before calling

`a1 + 0x728` is dereferenced unconditionally at entry (`if (*(_QWORD*)(a1+1832))`).
There is a dedicated predicate for exactly this test that compiles to
`return *(_QWORD *)(a1 + 1832) != 0;`. Check `actor + 0x728 != 0` before the call.

## Ownership result (2026-09-16)

The caller trace resolves the remaining lifetime question.

`sub_14007B070` calls the actor virtual entry with the current cycle and a caller-owned
16-byte output pair, then consumes and releases the returned first member:

```asm
mov  r8d, [client + 0x2164] ; current cycle
lea  rdx, [stackPair]
mov  rcx, actor
call sub_1400A4D80
mov  control, [rax + 0x00]
mov  object,  [rax + 0x08]
```

The release is the same two-stage protocol used inside the bridge:

```c
strong = sub_140044D30(control + 8);
if (_InterlockedExchangeAdd(strong, -1) == 1) {
    control->vtable[1](control);
    weak = sub_140044D30(control + 12);
    if (_InterlockedExchangeAdd(weak, -1) == 1)
        control->vtable[2](control);
}
```

This establishes:

* pair member `+0x00` is the managed control/ownership object;
* pair member `+0x08` is the concrete RuntimeModel object;
* the returned control reference is owned by the caller and must be released after the snapshot.

`client/model_geometry.hpp` now calls the validated virtual slot, copies the RuntimeModel arrays,
and releases the control pair immediately afterward. It never retains a raw model pointer. The provider
currently enables NPCs only; player acquisition remains unavailable until a player-side path is separately
confirmed.
