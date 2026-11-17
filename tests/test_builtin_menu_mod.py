"""Exercise the production scan seam with the actual storage and disabled policy."""
from pathlib import Path
import os
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
GAME=ROOT/'game/MetroidPrimePort'
def function(text,signature):
    start=text.index(signature);brace=text.index('{',start);end=brace+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
class BuiltinMenuModTests(unittest.TestCase):
    def test_actual_scan_order_disable_and_fallback(self):
        path=Path(os.environ.get('BUILTIN_MENU_CANDIDATE',str(GAME/'platform/port_mods.cpp')))
        source=path.read_text();pak=(GAME/'platform/port_mods_pak.cpp').read_text()
        out=ROOT/'build/builtin-menu-mod';out.mkdir(exist_ok=True)
        actual=function(source,'std::vector<PortRemasteredStorage::ModFolder> ScanModFolders(')
        # Redirect only the immutable mount path; all discovery and ordering
        # code is the production function with the real filesystem policy.
        actual=actual.replace('"/app0/menus/hd-menu"','gBuiltinMenuPath')
        (out/'actual_menu_scan.inc').write_text(actual)
        (out/'actual_split_disabled.inc').write_text(function(pak,'std::vector<std::string> SplitDisabled('))
        (out/'actual_any_mod_enabled.inc').write_text(function(source,'bool AnyModEnabled('))
        for native,sanitized in [(False,False),(True,False),(True,True)]:
            binary=out/('sanitized' if sanitized else 'native' if native else 'other')
            cmd=['g++-14','-std=c++20','-O2','-g','-I'+str(out),'-I'+str(GAME/'platform/include'),str(ROOT/'tests/builtin_menu_mod.cpp'),'-o',str(binary)]
            if native:cmd+=['-D__ORBIS__']
            if sanitized:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            subprocess.run(cmd,check=True)
            result=subprocess.run([binary,out/'fixture'],capture_output=True,text=True,timeout=15,
                                  env=os.environ|{'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('readonly=ok',result.stdout);print(result.stdout.strip())
if __name__=='__main__':unittest.main()
