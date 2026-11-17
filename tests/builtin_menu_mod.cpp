#include <port_remastered_storage.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace fs=std::filesystem;
using PortUtf8::FromUtf8;
using PortUtf8::ToUtf8;
using PortRemasteredStorage::ModFolder;
static std::string gBuiltinMenuPath,gShared,gRemastered;
namespace PortRemastered { std::string InstalledModFolder() { return gRemastered; } }
namespace PortDebug {
static bool enabled=true;
static std::string disabled;
static bool ModsEnabled() { return enabled; }
static std::string ModsDisabled() { return disabled; }
}
static std::string Folder() { return gShared; }
#include "actual_menu_scan.inc"
#include "actual_split_disabled.inc"
#include "actual_any_mod_enabled.inc"
static bool same(const std::vector<ModFolder>& a,const std::vector<ModFolder>& b) {
  if(a.size()!=b.size())return false;
  for(size_t i=0;i<a.size();++i)
    if(a[i].name!=b[i].name || a[i].path!=b[i].path || a[i].builtin!=b[i].builtin)return false;
  return true;
}
static const ModFolder& find(const std::vector<ModFolder>& mods,const std::string& name) {
  for(auto& mod:mods)if(mod.name==name)return mod;
  std::abort();
}
static std::vector<std::string> names(const std::vector<ModFolder>& mods) {
  std::vector<std::string> result;for(auto& mod:mods)result.push_back(mod.name);return result;
}
static std::vector<std::string> tree(const fs::path& root) {
  std::vector<std::string> result;
  for(const auto& entry:fs::recursive_directory_iterator(root)) {
    auto text=entry.path().lexically_relative(root).string();
    if(entry.is_regular_file()) {std::ifstream f(entry.path());text+=':'+std::string(std::istreambuf_iterator<char>(f),{});}
    result.push_back(text);
  }
  std::sort(result.begin(),result.end());return result;
}
int main(int argc,char** argv) {
  assert(argc==2);const fs::path root=argv[1];fs::remove_all(root);fs::create_directories(root);
  const auto shared=root/"shared";gShared=shared.string();gBuiltinMenuPath=(root/"app0/menus/hd-menu").string();
  for(const auto* name:{"aaa","hd-menu","remastered-models","zzz","dread-samus",".hidden"})fs::create_directories(shared/name);
  std::ofstream(shared/"stray-file")<<"retained";
  const auto remastered=root/"private/remastered-models";fs::create_directories(remastered);gRemastered=remastered.string();
  auto baseline=PortRemasteredStorage::ScanFolders(gShared,gRemastered);
  assert(same(ScanModFolders(gShared),baseline));
  assert(find(baseline,"remastered-models").builtin && !find(baseline,"hd-menu").builtin);
  fs::create_directories(fs::path(gBuiltinMenuPath).parent_path());std::ofstream(gBuiltinMenuPath)<<"not a directory";
  assert(same(ScanModFolders(gShared),baseline));fs::remove(gBuiltinMenuPath);
  fs::create_directories(fs::path(gBuiltinMenuPath)/"Video");
  for(const auto* clip:{"00_first_start.thp","01_startloop.thp","03_fileselectloop.thp"})
    std::ofstream(fs::path(gBuiltinMenuPath)/"Video"/clip)<<"fixture movie bytes";
  const auto before=tree(root);const auto mods=ScanModFolders(gShared);
#ifdef __ORBIS__
  assert(names(mods)==std::vector<std::string>({"aaa","hd-menu","remastered-models","zzz","dread-samus"}));
  assert(find(mods,"hd-menu").path==gBuiltinMenuPath && find(mods,"hd-menu").builtin);
  assert(find(mods,"remastered-models").path==gRemastered && find(mods,"remastered-models").builtin);
  assert(!find(mods,"aaa").builtin && !find(mods,"zzz").builtin && !find(mods,"dread-samus").builtin);
#else
  assert(same(mods,baseline));
#endif
  assert(tree(root)==before);
  // The existing enable/disable path sees the package name on the next scan.
  gShared=(root/"missing-shared").string();gRemastered.clear();PortDebug::disabled="remastered-models";
#ifdef __ORBIS__
  assert(AnyModEnabled());PortDebug::disabled="remastered-models/hd-menu";assert(!AnyModEnabled());
  PortDebug::disabled="hd-menu-extra";assert(AnyModEnabled());
  PortDebug::enabled=false;assert(!AnyModEnabled());PortDebug::enabled=true;
  gShared.clear();assert(ScanModFolders(gShared).size()==1);
#else
  assert(ScanModFolders(gShared).empty() && !AnyModEnabled());
#endif
  fs::remove_all(gBuiltinMenuPath);assert(ScanModFolders(gShared).empty());
  fs::remove_all(root);std::puts("builtin_menu: actual_scan+order missing/file fallback duplicate flags ordering disabled other_platform readonly=ok");
}
