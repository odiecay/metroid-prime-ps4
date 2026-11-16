#include <cassert>
#include <cstdio>
#include <vector>

using ushort = unsigned short;
using s16 = short;
enum GXTexFilter { GX_NEAR, GX_LINEAR };
enum { GX_TF_I8, GX_CLAMP, FALSE, GX_ANISO_1, GX_TEXMAP0, GX_TEXMAP1, GX_TEXMAP2 };
struct GXTexObj { int plane; };
struct Sample { int width, height; void* data; GXTexFilter min, mag; };
std::vector<Sample> samples;
std::vector<int> events;
void GXInitTexObj(GXTexObj* o, void* data, ushort w, ushort h, int, int, int, int) {
  o->plane = int(samples.size()); samples.push_back({w, h, data});
}
void GXInitTexObjLOD(GXTexObj* o, GXTexFilter min, GXTexFilter mag, float, float, float, int, int, int) {
  samples[o->plane].min = min; samples[o->plane].mag = mag;
}
void GXLoadTexObj(GXTexObj* o, int) { events.push_back(10 + o->plane); }
void GXDestroyTexObj(GXTexObj* o) { events.push_back(20 + o->plane); }
struct CTexture { static void InvalidateTexmap(int) { events.push_back(30); } };
#include "frontend_video_filter.inc"

int main() {
  int planes[3] = {};
  for (bool linear : {false, true}) {
    samples.clear(); events.clear();
    MyTHPYuv2RgbTextureSetup(&planes[0], &planes[1], &planes[2], 640, 480, linear);
    assert(samples.size() == 3);
    for (int i = 0; i < 3; ++i) {
      const auto& sample = samples[i];
      assert(sample.data == &planes[i]);
      assert(sample.width == (i == 0 ? 640 : 320));
      assert(sample.height == (i == 0 ? 480 : 240));
      assert(sample.min == (linear ? GX_LINEAR : GX_NEAR));
      assert(sample.mag == (linear ? GX_LINEAR : GX_NEAR));
    }
    assert(events == std::vector<int>({10, 11, 12, 20, 21, 22, 30, 30, 30}));
  }
  std::puts("actual_yuv_planes=3 filters=nearest+linear texture_ownership_unchanged=1");
}
