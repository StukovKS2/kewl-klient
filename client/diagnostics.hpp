#pragma once

#include "game.hpp"
#include "runtime_layout.hpp"

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

    out.emplace_back("offsets: functions / runtime model");
#define KEWL_OFFSET(name) do { \
        std::snprintf(line, sizeof line, "offset: %-28s 0x%llX", #name, \
                      static_cast<unsigned long long>(off::name)); \
        out.emplace_back(line); \
    } while (false)
    KEWL_OFFSET(WORLD_TO_SCREEN); KEWL_OFFSET(DO_ACTION);
    KEWL_OFFSET(RUNTIME_MODEL_VTABLE); KEWL_OFFSET(RUNTIME_MODEL_CTOR);
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
