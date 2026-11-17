import os
import hashlib
from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class BenchmarkPlayLogicTests(unittest.TestCase):
    def test_actual_engine_consumes_local_input_after_aim(self):
        player=(ROOT/'game/MetroidPrimePort/src/MetroidPrime/Player/CPlayer.cpp').read_text()
        process=player[player.index('void CPlayer::ProcessInput('):]
        self.assertLess(process.index('UpdateMouseAim(mgr);'),process.index('ComputeMovement(scaledInput, mgr, dt);'))
        physics=(ROOT/'game/MetroidPrimePort/src/MetroidPrime/CPhysicsActor.cpp').read_text()
        apply=physics[physics.index('void CPhysicsActor::ApplyForceOR('):]
        self.assertIn('GetTransform().Rotate(force)',apply[:apply.index('\n}')])
        adapter=(ROOT/'platform/ps4/benchmark_game.cpp').read_text()
        v3=adapter[adapter.index('void drive(CStateManager& mgr,'):adapter.index('\n}\n}\nvoid benchmark_game_tick')]
        self.assertIn('coverage_input(',v3);self.assertIn('coverage_command(',v3)
        self.assertIn('pad.substickX=static_cast<s8>(command.aim);',v3)
        self.assertIn('stick_for_world(command.movement,',v3)
        self.assertIn('if(!benchmark_coverage_enabled()){drive_legacy(mgr,control);return;}',v3)
    def test_v2_driver_preserves_frozen_v51_body(self):
        source=(ROOT/'platform/ps4/benchmark_game.cpp').read_text()
        start=source.index('void drive_legacy(CStateManager& mgr,')
        begin=source.index('{',start);end=begin+1;depth=1
        while depth:
            depth+=(source[end]=='{')-(source[end]=='}');end+=1
        # Frozen v51 source-export body; no private build/archive dependency.
        self.assertEqual(hashlib.sha256(source[begin:end].encode()).hexdigest(),
                         'f18c5fab7efd5bf69ecd69b20f90b11fd9a09242d87629982f021fe26e5179b7')
        start=source.index('void drive(CStateManager& mgr,')
        self.assertIn('if(!benchmark_coverage_enabled()){drive_legacy(mgr,control);return;}',source[start:source.index('auto& player=',start)])
    def test_actual_placement_terminal_releases_virtual_pad(self):
        out=Path(os.environ.get('PRIME_PLAY_LOGIC_BUILD_DIR',str(ROOT/'build/benchmark-play-logic')))
        out.mkdir(parents=True,exist_ok=True)
        source=(ROOT/'platform/ps4/benchmark_game.cpp').read_text()
        start=source.index('void acknowledge_placement(')
        begin=source.index('{',start);end=begin+1;depth=1
        while depth:
            depth+=(source[end]=='{')-(source[end]=='}');end+=1
        tail=source[begin+1:end-1]
        fixture='''#include <cassert>
#include <cmath>
#include <cstdint>
struct Point { float x{},y{},z{}; };
struct Route { Point start; float length=12,openness=30,dx=1,dy=0; bool jumpSafe=true; };
struct { Route route; } adapter;
bool enabled=true,finishing=false,owned=true;unsigned calls=0,clears=0;
bool benchmark_play_enabled(){return enabled;}
void PADClearVirtualStatus(int slot){assert(slot==0);owned=false;++clears;}
void benchmark_play_placement(bool,float,float,float,float,float,bool,uint64_t,float){++calls;if(finishing)enabled=false;}
void placement_tail(bool valid,uint64_t elapsed){
Point where;
'''+tail+'''
}
int main(){for(bool valid:{false,true})for(bool terminal:{false,true}){
 enabled=owned=true;finishing=terminal;calls=clears=0;placement_tail(valid,123);
 assert(calls==1);assert(clears==unsigned(terminal));assert(owned==!terminal);
}}
'''
        fixture=fixture.replace('#include <cassert>','#include <cassert>\n#include <initializer_list>')
        path=out/'placement_tail.cpp';path.write_text(fixture);exe=out/'placement_tail'
        command=['/usr/lib/llvm-19/bin/clang++','-std=c++20','-O2','-Wall','-Wextra','-Werror',str(path),'-o',str(exe)]
        sanitized=os.environ.get('PRIME_BENCHMARK_SANITIZE')=='1'
        if sanitized:
            command+=['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-link-runtime',
                      '-fno-omit-frame-pointer','-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
        subprocess.run(command,check=True,capture_output=True,text=True)
        env=dict(os.environ)
        if sanitized:env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                               ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
        subprocess.run([str(exe)],env=env,check=True,capture_output=True,text=True)
    def test_actual_route_and_input_logic(self):
        out=Path(os.environ.get('PRIME_PLAY_LOGIC_BUILD_DIR',str(ROOT/'build/benchmark-play-logic')))
        out.mkdir(parents=True,exist_ok=True);exe=out/'run'
        # Compile the real native Aurora clamp, including its integer scaling.
        source=(ROOT/'game/MetroidPrimePort/extern/aurora/lib/dolphin/pad/pad.cpp').read_text()
        clamp=source[source.index('void ClampStick(s8*'):source.index('void PADClamp(PADStatus*')]
        (out/'actual_pad_clamp.hpp').write_text('#include <cstdint>\n#include <cmath>\nusing s8=int8_t;\n'+clamp)
        command=['/usr/lib/llvm-19/bin/clang++','-std=c++20','-O2','-Wall','-Wextra','-Werror',
                 '-I'+str(ROOT/'platform/ps4'),'-I'+str(out),
                 '-I'+str(ROOT/'game/MetroidPrimePort/platform/include'),
                 str(ROOT/'tests/benchmark_play_logic.cpp'),'-o',str(exe)]
        sanitized=os.environ.get('PRIME_BENCHMARK_SANITIZE')=='1'
        if sanitized:
            command+=['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-link-runtime',
                      '-fno-omit-frame-pointer','-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
        subprocess.run(command,check=True,capture_output=True,text=True)
        env=dict(os.environ)
        if sanitized:env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                               ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
        result=subprocess.run([str(exe)],env=env,check=True,capture_output=True,text=True)
        self.assertIn('2880 normal-input phase cases passed',result.stdout)
        self.assertIn('coverage stop/turn/go 96 latched LOCAL-pad/retail-force cases',result.stdout)
if __name__=='__main__':unittest.main()
