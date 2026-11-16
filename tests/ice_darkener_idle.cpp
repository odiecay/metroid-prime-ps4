// The test runner supplies the actual old/new game methods and metadata query.
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using uint = unsigned;
static bool countAllocations = false;
static uint64_t allocations = 0;
void* operator new(std::size_t size) {
  if (countAllocations) ++allocations;
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static void check(bool value, const char* reason) {
  if (!value) throw std::runtime_error(reason);
}

namespace PortRoomEnv {
struct IceDarkener { float delay, factor; };
struct File { std::vector<IceDarkener> iceDarkeners; };
struct Area { File file; };
std::unordered_map<uint, Area> sAreas;
const std::vector<IceDarkener>* IceDarkeners(uint id) {
  auto it = sAreas.find(id);
  return it == sAreas.end() ? nullptr : &it->second.file.iceDarkeners;
}
#include "ice_metadata_query.inc"
}

struct SPortIceDarkenerArea {
  bool armed = false;
  std::vector<float> remaining;
};
static std::map<uint, SPortIceDarkenerArea> sPortIceDarkeners;
struct TAreaId { int index; explicit TAreaId(int i) : index(i) {} };
struct CGameArea {
  uint id;
  bool loaded;
  bool IsLoaded() const { return loaded; }
  uint GetAreaAssetId() const { return id; }
};
struct World {
  std::vector<CGameArea> areas;
  uint64_t visits = 0;
  int GetNumAreas() const { return int(areas.size()); }
  CGameArea* GetArea(TAreaId id) { ++visits; return &areas[id.index]; }
};
struct CPhysicsActor {
  std::vector<uint32_t> applied;
  void SetPortIceDarkening(float f) { applied.push_back(std::bit_cast<uint32_t>(f)); }
};
struct CObjectList {
  std::vector<CPhysicsActor*> actors;
  int GetFirstObjectIndex() { return actors.empty() ? -1 : 0; }
  int GetNextObjectIndex(int i) { return ++i < int(actors.size()) ? i : -1; }
  CPhysicsActor* operator[](int i) { return actors[i]; }
};
enum { kOL_PhysicsActor };
class CStateManager {
public:
  World* x850_world;
  CObjectList objects;
  CObjectList& ObjectListById(int) { return objects; }
  void PortUpdateIceDarkeners(float);
  void ReferenceUpdateIceDarkeners(float);
};
#include "ice_current_method.inc"
#include "ice_reference_method.inc"

using Method = void (CStateManager::*)(float);
constexpr Method current = &CStateManager::PortUpdateIceDarkeners;
constexpr Method reference = &CStateManager::ReferenceUpdateIceDarkeners;

static World makeWorld() {
  World w;
  for (uint i = 0; i < 256; ++i) w.areas.push_back({i + 1, i < 8});
  return w;
}

struct Observation {
  std::vector<std::pair<uint, std::vector<uint32_t>>> timers;
  std::vector<uint32_t> actor;
  bool operator==(const Observation&) const = default;
};
static Observation observe(const CPhysicsActor& actor) {
  Observation out;
  out.actor = actor.applied;
  for (const auto& [id, state] : sPortIceDarkeners) {
    if (state.remaining.empty()) continue; // empty bookkeeping never affects an actor
    std::vector<uint32_t> bits;
    for (float t : state.remaining) bits.push_back(std::bit_cast<uint32_t>(t));
    out.timers.push_back({id, std::move(bits)});
  }
  return out;
}

static std::vector<Observation> lifecycle(Method method) {
  sPortIceDarkeners.clear();
  PortRoomEnv::sAreas.clear();
  World world = makeWorld();
  CPhysicsActor actor;
  CStateManager mgr{&world, {{&actor, nullptr}}};
  std::vector<Observation> output;
  auto tick = [&] { (mgr.*method)(0.02f); output.push_back(observe(actor)); };
  for (int i = 0; i < 8; ++i) tick();
  // Metadata arrives after the world is already loaded. It must arm next tick.
  PortRoomEnv::sAreas[3].file.iceDarkeners = {{0.03f, 0.5f}, {0.08f, 0.75f}};
  tick();
  tick();
  // A profile reload removes the metadata while an existing timer is pending.
  PortRoomEnv::sAreas.clear();
  tick();
  tick();
  PortRoomEnv::sAreas[3].file.iceDarkeners = {{0.03f, 0.5f}, {0.08f, 0.75f}};
  for (int i = 0; i < 8; ++i) tick();
  check(actor.applied == std::vector<uint32_t>{std::bit_cast<uint32_t>(0.5f), std::bit_cast<uint32_t>(0.75f)},
        "expected each authored darkener exactly once");
  // Unload retires previously armed/fired bookkeeping; it must not reset the
  // actor's retained factor, which is part of the existing game behaviour.
  for (auto& area : world.areas) area.loaded = false;
  tick();
  check(sPortIceDarkeners.empty(), "unloaded darkener state was retained");
  check(actor.applied.size() == 2, "unload unexpectedly rewrote actor lighting");
  PortRoomEnv::sAreas.clear();
  tick();
  world.areas[2].loaded = true;
  PortRoomEnv::sAreas[3].file.iceDarkeners = {{0.f, 0.9f}};
  tick();
  tick();
  check(actor.applied.back() == std::bit_cast<uint32_t>(0.9f), "reloaded room was not rearmed");
  return output;
}

static std::pair<double, uint64_t> idle(Method method, bool count) {
  sPortIceDarkeners.clear();
  PortRoomEnv::sAreas.clear();
  for (uint i = 1; i <= 8; ++i) PortRoomEnv::sAreas[i];
  World world = makeWorld();
  CStateManager mgr{&world, {}};
  for (int i = 0; i < 8; ++i) (mgr.*method)(1.f / 60.f);
  allocations = 0;
  countAllocations = count;
  constexpr uint frames = 25000;
  const auto start = std::chrono::steady_clock::now();
  for (uint i = 0; i < frames; ++i) (mgr.*method)(1.f / 60.f);
  const auto end = std::chrono::steady_clock::now();
  countAllocations = false;
  if (method == current) check(world.visits == 0, "empty metadata still scanned the world");
  return {std::chrono::duration<double, std::nano>(end - start).count() / frames, allocations};
}

int main() {
  try {
    check(lifecycle(reference) == lifecycle(current), "timer/callback/lighting outputs changed");
    auto oldAlloc = idle(reference, true);
    auto newAlloc = idle(current, true);
    check(oldAlloc.second == 100000, "unexpected reference vector allocation count");
    check(newAlloc.second == 0, "Original HD fast path allocated");
    std::vector<double> oldNs, newNs;
    for (int repeat = 0; repeat < 4; ++repeat) {
      oldNs.push_back(idle(reference, false).first);
      newNs.push_back(idle(current, false).first);
      newNs.push_back(idle(current, false).first);
      oldNs.push_back(idle(reference, false).first);
    }
    std::sort(oldNs.begin(), oldNs.end());
    std::sort(newNs.begin(), newNs.end());
    std::cout << "ICE_IDLE old_allocations=" << oldAlloc.second << " new_allocations=" << newAlloc.second
              << " frames=25000 world_areas=256 loaded_areas=8 old_median_ns=" << oldNs[4]
              << " new_median_ns=" << newNs[4] << " lifecycle_equivalent=1\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
