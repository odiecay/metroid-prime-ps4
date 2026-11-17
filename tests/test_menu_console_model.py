"""Validate our texture-free console mesh through the actual game model loader."""
import importlib.util
import json
import math
import os
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
HOST=ROOT/'build/pc-asset-importer'

class MenuConsoleModelTests(unittest.TestCase):
    def test_generated_input_matches_checked_in_header(self):
        spec=importlib.util.spec_from_file_location('menu_mesh',ROOT/'tools/generate_menu_console.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        mesh=module.build_mesh();data,bounds=module.encode(mesh)
        self.assertEqual(module.header(data),(ROOT/'platform/ps4/menu_console_model_data.hpp').read_text())
        self.assertEqual(len(data),377408)
        self.assertEqual(sum(map(len,mesh.triangles))//3,6908)
        self.assertEqual(len(mesh.vertices),9019)
        self.assertLess(max(abs(x)for x in bounds),1.6)
        self.assertTrue(all(vertex[2][3]==255 for vertex in mesh.vertices))
        for position,normal,colour in mesh.vertices:
            self.assertTrue(all(math.isfinite(x)for x in position+normal))
            self.assertAlmostEqual(sum(x*x for x in normal),1.,places=6)
        for indices in mesh.triangles:
            for offset in range(0,len(indices),3):
                a,b,c=(mesh.vertices[indices[offset+i]]for i in range(3))
                normal=module.normal(a[0],b[0],c[0])
                self.assertGreater(sum(normal[i]*a[1][i]for i in range(3)),.99999)

    def test_authentic_marks_face_outward_and_preserve_outline_holes(self):
        spec=importlib.util.spec_from_file_location('menu_mesh',ROOT/'tools/generate_menu_console.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        mesh=module.build_mesh()
        sony=(117,121,130,255);ps4=(10,13,18,255);ps=(143,150,161,255)
        for colour in (sony,ps4,ps):
            vertices=[v for v in mesh.vertices if v[2]==colour]
            self.assertGreater(len(vertices),100)
            for position,normal,c in vertices:
                self.assertGreater(normal[2] if colour==ps else -normal[1],.85)
        def covered(colour,point):
            for indices in mesh.triangles:
                for offset in range(0,len(indices),3):
                    vertices=[mesh.vertices[indices[offset+i]]for i in range(3)]
                    if vertices[0][2]!=colour:continue
                    a,b,c=[(v[0][0],v[0][2])for v in vertices]
                    def cross(a,b,p):return(b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0])
                    signs=[cross(a,b,point),cross(b,c,point),cross(c,a,point)]
                    if min(signs)>=-1e-14 or max(signs)<=1e-14:return True
            return False
        def sony_point(x,y):return(-1.255+(x-.00015754749)*.235/(1280-.00015754749),.195-y*.235/(1280-.00015754749))
        def ps4_point(x,y):return(.855+(x-180.798)*.350/(482.265-180.798),.193-(y-24.062)*.350/(482.265-180.798))
        # The authentic Sony O and PS4 numeral 4 must have genuine open counters.
        self.assertFalse(covered(sony,sony_point(456,112)))
        self.assertTrue(covered(sony,sony_point(335,112)))
        self.assertFalse(covered(ps4,ps4_point(440,62)))
        self.assertTrue(covered(ps4,ps4_point(430,78)))

    @unittest.skipUnless((HOST/'compile_commands.json').exists(),'populated host game build required')
    def test_actual_model_load_surface_fetches_and_lifetime(self):
        out=Path(os.environ.get('PRIME_MENU_BUILD_DIR',str(ROOT/'build/menu-console-model-tests')));out.mkdir(parents=True,exist_ok=True)
        recipes=json.loads((HOST/'compile_commands.json').read_text())
        sanitizer=os.environ.get('PRIME_MENU_SANITIZE')=='1'
        extra=[]
        if sanitizer:
            headers=out/'headers';headers.mkdir(exist_ok=True)
            if not(headers/'sanitizer').exists():
                (headers/'sanitizer').symlink_to('/usr/lib/gcc/x86_64-linux-gnu/14/include/sanitizer',target_is_directory=True)
            extra=['-O1','-g','-DNDEBUG_SANITIZER','-isystem',str(headers),
                   '-fsanitize=address,undefined','-fno-sanitize-link-runtime','-fno-omit-frame-pointer']
        selected=[('/port_remastered_convert_retail.cpp',ROOT/'tests/menu_console_model.cpp')]
        if sanitizer:
            selected += [(suffix,None)for suffix in ('/DolphinCModel.cpp','/CCubeModel.cpp','/CCubeSurface.cpp')]
        objects=[];removed=[]
        for suffix,source in selected:
            recipe=next(row for row in recipes if row['file'].endswith(suffix))
            args=shlex.split(recipe['command']);removed.append(args[args.index('-o')+1])
            obj=out/(Path(source or recipe['file']).stem+'.o')
            args[args.index('-o')+1]=str(obj);args[args.index('-c')+1]=str(source or recipe['file'])
            args+=extra+['-I'+str(ROOT/'platform/ps4'),'-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/lib')]
            result=subprocess.run(args,cwd=recipe['directory'],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            objects.append(str(obj))
        line=subprocess.check_output(['ninja','-C',str(HOST),'-t','commands','metroid_prime_port'],text=True).splitlines()[-1]
        tokens=shlex.split(line);args=tokens[2:tokens.index('&&',2)]
        args=[arg for arg in args if arg not in removed[1:]]
        dep=next((i for i,arg in enumerate(args)if arg.startswith('--dependency-file=')),None)
        if dep is not None:del args[dep-1:dep+1]
        exe=out/'runtime-test';args[args.index('-o')+1]=str(exe)
        args[args.index('-o'):args.index('-o')]=objects
        env=dict(os.environ)
        if sanitizer:
            args[1:1]=['-Wl,--no-as-needed','-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
            env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                       ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result=subprocess.run(args,cwd=HOST,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        result=subprocess.run([exe],env=env,capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('100 create/load/surface-walk/destroy cycles',result.stdout)
        self.assertIn('memory returns to baseline',result.stdout)
        print(result.stdout,end='')

if __name__=='__main__':unittest.main()
