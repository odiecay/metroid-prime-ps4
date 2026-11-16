"""Original HD preserves its warm GX cache without prewarming unused imports."""
from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]


class PipelineProfileTests(unittest.TestCase):
    def test_actual_cache_dispatch_policy_preserves_all_original_pipeline_types(self):
        source=(ROOT/'game/MetroidPrimePort/extern/aurora/lib/gfx/pipeline_cache.cpp').read_text()
        policy=source.split('static bool cached_type_selected(',1)[1].split('\nstatic void queue_cached_row_any',1)[0]
        code='''
#include <cstring>
#include <cassert>
enum class ShaderType {Clear, GX, Rml, Vfx, Water};
static bool cached_type_selected('''+policy+'''
int main() {
  for (auto type : {ShaderType::Clear, ShaderType::GX, ShaderType::Rml}) {
    assert(cached_type_selected(type,"consolidated-original-hd"));
  }
  for (auto type : {ShaderType::Vfx, ShaderType::Water}) {
    assert(!cached_type_selected(type,"consolidated-original-hd"));
    assert(cached_type_selected(type,"consolidated"));
    assert(cached_type_selected(type,nullptr));
  }
}
'''
        code='#include <initializer_list>\n'+code
        out=ROOT/'build/pipeline-profile-test';out.mkdir(exist_ok=True)
        unit=out/'policy.cpp';unit.write_text(code)
        binary=out/'policy'
        subprocess.run(['g++-14','-std=c++20','-O2','-fsanitize=address,undefined',unit,'-o',binary],check=True)
        subprocess.run([binary],check=True,timeout=10)
        # Every cached startup/context row goes through this dispatcher. Live
        # pipeline lookup is deliberately outside this selection policy.
        self.assertIn('cached_type_selected(static_cast<ShaderType>(type)',source)
