// model_geometry.hpp -- read-only RuntimeModel geometry for future model hulls.
//
// This layer owns the unsafe boundary: it validates the concrete vtable, bounds the
// vertex count, and copies all three mutable arrays before any projection work. It
// calls the validated NPC actor model bridge only through the actor's virtual
// slot 0. Its managed pair is released immediately after snapshotting.
#pragma once

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "offsets.hpp"
#include "runtime_layout.hpp"
#include "log.hpp"

namespace kk::model {

enum class EntityKind { Npc, Player };

struct CurrentModelRef {
    std::uintptr_t model = 0;   // second member of a managed pair, when acquired safely
    std::uintptr_t control = 0; // optional owner/control member; never passed as model
};

struct Vertex { std::int32_t x = 0, y = 0, z = 0; };
struct ScreenPoint { float x = 0.0f, y = 0.0f; };
struct EntityPose {
    float fineX = 0.0f, fineY = 0.0f, fineH = 0.0f;
    int orientation = 0;
};
struct WorldPoint { float x = 0.0f, h = 0.0f, y = 0.0f; };

struct Snapshot {
    std::uintptr_t sourceModel = 0;
    std::vector<Vertex> vertices;
    void clear() { sourceModel = 0; vertices.clear(); }
    bool empty() const { return vertices.empty(); }
};

enum class Failure {
    None, CapabilityUnavailable, NullModel, WrongVtable, BadVertexCount,
    BadVertexArrays, ReadFailed
};

enum class AcquireFailure {
    CapabilityUnavailable, ActorUnreadable, ActorEntryMismatch, ResourceMissing,
    ClientUnavailable, CycleUnreadable, PairEmpty, Acquired, SnapshotFailed,
    ProjectionFailed, Success
};

struct Diagnostics {
    std::atomic<std::uint64_t> attempts{0};
    std::atomic<std::uint64_t> acquired{0};
    std::atomic<std::uint64_t> success{0};
    std::atomic<std::uint64_t> capabilityUnavailable{0};
    std::atomic<std::uint64_t> actorUnreadable{0};
    std::atomic<std::uint64_t> actorEntryMismatch{0};
    std::atomic<std::uint64_t> resourceMissing{0};
    std::atomic<std::uint64_t> clientUnavailable{0};
    std::atomic<std::uint64_t> cycleUnreadable{0};
    std::atomic<std::uint64_t> pairEmpty{0};
    std::atomic<std::uint64_t> snapshotFailed{0};
    std::atomic<std::uint64_t> projectionFailed{0};
    ULONGLONG lastReport = 0;
};

inline Diagnostics diagnostics;

inline void note(AcquireFailure why) {
    diagnostics.attempts.fetch_add(why == AcquireFailure::CapabilityUnavailable ||
                                   why == AcquireFailure::ActorUnreadable ||
                                   why == AcquireFailure::ActorEntryMismatch ||
                                   why == AcquireFailure::ResourceMissing ||
                                   why == AcquireFailure::ClientUnavailable ||
                                   why == AcquireFailure::CycleUnreadable ||
                                   why == AcquireFailure::PairEmpty ||
                                   why == AcquireFailure::Acquired ||
                                   why == AcquireFailure::Success ? 1 : 0);
    switch (why) {
    case AcquireFailure::CapabilityUnavailable: diagnostics.capabilityUnavailable++; break;
    case AcquireFailure::ActorUnreadable: diagnostics.actorUnreadable++; break;
    case AcquireFailure::ActorEntryMismatch: diagnostics.actorEntryMismatch++; break;
    case AcquireFailure::ResourceMissing: diagnostics.resourceMissing++; break;
    case AcquireFailure::ClientUnavailable: diagnostics.clientUnavailable++; break;
    case AcquireFailure::CycleUnreadable: diagnostics.cycleUnreadable++; break;
    case AcquireFailure::PairEmpty: diagnostics.pairEmpty++; break;
    case AcquireFailure::Acquired: diagnostics.acquired++; break;
    case AcquireFailure::SnapshotFailed: diagnostics.snapshotFailed++; break;
    case AcquireFailure::ProjectionFailed: diagnostics.projectionFailed++; break;
    case AcquireFailure::Success: diagnostics.success++; break;
    }
}

inline void maybeReport() {
    if (!std::getenv("KEWL_LOG")) return;
    const ULONGLONG now = GetTickCount64();
    if (now - diagnostics.lastReport < 2000) return;
    diagnostics.lastReport = now;
    kk::logf("[model] attempts=%llu acquired=%llu success=%llu capability=%llu actorRead=%llu entryMismatch=%llu resource=%llu client=%llu cycle=%llu pairEmpty=%llu snapshot=%llu projection=%llu vtableRva=%llx npcEntryRva=%llx resourceOff=%llx\n",
        static_cast<unsigned long long>(diagnostics.attempts.load()),
        static_cast<unsigned long long>(diagnostics.acquired.load()),
        static_cast<unsigned long long>(diagnostics.success.load()),
        static_cast<unsigned long long>(diagnostics.capabilityUnavailable.load()),
        static_cast<unsigned long long>(diagnostics.actorUnreadable.load()),
        static_cast<unsigned long long>(diagnostics.actorEntryMismatch.load()),
        static_cast<unsigned long long>(diagnostics.resourceMissing.load()),
        static_cast<unsigned long long>(diagnostics.clientUnavailable.load()),
        static_cast<unsigned long long>(diagnostics.cycleUnreadable.load()),
        static_cast<unsigned long long>(diagnostics.pairEmpty.load()),
        static_cast<unsigned long long>(diagnostics.snapshotFailed.load()),
        static_cast<unsigned long long>(diagnostics.projectionFailed.load()),
        static_cast<unsigned long long>(off::RUNTIME_MODEL_VTABLE),
        static_cast<unsigned long long>(off::NPC_GET_MODEL_ENTRY),
        static_cast<unsigned long long>(off::NPC_MODEL_RESOURCE));
}

inline bool readable(std::uintptr_t p, std::size_t n) {
    if (p < 0x10000) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(reinterpret_cast<void*>(p), &m, sizeof m)) return false;
    if (m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return p <= reinterpret_cast<std::uintptr_t>(m.BaseAddress) + m.RegionSize &&
           n <= reinterpret_cast<std::uintptr_t>(m.BaseAddress) + m.RegionSize - p;
}

template <class T>
inline bool read(std::uintptr_t p, T& out) {
    if (!readable(p, sizeof(T))) return false;
    out = *reinterpret_cast<const T*>(p);
    return true;
}

inline bool copy(std::uintptr_t p, void* out, std::size_t n) {
    if (!readable(p, n)) return false;
    std::memcpy(out, reinterpret_cast<const void*>(p), n);
    return true;
}

inline bool snapshot(std::uintptr_t moduleBase, std::uintptr_t model,
                     Snapshot& out, Failure* why = nullptr) {
    out.clear();
    auto fail = [&](Failure f) { if (why) *why = f; return false; };
    if (why) *why = Failure::None;
    if (!layout::runtimeModelGeometry()) return fail(Failure::CapabilityUnavailable);
    if (!model) return fail(Failure::NullModel);

    std::uintptr_t vtable = 0;
    if (!read(model, vtable) || vtable != moduleBase + off::RUNTIME_MODEL_VTABLE)
        return fail(Failure::WrongVtable);
    std::int32_t count = 0;
    if (!read(model + off::MODEL_VERTEX_COUNT, count) || count <= 0 || count > 262144)
        return fail(Failure::BadVertexCount);
    std::uintptr_t xs = 0, ys = 0, zs = 0;
    if (!read(model + off::MODEL_VERTEX_X, xs) ||
        !read(model + off::MODEL_VERTEX_Y, ys) ||
        !read(model + off::MODEL_VERTEX_Z, zs) || !xs || !ys || !zs)
        return fail(Failure::BadVertexArrays);

    const std::size_t bytes = static_cast<std::size_t>(count) * sizeof(std::int32_t);
    std::vector<std::int32_t> x(count), y(count), z(count);
    if (!copy(xs, x.data(), bytes) || !copy(ys, y.data(), bytes) || !copy(zs, z.data(), bytes))
        return fail(Failure::ReadFailed);
    out.sourceModel = model;
    out.vertices.resize(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i)
        out.vertices[static_cast<std::size_t>(i)] = { x[i], y[i], z[i] };
    return true;
}

// The actor bridge returns a managed pair. The game caller at 0x14007B070
// releases the first member after consuming the result; mirror that release
// protocol immediately after our snapshot, never retain either raw pointer.
inline bool releaseManaged(std::uintptr_t control) {
    if (!control || !off::MANAGED_RELEASE_HELPER || !readable(control, 16)) return false;
    using Helper = std::uintptr_t (__fastcall*)(std::uintptr_t);
    using Destroy = void (__fastcall*)(std::uintptr_t);
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto helper = reinterpret_cast<Helper>(base + off::MANAGED_RELEASE_HELPER);
    auto counter = helper(control + off::MANAGED_STRONG_COUNT);
    if (!counter || !readable(counter, sizeof(std::int32_t))) return false;
    if (::InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(counter), -1) != 1) return true;

    std::uintptr_t vt = 0;
    if (!read(control, vt) || !vt || !readable(vt + off::MANAGED_DESTROY_VTABLE, sizeof(std::uintptr_t))) return false;
    const std::uintptr_t destroyAddress = *reinterpret_cast<const std::uintptr_t*>(vt + off::MANAGED_DESTROY_VTABLE);
    if (destroyAddress) reinterpret_cast<Destroy>(destroyAddress)(control);

    counter = helper(control + off::MANAGED_WEAK_COUNT);
    if (!counter || !readable(counter, sizeof(std::int32_t))) return false;
    if (::InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(counter), -1) != 1) return true;
    if (!readable(vt + off::MANAGED_DELETE_VTABLE, sizeof(std::uintptr_t))) return false;
    const std::uintptr_t deleteAddress = *reinterpret_cast<const std::uintptr_t*>(vt + off::MANAGED_DELETE_VTABLE);
    if (deleteAddress) reinterpret_cast<Destroy>(deleteAddress)(control);
    return true;
}

inline bool currentForEntity(EntityKind kind, std::uintptr_t entity, CurrentModelRef& out) {
    out = {};
    auto fail = [&](AcquireFailure why) { note(why); return false; };
    if (kind != EntityKind::Npc || !layout::npcModelHighlights() || !entity)
        return fail(AcquireFailure::CapabilityUnavailable);
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::uintptr_t actorVtable = 0;
    if (!read(entity, actorVtable) || !actorVtable || !readable(actorVtable, sizeof(std::uintptr_t)))
        return fail(AcquireFailure::ActorUnreadable);
    std::uintptr_t entry = 0;
    if (!read(actorVtable + off::NPC_MODEL_ENTRY_SLOT, entry) || entry != base + off::NPC_GET_MODEL_ENTRY)
        return fail(AcquireFailure::ActorEntryMismatch);
    std::uintptr_t resource = 0;
    if (!read(entity + off::NPC_MODEL_RESOURCE, resource) || !resource)
        return fail(AcquireFailure::ResourceMissing);
    std::uintptr_t client = 0;
    if (!read(base + off::CLIENT_OBJ_PTR, client) || !client)
        return fail(AcquireFailure::ClientUnavailable);
    std::int32_t cycle = 0;
    if (!read(client + off::CYCLE, cycle))
        return fail(AcquireFailure::CycleUnreadable);

    using GetModel = void (__fastcall*)(std::uintptr_t, std::uintptr_t*, std::int32_t);
    auto getModel = reinterpret_cast<GetModel>(entry);
    std::uintptr_t pair[2] = { 0, 0 };
    getModel(entity, pair, cycle);
    if (!pair[0] && !pair[1]) return fail(AcquireFailure::PairEmpty);
    out.control = pair[0];
    out.model = pair[1];
    note(AcquireFailure::Acquired);
    return true;
}

// The transform is kept separate from acquisition and projection so the axis
// convention can be corrected from a render-path trace without touching either.
// These are the current OSRS-style hypotheses: yaw is 2048 units/revolution and
// model Y is upward while the client's fine-height datum is downward.
inline WorldPoint modelToWorld(const Vertex& v, const EntityPose& p) {
    constexpr float tau = 6.28318530717958647692f;
    const float a = static_cast<float>(p.orientation & 2047) * tau / 2048.0f;
    const float s = std::sin(a), c = std::cos(a);
    const float x = static_cast<float>(v.x), z = static_cast<float>(v.z);
    return { p.fineX + x * c + z * s,
             p.fineH - static_cast<float>(v.y),
             p.fineY + z * c - x * s };
}

inline float cross(const ScreenPoint& o, const ScreenPoint& a, const ScreenPoint& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

inline std::vector<ScreenPoint> convexHull(std::vector<ScreenPoint> points) {
    std::sort(points.begin(), points.end(), [](const ScreenPoint& a, const ScreenPoint& b) {
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
    points.erase(std::unique(points.begin(), points.end(), [](const ScreenPoint& a, const ScreenPoint& b) {
        return std::fabs(a.x - b.x) < 0.01f && std::fabs(a.y - b.y) < 0.01f;
    }), points.end());
    if (points.size() <= 3) return points;
    std::vector<ScreenPoint> h(points.size() * 2);
    std::size_t k = 0;
    for (const auto& p : points) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], p) <= 0.0f) --k;
        h[k++] = p;
    }
    for (std::size_t i = points.size(), t = k + 1; i > 0; --i) {
        const auto& p = points[i - 1];
        while (k >= t && cross(h[k - 2], h[k - 1], p) <= 0.0f) --k;
        h[k++] = p;
    }
    if (k > 1) --k;
    h.resize(k);
    return h;
}

// A model's projected vertices are enough for a closer outline even when the face-index array has
// not been recovered yet. The old convex hull discarded every inward corner and made characters
// look like boxes. Keep the farthest sample in angular bins around the projected model centre;
// this produces a bounded, non-convex silhouette from the same read-only vertex snapshot. It is
// deliberately an outline approximation, not a claim that we know the renderer's triangle topology.
inline std::vector<ScreenPoint> radialSilhouette(const std::vector<ScreenPoint>& points) {
    constexpr int BINS = 96;
    constexpr float TWO_PI = 6.28318530717958647692f;
    if (points.size() < 12) return convexHull(points);

    ScreenPoint centre{};
    for (const auto& p : points) { centre.x += p.x; centre.y += p.y; }
    centre.x /= static_cast<float>(points.size());
    centre.y /= static_cast<float>(points.size());

    std::vector<ScreenPoint> edge(BINS);
    std::vector<float> radius(BINS, -1.0f);
    for (const auto& p : points) {
        const float dx = p.x - centre.x, dy = p.y - centre.y;
        const float r2 = dx * dx + dy * dy;
        if (r2 < 1.0f) continue;
        float angle = std::atan2(dy, dx);
        if (angle < 0.0f) angle += TWO_PI;
        const int bin = (std::min)(BINS - 1, static_cast<int>(angle / TWO_PI * BINS));
        if (r2 > radius[bin]) { radius[bin] = r2; edge[bin] = p; }
    }

    std::vector<ScreenPoint> outline;
    outline.reserve(BINS);
    for (int i = 0; i < BINS; ++i)
        if (radius[i] >= 0.0f) outline.push_back(edge[i]);
    // Sparse coverage means the model is mostly behind the camera; the convex fallback is more
    // honest than a polygon with long chords across missing angular sectors.
    return outline.size() >= 8 ? outline : convexHull(points);
}

template <class Project>
inline bool projectSnapshot(const Snapshot& snapshot, const EntityPose& pose,
                            Project&& project, std::vector<ScreenPoint>& out) {
    out.clear();
    out.reserve(snapshot.vertices.size());
    for (const auto& v : snapshot.vertices) {
        const WorldPoint w = modelToWorld(v, pose);
        ScreenPoint s;
        if (project(w.x, w.h, w.y, s) && std::isfinite(s.x) && std::isfinite(s.y))
            out.push_back(s);
    }
    out = radialSilhouette(out);
    return out.size() >= 3;
}


} // namespace kk::model
