from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function_source(text, signature):
    start = text.index(signature)
    body = text.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class OriginalEffectMetricsTests(unittest.TestCase):
    def test_original_shadow_scopes_are_bounded_at_entry_and_capture(self):
        source = (ROOT / 'game/MetroidPrimePort/src/MetroidPrime/CWorldShadow.cpp').read_text()
        body = function_source(source, 'void CWorldShadow::BuildLightShadowTexture(')
        self.assertEqual(body.count('aurora::trace::Scope'), 1)
        self.assertEqual(body.count('aurora::trace::count'), 2)
        self.assertLess(body.index('original world-shadow generation'), body.index('if (x80_aid'))
        self.assertLess(body.index('original world-shadow calls'), body.index('if (x80_aid'))
        self.assertGreater(body.index('original world-shadow captures'), body.index('if (!(fov <'))
        self.assertLess(body.index('original world-shadow captures'), body.index('lightToPoint.Normalize()'))
        self.assertNotIn('cpu_clock', body)

    def test_actual_reflection_function_retains_dirty_gate_and_bounded_events(self):
        source = (ROOT / 'game/MetroidPrimePort/src/MetaRender/CCubeRenderer.cpp').read_text()
        body = function_source(source, 'void CCubeRenderer::CacheReflection(')
        self.assertEqual(body.count('aurora::trace::Scope'), 1)
        self.assertEqual(body.count('aurora::trace::count'), 2)
        prelude = r'''
#include <aurora/frame_trace.hpp>
#include <cassert>
#include <cstring>
#include <memory>
#define rs_new new
struct CVector3f {};
struct CViewport { int mLeft=3,mTop=5,mWidth=1920,mHeight=1080; };
static unsigned copies,callbacks,viewportChanges,scissorChanges;
struct CTexture {
 CTexture(int,int,int,int) {} const void* GetConstBitMapData(int)const{return this;}
};
struct TexturePtr {
 std::unique_ptr<CTexture> ptr;
 bool null()const{return !ptr;}
 CTexture* operator->()const{return ptr.get();}
 TexturePtr& operator=(CTexture* p){ptr.reset(p);return *this;}
};
enum {kTF_RGB565,GX_TF_RGB565,GX_LEQUAL};
namespace CGraphics {
 static CViewport vp;
 struct Mode {int efbHeight=1080;};static Mode mRenderModeObj;
 static void* mpSpareBuffer=nullptr;
 const CViewport& GetViewport(){return vp;}
 void SetViewport(int l,int t,int w,int h){vp={l,t,w,h};++viewportChanges;}
 void SetScissor(int,int,int,int){++scissorChanges;}
}
namespace CGX { void SetZMode(bool,int,bool){} }
namespace CCubeMaterial { CVector3f GetViewingReflection(){return {};} }
void GXSetTexCopySrc(int,int,int,int){}
void GXSetTexCopyDst(int,int,int,bool){}
void GXCopyTex(void*,bool){++copies;}
struct CCubeRenderer {
 bool x318_24_reflectionDirty=false;
 int x2dc_reflectionAge=37;
 TexturePtr x14c_reflectionTex;
 void CacheReflection(void (*cb)(void*,const CVector3f&),void*,bool);
};
'''
        main = r'''
int main(){
 CCubeRenderer renderer;
 uint64_t calls=0,captures=0,scopes=0,complete=0;
 for(unsigned i=0;i<3000;++i){
   const bool dirty=i%2==0;
   renderer.x318_24_reflectionDirty=dirty;
   const unsigned oldCallbacks=callbacks,oldCopies=copies;
   {aurora::trace::Frame f(aurora::trace::Main,i+1);
    renderer.CacheReflection([](void*,const CVector3f&){++callbacks;},nullptr,true);}
   assert(callbacks==oldCallbacks+dirty && copies==oldCopies+2*dirty);
   assert(!renderer.x318_24_reflectionDirty);
   assert(CGraphics::vp.mLeft==3 && CGraphics::vp.mTop==5);
   assert(CGraphics::vp.mWidth==1920 && CGraphics::vp.mHeight==1080);
   for(unsigned t=0;t<aurora::trace::MaxThreads;++t)
     aurora::trace::drain(t,[&](const aurora::trace::Event& e){
       assert(e.lane==aurora::trace::Main && e.frame==i+1);
       if(!strcmp(e.name,"original reflection cache calls")){assert(e.value==1);++calls;}
       if(!strcmp(e.name,"original reflection captures")){assert(e.value==1);++captures;}
       if(!strcmp(e.name,"original reflection cache (inclusive)"))++scopes;
       if(e.kind==aurora::trace::Complete)++complete;
     });
 }
 assert(calls==3000 && captures==1500 && scopes==3000 && complete==3000);
 assert(callbacks==1500 && copies==3000);
 assert(viewportChanges==3000 && scissorChanges==3000);
 for(auto& r:aurora::trace::rings)assert(r.dropped.load()==0);
 assert(aurora::trace::unregisteredDrops.load()==0);
}
'''
        out = ROOT / 'build/original-effect-metrics-tests'
        out.mkdir(exist_ok=True)
        unit = out / 'actual_reflection.cpp'
        unit.write_text(prelude + body + main)
        exe = out / 'actual_reflection'
        subprocess.run(['g++-14', '-std=c++20', '-O1', '-g', '-DTARGET_PC',
                        '-DPRIME_TRACE_TEST', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer',
                        '-I' + str(ROOT / 'game/MetroidPrimePort/extern/aurora/include'),
                        str(unit), '-o', str(exe)], check=True)
        for _ in range(10):
            subprocess.run([exe], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
