#include <cassert>
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>

using ushort = unsigned short;
using s16 = short;
using GXTexFmt = int;
enum GXTexFilter { GX_NEAR, GX_LINEAR };
enum { GX_TF_R8_PC = 99 };
enum { GX_TF_I8, GX_CLAMP, FALSE, GX_ANISO_1, GX_TEXMAP0, GX_TEXMAP1, GX_TEXMAP2 };
struct GXTexObj { int plane; };
struct Sample { int width, height; void* data; GXTexFilter min, mag; int format; };
std::vector<Sample> samples;
std::vector<int> events;
void GXInitTexObj(GXTexObj* o, void* data, ushort w, ushort h, int format, int, int, int) {
  o->plane = int(samples.size()); samples.push_back({w, h, data, {}, {}, format});
}
void GXInitTexObjLOD(GXTexObj* o, GXTexFilter min, GXTexFilter mag, float, float, float, int, int, int) {
  samples[o->plane].min = min; samples[o->plane].mag = mag;
}
void GXLoadTexObj(GXTexObj* o, int) { events.push_back(10 + o->plane); }
void GXDestroyTexObj(GXTexObj* o) { events.push_back(20 + o->plane); }
struct CTexture { static void InvalidateTexmap(int) { events.push_back(30); } };
#include "frontend_video_filter.inc"

#ifdef __ORBIS__
struct CMoviePlayer {
  struct File {std::string path; const std::string& GetFilename()const{return path;}} x0_dvdFile;
  struct Index {bool ready=true; bool null()const{return ready;}} xac_indexLoad;
  struct Comp {unsigned mNumComponents=1;unsigned char mFrameComp[2]={0,1};} x58_thpComponents;
  struct Video {unsigned mXSize=1920,mYSize=1440;} x6c_videoInfo;
  bool xf4_26_hasAudio=false;
  bool PortUsesLinearMenuPlanes()const;
};
using uint=unsigned;
#include "linear_menu_selector.inc"
#endif
int main() {
#ifdef __ORBIS__
 CMoviePlayer movie;
 for(const char* n:{"00_first_start.thp","01_startloop.thp","02_start_fileselect_A.thp","02_start_fileselect_B.thp","02_start_fileselect_C.thp","03_fileselectloop.thp","04_fileselect_playgame_A.thp","04_fileselect_playgame_B.thp","04_fileselect_playgame_C.thp"}){
  movie.x0_dvdFile.path=std::string("Video/")+n; assert(movie.PortUsesLinearMenuPlanes());
  movie.xf4_26_hasAudio=true;assert(!movie.PortUsesLinearMenuPlanes());movie.xf4_26_hasAudio=false;
  movie.x58_thpComponents.mNumComponents=2;assert(!movie.PortUsesLinearMenuPlanes());movie.x58_thpComponents.mNumComponents=1;
  movie.x58_thpComponents.mFrameComp[0]=1;assert(!movie.PortUsesLinearMenuPlanes());movie.x58_thpComponents.mFrameComp[0]=0;
  movie.xac_indexLoad.ready=false;assert(!movie.PortUsesLinearMenuPlanes());movie.xac_indexLoad.ready=true;
  movie.x6c_videoInfo={640,480};assert(!movie.PortUsesLinearMenuPlanes());movie.x6c_videoInfo={1920,1080};assert(!movie.PortUsesLinearMenuPlanes());movie.x6c_videoInfo={1920,1440};
 }
 movie.x0_dvdFile.path="Video/attract0.thp";assert(!movie.PortUsesLinearMenuPlanes());
#endif
  int planes[3] = {};
  for (bool linear : {false, true}) for (bool packed : {false, true}) {
    samples.clear(); events.clear();
    MyTHPYuv2RgbTextureSetup(&planes[0], &planes[1], &planes[2], 640, 480, linear, packed);
    assert(samples.size() == 3);
    for (int i = 0; i < 3; ++i) {
      const auto& sample = samples[i];
      assert(sample.data == &planes[i]);
#ifdef __ORBIS__
      assert(sample.format == (packed ? GX_TF_R8_PC : GX_TF_I8));
#else
      assert(sample.format == GX_TF_I8);
#endif
      assert(sample.width == (i == 0 ? 640 : 320));
      assert(sample.height == (i == 0 ? 480 : 240));
      assert(sample.min == (linear ? GX_LINEAR : GX_NEAR));
      assert(sample.mag == (linear ? GX_LINEAR : GX_NEAR));
    }
    assert(events == std::vector<int>({10, 11, 12, 20, 21, 22, 30, 30, 30}));
  }
  std::puts("actual_yuv_planes=3 filters=nearest+linear texture_ownership_unchanged=1");
}
