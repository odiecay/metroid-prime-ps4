#include "port_frontend_layout.h"

#include <cassert>
#include <cmath>
#include <cstdio>

struct CVector3f {};
struct CTransform4f {
  static CTransform4f Translate(const CVector3f&) { return {}; }
};
inline CTransform4f operator*(const CTransform4f&, const CTransform4f&) { return {}; }
struct CGuiWidgetDrawParms {
  CVector3f GetCameraOffset() const { return {}; }
};
struct CGuiWidget {
  void Draw(const CGuiWidgetDrawParms&) const {}
  CTransform4f GetWorldTransform() const { return {}; }
};

namespace PortDebug {
enum { kAspect_4_3, kAspect_Wide };
int aspect = kAspect_Wide;
int AspectMode() { return aspect; }
int HudScale() { return 100; }
bool HudWide() { return false; }
}
namespace CGraphics {
int width = 1920, height = 1080;
float projection[6];
int GetViewportWidth() { return width; }
int GetViewportHeight() { return height; }
void GetViewport(int& l, int& t, int& w, int& h) { l = 0; t = 0; w = width; h = height; }
void SetPerspective(float fov, float aspect, float near, float far) {
  projection[0] = fov; projection[1] = aspect; projection[2] = near; projection[3] = far;
}
void SetOrtho(float left, float right, float top, float bottom, float near, float far) {
  const float p[] = {left, right, top, bottom, near, far};
  for (int i = 0; i < 6; ++i) projection[i] = p[i];
}
void SetViewPointMatrix(const CTransform4f&) {}
}
struct CGuiCamera : CGuiWidget {
  enum { kProjection_Perspective, kProjection_Orthographic };
  union {
    struct { float fov, aspect, znear, zfar; } perspective;
    struct { float left, right, top, bottom, znear, zfar; } orthographic;
  } mCameraParms;
  int xb8_projection = kProjection_Orthographic;
  bool xb9_aspectMatch = true, mHudScaled = false, mSpreadable = false;
  bool mFrontEndFullscreen = false;
  mutable float mSpread, mSpreadCenterX, mSpreadY, mSpreadHalfTan, mHudScale;
  mutable bool mSpreadAboutEye;
  mutable float mCenterX, mCenterZ, mDrawTanX, mDrawTanY;
  mutable int mClipHeight, mClipWidth, mClipLeft, mClipTop;
  mutable CTransform4f mSpreadView;
  void Draw(const CGuiWidgetDrawParms&) const;
};

// The complete production method is extracted by the Python driver. The
// graphics seam records projections without needing a host window or device.
#include "frontend_camera_draw.inc"

static void Near(float a, float b, float tolerance = 0.001f) {
  if (std::abs(a - b) > tolerance) {
    std::fprintf(stderr, "expected %.6f, got %.6f\n", b, a);
    std::abort();
  }
}

int main() {
  using namespace PortFrontEndLayout;
  assert(EmbeddedPicture(640, 480));
  assert(!EmbeddedPicture(640, 448)); // anamorphic Trilogy
  assert(!EmbeddedPicture(1920, 1080));
  assert(!EmbeddedPicture(1280, 720));
  assert(!EmbeddedPicture(0, 0));
#ifdef __ORBIS__
  assert(EmbeddedPicture(960, 720));
  assert(EmbeddedPicture(1280, 960));
  assert(EmbeddedPicture(1920, 1440));
  assert(EmbeddedPicture(2560, 1920));
  const unsigned scaledMovies[][2] = {{960, 720}, {1280, 960}, {1920, 1440}, {2560, 1920}};
  for (const auto& scaled : scaledMovies) {
    assert(FillPicture(true, true, true, scaled[0], scaled[1]));
    assert(!FillPicture(true, true, false, scaled[0], scaled[1]));
    assert(!FillPicture(false, true, true, scaled[0], scaled[1]));
    assert(!FillPicture(true, false, true, scaled[0], scaled[1]));
  }
#else
  assert(!EmbeddedPicture(960, 720));
  assert(!EmbeddedPicture(1280, 960));
  assert(!EmbeddedPicture(1920, 1440));
  assert(!EmbeddedPicture(2560, 1920));
#endif
  assert(!EmbeddedPicture(1280, 959));
  assert(!EmbeddedPicture(1920, 1441));
  assert(!EmbeddedPicture(800, 600));
  assert(!EmbeddedPicture(3840, 2880));
  assert(FillPicture(true, true, true, 640, 480));
  assert(!FillPicture(false, true, true, 640, 480));
  assert(!FillPicture(true, false, true, 640, 480));
  assert(!FillPicture(true, true, false, 640, 480)); // attract or GBA movie
  assert(!FillPicture(true, true, true, 640, 448));
  assert(!FillPicture(true, true, true, 1920, 1080));
  assert(!FillPicture(true, true, true, 0, 0));
  Near(Zoom(0.f), 1.f);
  Near(Zoom(4.f / 3.f), 1.f);
  Near(Zoom(16.f / 10.f), 1.2f);
  Near(Zoom(16.f / 9.f), 4.f / 3.f);
  Near(Zoom(32.f / 9.f), 4.f / 3.f);

  // Bounds of all native text panes in the supported USA menu resources,
  // measured before changing the camera. Includes shadows, New Game choices,
  // file dates, right-hand options and confirm/cancel. No proprietary assets.
  const float panes[][4] = {
      {21.8f, 61.f, 158.3f, 91.1f}, {47.4f, 120.f, 189.8f, 261.9f},
      {52.f, 120.f, 542.4f, 150.7f}, {235.9f, 120.f, 612.7f, 222.8f},
      {52.5f, 284.3f, 189.8f, 353.2f}, {134.9f, 344.3f, 512.7f, 382.2f},
      {213.f, 120.f, 425.6f, 149.7f}, {460.1f, 120.f, 596.2f, 298.4f}};
  const int sizes[][2] = {{640, 480}, {852, 480}, {853, 480}, {1280, 720},
                          {1600, 900}, {1917, 1080}, {1920, 1080}, {1920, 1200},
                          {2560, 1080}, {3840, 1080}, {480, 800}};
  int cases = 0;
  for (const auto& size : sizes) {
    CGraphics::width = size[0]; CGraphics::height = size[1];
    const float aspect = float(size[0]) / size[1];
    const float zoom = Zoom(aspect);
    CGuiCamera camera;
    camera.mCameraParms.orthographic = {-15.7f, 15.7f, 11.5f, -11.5f, 1.f, 4096.f};
    camera.Draw({});
    float original[6];
    for (int i = 0; i < 6; ++i) original[i] = CGraphics::projection[i];
    camera.mFrontEndFullscreen = true;
    camera.Draw({});
    Near(CGraphics::projection[1] - CGraphics::projection[0], (original[1] - original[0]) / zoom);
    Near(CGraphics::projection[2] - CGraphics::projection[3], (original[2] - original[3]) / zoom);
    Near(CGraphics::projection[4], 1.f); Near(CGraphics::projection[5], 4096.f);
    Near(camera.mSpread, 1.f); Near(camera.mHudScale, 1.f);
    for (const auto& pane : panes) {
      for (int corner = 0; corner < 4; ++corner) {
        const float x = pane[(corner & 1) ? 2 : 0];
        const float y = pane[(corner & 2) ? 3 : 1];
        const float worldX = (x / 640.f - 0.5f) * 31.4f;
        const float worldY = (0.5f - y / 480.f) * 23.f;
        const float drawnX = (worldX - CGraphics::projection[0]) /
                             (CGraphics::projection[1] - CGraphics::projection[0]);
        const float drawnY = (CGraphics::projection[2] - worldY) /
                             (CGraphics::projection[2] - CGraphics::projection[3]);
        assert(drawnX >= 0.f && drawnX <= 1.f && drawnY >= 0.f && drawnY <= 1.f);
      }
    }
    if (aspect > 4.f / 3.f) {
      const MovieRect rect = PictureRect(17.f, 29.f, size[0], size[1]);
      // Every axis has exactly the same scale. The 60..420 picture band fills
      // 16:9 and remains centred, without over-zoom on ultrawide displays.
      Near((rect.right - rect.left) / 640.f, (rect.bottom - rect.top) / 480.f);
      Near((rect.left + rect.right) * 0.5f, 17.f + size[0] * 0.5f);
      Near((rect.top + rect.bottom) * 0.5f, 29.f + size[1] * 0.5f);
      if (size[0] * 9 == size[1] * 16) {
        Near(rect.left, 17.f); Near(rect.right, 17.f + size[0]);
        Near(rect.top + (rect.bottom - rect.top) * 0.125f, 29.f);
        Near(rect.top + (rect.bottom - rect.top) * 0.875f, 29.f + size[1]);
      }
      const float promptHeight = size[1] / 480.f * zoom * 32.f;
      const float prompt = PromptPosition(72.f, size[1], zoom, promptHeight);
      assert(prompt > 0.f && prompt < size[1]);
      assert(prompt - promptHeight * 0.5f >= size[1] / 24.f - 0.001f);
    }
    ++cases;
  }
  // A 4:3 profile must retain the authored camera exactly even on a wide
  // output. Other GUI cameras never opt in and remain unchanged.
  PortDebug::aspect = PortDebug::kAspect_4_3;
  CGraphics::width = 1920; CGraphics::height = 1080;
  CGuiCamera original;
  original.mFrontEndFullscreen = true;
  original.mCameraParms.orthographic = {-15.7f, 15.7f, 11.5f, -11.5f, 1.f, 4096.f};
  original.Draw({});
  Near(CGraphics::projection[0], -15.7f); Near(CGraphics::projection[1], 15.7f);
  Near(CGraphics::projection[2], 11.5f); Near(CGraphics::projection[3], -11.5f);
  PortDebug::aspect = PortDebug::kAspect_Wide;
  CGuiCamera perspective;
  perspective.xb8_projection = CGuiCamera::kProjection_Perspective;
  perspective.mCameraParms.perspective = {55.f, 4.f / 3.f, 0.2f, 8000.f};
  perspective.Draw({});
  const float tanY = perspective.mDrawTanY;
  perspective.mFrontEndFullscreen = true;
  perspective.Draw({});
  Near(perspective.mDrawTanY, tanY * 0.75f);
  Near(perspective.mDrawTanX / perspective.mDrawTanY, 16.f / 9.f);
  std::printf("actual_camera_aspects=%d native_pane_bounds=8 all_corners_visible=1 movie_scale_uniform=1\n", cases);
}
