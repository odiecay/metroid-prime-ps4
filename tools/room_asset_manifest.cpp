// PC-side dependency inventory. Uses the pinned game's parsers; never runs game code.
#include "port_room_geo.h"
#include "port_room_env.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <vector>

static std::vector<uint8_t> Read(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + path);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
  try {
    if (argc < 6 || (argc - 4) % 2 != 0)
      throw std::runtime_error("usage: room_asset_manifest source-lods output-lods report.json roomgeo roomenv [roomgeo roomenv ...]");
    std::string error;
    std::vector<PortRoomGeo::Lods> lods;
    if (!PortRoomGeo::ParseLods(Read(argv[1]), lods, error)) throw std::runtime_error(error);
    std::set<uint32_t> allModels;
    std::ofstream report(argv[3]);
    if (!report) throw std::runtime_error("cannot create report");
    report << "{\"schema\":1,\"rooms\":[";
    for (int at = 4; at < argc; at += 2) {
      std::vector<PortRoomGeo::Instance> instances;
      PortRoomGeo::Script script;
      if (!PortRoomGeo::Parse(Read(argv[at]), instances, error, &script))
        throw std::runtime_error(std::string(argv[at]) + ": " + error);
      PortRoomEnv::File env;
      if (!PortRoomEnv::Parse(Read(argv[at + 1]), env, error))
        throw std::runtime_error(std::string(argv[at + 1]) + ": " + error);
      std::set<uint32_t> models;
      size_t lightmapped = 0;
      for (const auto& instance : instances) {
        models.insert(instance.model);
        lightmapped += PortRoomGeo::HasLightmapLookup(instance.lightmap);
      }
      if (lightmapped && !env.lightmap.width)
        throw std::runtime_error("room has lightmap lookups without its matching lightmap");
      const size_t baseModels = models.size();
      for (const auto& table : lods)
        if (models.count(table.model))
          for (const auto& level : table.levels) models.insert(level.model);
      allModels.insert(models.begin(), models.end());
      size_t cubeBytes = 0;
      for (const auto& cube : env.cubes) cubeBytes += cube.length;
      if (at != 4) report << ',';
      // Paths/room labels are added by the Python caller, never interpolated here.
      report << "{\"instances\":" << instances.size() << ",\"base_models\":" << baseModels
             << ",\"lightmapped_instances\":" << lightmapped << ",\"script_nodes\":" << script.nodes.size()
             << ",\"script_edges\":" << script.edges.size() << ",\"environment_version\":" << env.version
             << ",\"reflection_cubes\":" << env.cubes.size() << ",\"reflection_cube_bytes\":" << cubeBytes
             << ",\"lightmap_bytes\":" << env.lightmap.length << ",\"point_lights\":" << env.lights.size()
             << ",\"sun_lights\":" << env.suns.size() << ",\"fog_hints\":" << env.fogs.size()
             << ",\"models\":[";
      bool first = true;
      for (uint32_t id : models) {
        if (!first) report << ',';
        first = false;
        report << id;
      }
      report << "]}";
    }
    report << "]}\n";
    report.close();
    std::erase_if(lods, [&](const auto& entry) { return !allModels.count(entry.model); });
    const auto selected = PortRoomGeo::WriteLods(lods);
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(selected.data()), selected.size());
    if (!out || !report) throw std::runtime_error("output write failed");
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
