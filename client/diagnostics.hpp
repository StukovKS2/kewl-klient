#pragma once

#include "game.hpp"
#include "runtime_layout.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <cstdio>
#include <string>
#include <vector>

namespace kk::diagnostics {

inline const char* stateName(layout::State state) {
    switch (state) {
        case layout::State::LegacyFallback: return "legacy";
        case layout::State::Resolved: return "resolved";
        case layout::State::Validated: return "validated";
        default: return "unavailable";
    }
}


inline const char* locCategoryName(SceneLocCategory c) {
    switch (c) {
        case SceneLocCategory::BoundaryObject:  return "boundary";
        case SceneLocCategory::WallDecoration: return "wall-deco";
        case SceneLocCategory::GameObject:      return "game";
        case SceneLocCategory::FloorDecoration: return "floor-deco";
        default:                                return "unknown";
    }
}

inline const char* renderableKind(std::uintptr_t renderable) {
    if (!renderable) return "none";
    const std::uintptr_t vt = rdp(renderable);
    const std::uintptr_t base = moduleBase();
    if (off::RUNTIME_MODEL_VTABLE && vt == base + off::RUNTIME_MODEL_VTABLE) return "RuntimeModel";
    if (off::MODEL_DATA_VTABLE && vt == base + off::MODEL_DATA_VTABLE) return "ModelData";
    if (off::DYNAMIC_LOC_VTABLE && vt == base + off::DYNAMIC_LOC_VTABLE) return "DynamicLoc";
    return "unknown";
}

inline int sceneDistance(const Entity& me, int x, int y) {
    return (std::max)(std::abs(me.sceneX - x), std::abs(me.sceneY - y));
}

struct NearbyLocRow {
    SceneLoc loc;
    int distance = 0;
};

struct NearbyEntityRow {
    Entity entity;
    int distance = 0;
    std::string name;
    int typeId = -1;
};

inline void appendLiveSnapshot(std::vector<std::string>& out, char (&line)[256]) {
    constexpr int RADIUS = 15;
    // Keep every nearby record. The inspector is a diagnostic explorer, not a summary list;
    // the launcher provides scrolling for large snapshots.
    constexpr std::size_t MAX_OBJECT_ROWS = static_cast<std::size_t>(-1);
    constexpr std::size_t MAX_PLAYER_ROWS = static_cast<std::size_t>(-1);
    constexpr std::size_t MAX_NPC_ROWS = static_cast<std::size_t>(-1);

    const std::uintptr_t s = scene();
    const Tile base = sceneBase();
    const int dimX = s && off::SCENE_TILE_DIM_X ? rd<std::int32_t>(s + off::SCENE_TILE_DIM_X) : 0;
    const int dimY = s && off::SCENE_TILE_DIM_Y ? rd<std::int32_t>(s + off::SCENE_TILE_DIM_Y) : 0;
    const std::uintptr_t c = clientObj();
    const int cycle = c && off::CYCLE ? rd<std::int32_t>(c + off::CYCLE, -1) : -1;

    bool haveMe = false;
    const Entity me = localPlayer(haveMe);

    std::snprintf(line, sizeof line,
                  "live: scene=0x%llX base=%s%d,%d dims=%dx%d cycle=%d locScene=%s locHull=%s",
                  static_cast<unsigned long long>(s),
                  base.ok ? "" : "?",
                  base.ok ? base.x : 0, base.ok ? base.y : 0,
                  dimX, dimY, cycle,
                  layout::available(layout::Field::LocScene) ? "ready" : "off",
                  layout::locModelHighlights() ? "ready" : "off");
    out.emplace_back(line);

    if (haveMe) {
        const std::string meName = playerName(me.addr);
        std::snprintf(line, sizeof line,
                      "live: local uid=%d name=%s scene=%d,%d p=%d fine=%d,%d,%d anim=%d ori=%d",
                      me.uid, meName.empty() ? "?" : meName.c_str(),
                      me.sceneX, me.sceneY, me.plane,
                      me.fineX, me.fineH, me.fineY, me.animation, me.orientation);
        out.emplace_back(line);
    } else {
        out.emplace_back("live: local player unavailable");
    }

    int totalObjects = 0;
    int catGame = 0, catBoundary = 0, catWall = 0, catFloor = 0;
    std::vector<NearbyLocRow> nearbyObjects;
    nearbyObjects.reserve(64);

    forEachSceneLoc([&](const SceneLoc& loc) {
        ++totalObjects;
        switch (loc.category) {
            case SceneLocCategory::GameObject:      ++catGame; break;
            case SceneLocCategory::BoundaryObject:  ++catBoundary; break;
            case SceneLocCategory::WallDecoration: ++catWall; break;
            case SceneLocCategory::FloorDecoration: ++catFloor; break;
        }
        if (!haveMe || loc.plane != me.plane) return;
        const int d = sceneDistance(me, loc.sceneX, loc.sceneY);
        if (d <= RADIUS) nearbyObjects.push_back({loc, d});
    });

    std::sort(nearbyObjects.begin(), nearbyObjects.end(),
              [](const NearbyLocRow& a, const NearbyLocRow& b) {
                  if (a.distance != b.distance) return a.distance < b.distance;
                  if (a.loc.id != b.loc.id) return a.loc.id < b.loc.id;
                  if (a.loc.sceneX != b.loc.sceneX) return a.loc.sceneX < b.loc.sceneX;
                  return a.loc.sceneY < b.loc.sceneY;
              });

    std::snprintf(line, sizeof line,
                  "live: objects total=%d nearby<=%d=%zu game=%d boundary=%d wall=%d floor=%d",
                  totalObjects, RADIUS, nearbyObjects.size(),
                  catGame, catBoundary, catWall, catFloor);
    out.emplace_back(line);

    int totalPlayers = 0, totalNpcs = 0;
    std::vector<NearbyEntityRow> nearbyPlayers, nearbyNpcs;
    nearbyPlayers.reserve(32);
    nearbyNpcs.reserve(64);

    forEachEntity([&](const Entity& e) {
        if (e.player) ++totalPlayers; else ++totalNpcs;
        if (!haveMe || (e.player && e.uid == me.uid) || e.plane != me.plane) return;
        const int d = sceneDistance(me, e.sceneX, e.sceneY);
        if (d > RADIUS) return;

        NearbyEntityRow row;
        row.entity = e;
        row.distance = d;
        row.name = e.player ? playerName(e.addr) : npcName(e.addr);
        row.typeId = e.player ? -1 : npcTypeId(e.addr);
        (e.player ? nearbyPlayers : nearbyNpcs).push_back(std::move(row));
    });

    auto entityLess = [](const NearbyEntityRow& a, const NearbyEntityRow& b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        return a.entity.uid < b.entity.uid;
    };
    std::sort(nearbyPlayers.begin(), nearbyPlayers.end(), entityLess);
    std::sort(nearbyNpcs.begin(), nearbyNpcs.end(), entityLess);

    std::snprintf(line, sizeof line,
                  "live: entities players=%d npcs=%d nearbyPlayers=%zu nearbyNpcs=%zu radius=%d",
                  totalPlayers, totalNpcs, nearbyPlayers.size(), nearbyNpcs.size(), RADIUS);
    out.emplace_back(line);

    out.emplace_back("nearby objects:");
    if (nearbyObjects.empty()) {
        out.emplace_back("object: <none>");
    } else {
        const std::size_t n = (std::min)(nearbyObjects.size(), MAX_OBJECT_ROWS);
        for (std::size_t i = 0; i < n; ++i) {
            const SceneLoc& loc = nearbyObjects[i].loc;
            const std::uintptr_t renderable = loc.renderableA ? loc.renderableA : loc.renderableB;
            const std::uintptr_t vt = renderable ? rdp(renderable) : 0;
            std::snprintf(line, sizeof line,
                          "object: d=%d id=%d cat=%s scene=%d,%d,%d fine=%d,%d,%d rec=0x%llX rend=0x%llX vt=0x%llX kind=%s",
                          nearbyObjects[i].distance, loc.id, locCategoryName(loc.category),
                          loc.sceneX, loc.sceneY, loc.plane,
                          loc.fineX, loc.fineH, loc.fineY,
                          static_cast<unsigned long long>(loc.addr),
                          static_cast<unsigned long long>(renderable),
                          static_cast<unsigned long long>(vt),
                          renderableKind(renderable));
            out.emplace_back(line);
        }
    }

    out.emplace_back("nearby players:");
    if (nearbyPlayers.empty()) {
        out.emplace_back("player: <none>");
    } else {
        const std::size_t n = (std::min)(nearbyPlayers.size(), MAX_PLAYER_ROWS);
        for (std::size_t i = 0; i < n; ++i) {
            const auto& r = nearbyPlayers[i];
            const Entity& e = r.entity;
            std::snprintf(line, sizeof line,
                          "player: d=%d uid=%d name=%s scene=%d,%d p=%d fine=%d,%d,%d anim=%d ori=%d addr=0x%llX",
                          r.distance, e.uid, r.name.empty() ? "?" : r.name.c_str(),
                          e.sceneX, e.sceneY, e.plane,
                          e.fineX, e.fineH, e.fineY, e.animation, e.orientation,
                          static_cast<unsigned long long>(e.addr));
            out.emplace_back(line);
        }
    }

    out.emplace_back("nearby npcs:");
    if (nearbyNpcs.empty()) {
        out.emplace_back("npc: <none>");
    } else {
        const std::size_t n = (std::min)(nearbyNpcs.size(), MAX_NPC_ROWS);
        for (std::size_t i = 0; i < n; ++i) {
            const auto& r = nearbyNpcs[i];
            const Entity& e = r.entity;
            std::snprintf(line, sizeof line,
                          "npc: d=%d uid=%d id=%d name=%s scene=%d,%d p=%d fine=%d,%d,%d anim=%d ori=%d addr=0x%llX",
                          r.distance, e.uid, r.typeId, r.name.empty() ? "?" : r.name.c_str(),
                          e.sceneX, e.sceneY, e.plane,
                          e.fineX, e.fineH, e.fineY, e.animation, e.orientation,
                          static_cast<unsigned long long>(e.addr));
            out.emplace_back(line);
        }
    }
}

inline std::vector<std::string> lines() {
    std::vector<std::string> out;
    char line[256]{};
    std::snprintf(line, sizeof line, "runtime: pid=%lu module=0x%llX build=%ls",
                  static_cast<unsigned long>(GetCurrentProcessId()),
                  static_cast<unsigned long long>(moduleBase()), off::BUILD_VERSION);
    out.emplace_back(line);
    std::snprintf(line, sizeof line, "runtime: client=0x%llX gameState=%s entities=%s projection=%s",
                  static_cast<unsigned long long>(clientObj()),
                  stateName(layout::get(layout::Field::GameState).state),
                  layout::entities() ? "ready" : "unavailable",
                  layout::projection() ? "ready" : "unavailable");
    out.emplace_back(line);
    for (unsigned i = 0; i < static_cast<unsigned>(layout::Field::Count); ++i) {
        const auto field = static_cast<layout::Field>(i);
        std::snprintf(line, sizeof line, "runtime: field[%u] state=%s source=%s", i,
                      stateName(layout::get(field).state), layout::get(field).source);
        out.emplace_back(line);
    }

    // Live inspector data comes before the offset table so the launcher popout opens on the
    // information most useful while debugging a plugin. The bridge caps the final list, so the
    // nearby rows above are deliberately bounded.
    appendLiveSnapshot(out, line);

    out.emplace_back("offsets: functions / runtime model");
#define KEWL_OFFSET(name) do { \
        std::snprintf(line, sizeof line, "offset: %-28s 0x%llX", #name, \
                      static_cast<unsigned long long>(off::name)); \
        out.emplace_back(line); \
    } while (false)
    KEWL_OFFSET(WORLD_TO_SCREEN); KEWL_OFFSET(DO_ACTION);
    KEWL_OFFSET(RUNTIME_MODEL_VTABLE); KEWL_OFFSET(MODEL_DATA_VTABLE);
    KEWL_OFFSET(DYNAMIC_LOC_VTABLE); KEWL_OFFSET(DYNAMIC_LOC_GET_MODEL); KEWL_OFFSET(LOC_TAG_TO_ID);
    KEWL_OFFSET(RUNTIME_MODEL_CTOR);
    KEWL_OFFSET(RUNTIME_MODEL_CLONE); KEWL_OFFSET(RUNTIME_MODEL_APPLY_ANIM);
    KEWL_OFFSET(RUNTIME_MODEL_TRANSFORM); KEWL_OFFSET(RUNTIME_MODEL_INVALIDATE);
    KEWL_OFFSET(RUNTIME_MODEL_SCALE); KEWL_OFFSET(MODEL_VERTEX_COUNT);
    KEWL_OFFSET(MODEL_VERTEX_X); KEWL_OFFSET(MODEL_VERTEX_Y);
    KEWL_OFFSET(MODEL_VERTEX_Z); KEWL_OFFSET(MODEL_ANIM_GROUPS);
    KEWL_OFFSET(NPC_GET_MODEL_ENTRY); KEWL_OFFSET(NPC_MODEL_RESOLVER);
    KEWL_OFFSET(MANAGED_RELEASE_HELPER); KEWL_OFFSET(NPC_MODEL_RESOURCE);
    KEWL_OFFSET(MANAGED_STRONG_COUNT); KEWL_OFFSET(MANAGED_WEAK_COUNT);
    KEWL_OFFSET(MANAGED_DESTROY_VTABLE); KEWL_OFFSET(MANAGED_DELETE_VTABLE);
    KEWL_OFFSET(NPC_MODEL_ENTRY_SLOT);

    out.emplace_back("offsets: roots / client fields");
    KEWL_OFFSET(CLIENT_OBJ_PTR); KEWL_OFFSET(VARP_ARRAY_PTR);
    KEWL_OFFSET(CONTAINER_BUCKETS); KEWL_OFFSET(CONTAINER_MASK);
    KEWL_OFFSET(IFACE_EMPTY_SENTINEL); KEWL_OFFSET(SCENE);
    KEWL_OFFSET(LOCAL_PLAYER_IDX); KEWL_OFFSET(PLAYER_COUNT);
    KEWL_OFFSET(PLAYER_IDS); KEWL_OFFSET(SKILL_EFFECTIVE);
    KEWL_OFFSET(SKILL_BASE); KEWL_OFFSET(SKILL_XP); KEWL_OFFSET(RUN_ENERGY);
    KEWL_OFFSET(CYCLE); KEWL_OFFSET(GAME_STATE); KEWL_OFFSET(WORLD_MAP);
    KEWL_OFFSET(REGISTRY_MAP); KEWL_OFFSET(REGISTRY_GROUPS);
    KEWL_OFFSET(REGISTRY_GROUP_COUNT); KEWL_OFFSET(IFACE_MANAGER);

    out.emplace_back("offsets: scene / entities");
    KEWL_OFFSET(SCENE_BASE_X); KEWL_OFFSET(SCENE_BASE_Y);
    KEWL_OFFSET(SCENE_NPC_UIDS); KEWL_OFFSET(SCENE_NPC_UID_COUNT);
    KEWL_OFFSET(SCENE_TILE_DIM_X); KEWL_OFFSET(SCENE_TILE_DIM_Y); KEWL_OFFSET(SCENE_TILE_GRID);
    KEWL_OFFSET(SCENE_TILE_ENTRY_STRIDE); KEWL_OFFSET(SCENE_TILE_OBJECT);
    KEWL_OFFSET(TILE_GAME_OBJECT_COUNT); KEWL_OFFSET(TILE_GAME_OBJECTS);
    KEWL_OFFSET(TILE_BOUNDARY_OBJECT); KEWL_OFFSET(TILE_WALL_DECORATION); KEWL_OFFSET(TILE_FLOOR_DECORATION);
    KEWL_OFFSET(LOC_FIXED_FINE_X); KEWL_OFFSET(LOC_FIXED_FINE_H); KEWL_OFFSET(LOC_FIXED_FINE_Y); KEWL_OFFSET(LOC_FIXED_TAG);
    KEWL_OFFSET(BOUNDARY_RENDERABLE_A); KEWL_OFFSET(BOUNDARY_RENDERABLE_B);
    KEWL_OFFSET(WALL_RENDERABLE_A); KEWL_OFFSET(WALL_RENDERABLE_B); KEWL_OFFSET(FLOOR_RENDERABLE);
    KEWL_OFFSET(GAME_OBJECT_FINE_H); KEWL_OFFSET(GAME_OBJECT_FINE_X); KEWL_OFFSET(GAME_OBJECT_FINE_Y);
    KEWL_OFFSET(GAME_OBJECT_TAG); KEWL_OFFSET(GAME_OBJECT_PLANE);
    KEWL_OFFSET(GAME_OBJECT_START_X); KEWL_OFFSET(GAME_OBJECT_END_X);
    KEWL_OFFSET(GAME_OBJECT_START_Y); KEWL_OFFSET(GAME_OBJECT_END_Y);
    KEWL_OFFSET(GAME_OBJECT_RENDERABLE); KEWL_OFFSET(GAME_OBJECT_RENDER_CONTROL); KEWL_OFFSET(GAME_OBJECT_RENDER_OBJECT);
    KEWL_OFFSET(ENTITY_SCENE_X); KEWL_OFFSET(ENTITY_SCENE_Y);
    KEWL_OFFSET(ENTITY_FINE_H); KEWL_OFFSET(ENTITY_FINE_X);
    KEWL_OFFSET(ENTITY_FINE_Y); KEWL_OFFSET(ENTITY_PLANE);
    KEWL_OFFSET(ENTITY_PLANE_COORD); KEWL_OFFSET(ENTITY_DEF_PTR);
    KEWL_OFFSET(ENTITY_NAME_OVERRIDE); KEWL_OFFSET(PLAYER_NAME_PTR);
    KEWL_OFFSET(DEF_NAME); KEWL_OFFSET(ENTITY_ANIMATION);
    KEWL_OFFSET(ENTITY_ORIENTATION);

    out.emplace_back("offsets: world map / interfaces");
    KEWL_OFFSET(WM_ORIGIN_LEVEL); KEWL_OFFSET(WM_ORIGIN_X);
    KEWL_OFFSET(WM_ORIGIN_Z); KEWL_OFFSET(WM_CENTRE_X); KEWL_OFFSET(WM_CENTRE_Z);
    KEWL_OFFSET(IFACE_GROUP_COUNT); KEWL_OFFSET(IFACE_GROUP_ARRAY);
    KEWL_OFFSET(IFACE_GROUP_ENTRY_STRIDE); KEWL_OFFSET(IFACE_GROUP_ENTRY_COUNT);
    KEWL_OFFSET(IFACE_GROUP_ENTRY_DATA); KEWL_OFFSET(IFTYPE_X);
    KEWL_OFFSET(IFTYPE_Y); KEWL_OFFSET(IFTYPE_WIDTH); KEWL_OFFSET(IFTYPE_HEIGHT);
    KEWL_OFFSET(IFTYPE_HIDDEN); KEWL_OFFSET(IFTYPE_CHILDREN_COUNT);
    KEWL_OFFSET(IFTYPE_CHILDREN_DATA); KEWL_OFFSET(IFTYPE_TEXT);
    KEWL_OFFSET(IFTYPE_TEXT2); KEWL_OFFSET(IFTYPE_SCAN_SPAN);
    KEWL_OFFSET(IFTYPE_TEXT_FLAG); KEWL_OFFSET(IFTYPE_TEXT2_FLAG);
    KEWL_OFFSET(GROUP_TABLE); KEWL_OFFSET(GROUP_NEXT);
    KEWL_OFFSET(PLAYER_BUCKETS); KEWL_OFFSET(PLAYER_BUCKET_COUNT);
    KEWL_OFFSET(NPC_BUCKETS); KEWL_OFFSET(NPC_BUCKET_COUNT);
    KEWL_OFFSET(NODE_UID); KEWL_OFFSET(NODE_ENTITY); KEWL_OFFSET(NODE_NEXT);
    KEWL_OFFSET(CONTAINER_NODE_IDS); KEWL_OFFSET(CONTAINER_NODE_IDS_END);
    KEWL_OFFSET(CONTAINER_NODE_QTYS); KEWL_OFFSET(CONTAINER_NODE_QTYS_END);
    KEWL_OFFSET(CONTAINER_NODE_NEXT); KEWL_OFFSET(CAMERA_FINE_X);
    KEWL_OFFSET(CAMERA_FINE_H); KEWL_OFFSET(CAMERA_FINE_Y);
    KEWL_OFFSET(VIEW_OBJ); KEWL_OFFSET(VIEW_OBJ_SCALE_BASE);
    KEWL_OFFSET(VIEW_IN_W); KEWL_OFFSET(VIEW_IN_H);
    KEWL_OFFSET(VIEW_OUT_W); KEWL_OFFSET(VIEW_OUT_H);
#undef KEWL_OFFSET
    return out;
}

} // namespace kk::diagnostics
