from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]

class ShadowDemandTests(unittest.TestCase):
    def test_material_ownership_and_receiver_fallback_sanitized(self):
        out=ROOT/'build/shadow-demand-tests';out.mkdir(exist_ok=True)
        exe=out/'policy'
        subprocess.run(['g++-14','-std=c++20','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
            '-I'+str(ROOT/'game/MetroidPrimePort/platform/include'),str(ROOT/'tests/shadow_demand.cpp'),
            '-o',str(exe)],check=True)
        for _ in range(20):subprocess.run([exe],check=True,stdout=subprocess.PIPE)

    def test_actual_actor_renderer_retains_original_blob_fallback(self):
        source=(ROOT/'game/MetroidPrimePort/src/MetroidPrime/CActor.cpp').read_text()
        body=source.split('void CActor::AddToRenderer(',1)[1].split('\nvoid CActor::EnsureRendered(',1)[0]
        active=(ROOT/'game/MetroidPrimePort/platform/port_actor_shadow.cpp').read_text()
        active=active.split('bool Active() {',1)[1].split('\nbool ActiveForActor(',1)[0]
        prelude=r'''
#include "port_shadow_demand.h"
#include <cassert>
#include <cstdio>
static unsigned blobs,projected,models;
static bool setting=true,geometry=false,mode=true;
struct CVector3f { CVector3f(float=0,float=0,float=0){} };
struct CAABox { CAABox(CVector3f={},CVector3f={}){} };
struct Transform { CVector3f GetTranslation()const{return {};} };
struct CFrustumPlanes {bool BoxInFrustumPlanes(const CAABox&)const{return true;} };
struct CPlayerState {
 enum {kPV_XRay=3,kPV_Thermal=1};int visor=0;
 int GetActiveVisor(const struct CStateManager&)const{return visor;}
};
struct CStateManager {
 CPlayerState player;
 const CPlayerState* GetPlayerState()const{return &player;}
};
struct Shadow {
 bool PortValid()const{return true;} bool Valid()const{return true;}
 const float* PortReceiverBox()const{static float b[6]={};return b;}
 CAABox GetBounds()const{return {};} Transform GetTransform()const{return {};}
};
struct Model {void RenderParticles(const CFrustumPlanes&)const{};};
struct IRenderer {enum{kDS_SortedCallback=1};};
struct Renderer {
 void AddDrawable(const Shadow*,CVector3f,CAABox,int,int){++blobs;}
} renderer,*gpRender=&renderer;
namespace PortDebug {bool ProjectedShadows(){return setting;}}
namespace PortMods {bool RoomGeometryLoaded(){return geometry;}}
namespace PortRoomGeo {
 enum class Mode {Off,Replace}; Mode GetMode(){return mode?Mode::Replace:Mode::Off;}
}
namespace PortActorShadow {
 void Register(const struct CActor*);
 bool Active(){'''+active+r'''
}
struct CActor {
 bool xPortProjectedShadowMode=false;
 bool HasModelData()const{return true;} bool GetRenderParticleDatabaseInside()const{return false;}
 const Model* GetModelData()const{static Model m;return &m;}
 bool GetPreRenderClipped()const{return false;} bool CanRenderUnsorted(const CStateManager&)const{return true;}
 void Render(const CStateManager&)const{++models;} void EnsureRendered(const CStateManager&)const{++models;}
 bool GetDrawShadow()const{return true;}
 const Shadow* GetShadow()const{static Shadow s;return &s;}
 void AddToRenderer(const CFrustumPlanes&,const CStateManager&)const;
};
namespace PortActorShadow {void Register(const CActor*){++projected;}}
void CActor::AddToRenderer('''
        # Forward declaration must refer to the actor in the global namespace.
        prelude=prelude.replace('namespace PortActorShadow {\n void Register(const struct CActor*);',
                                'struct CActor;\nnamespace PortActorShadow {\n void Register(const CActor*);')
        main=r'''
int main(){
 CActor a;CFrustumPlanes f;CStateManager m;
 assert(!PortActorShadow::Active()); // Setting on + reserve buffers alone never replace a retail blob.
 a.AddToRenderer(f,m);assert(blobs==1 && projected==0 && models==1);
 geometry=true;assert(PortActorShadow::Active());
 a.xPortProjectedShadowMode=PortShadowDemand::RoomReceiversReady(true,false,3,2);
 a.AddToRenderer(f,m);assert(blobs==2 && projected==0); // Partial room load retains original.
 assert(PortShadowDemand::SelectProjectedMode(a.xPortProjectedShadowMode,true));
 a.AddToRenderer(f,m);assert(blobs==2 && projected==1); // Compatible receiver keeps projected path.
 geometry=false;assert(!PortActorShadow::Active());
 assert(PortShadowDemand::SelectProjectedMode(a.xPortProjectedShadowMode,false));
 a.AddToRenderer(f,m);assert(blobs==3 && projected==1); // Mod reload returns to original.
 m.player.visor=CPlayerState::kPV_Thermal;
 a.AddToRenderer(f,m);assert(blobs==3 && projected==1); // Original visor semantics retained.
 puts("actual actor renderer: original blob, partial geometry fallback, projected receivers and reload passed");
}
'''
        out=ROOT/'build/shadow-demand-tests';out.mkdir(exist_ok=True)
        unit=out/'actual_actor.cpp';unit.write_text(prelude+body+main)
        exe=out/'actual_actor'
        subprocess.run(['g++-14','-std=c++20','-DTARGET_PC','-O1','-g',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
            '-I'+str(ROOT/'game/MetroidPrimePort/platform/include'),str(unit),'-o',str(exe)],check=True)
        for _ in range(20):subprocess.run([exe],check=True,stdout=subprocess.PIPE)

if __name__=='__main__':unittest.main()
