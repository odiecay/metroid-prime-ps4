from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]

class PortMenuShortcutTests(unittest.TestCase):
    def test_actual_binding_and_input_runtime(self):
        binary=ROOT/'build/port-menu-shortcut-test'
        subprocess.run(['g++-14','-std=c++20','-O1','-g','-fsanitize=address,undefined',
            '-I'+str(ROOT/'platform/ps4'),
            '-I'+str(ROOT/'game/MetroidPrimePort/platform/include'),
            str(ROOT/'tests/port_menu_shortcut.cpp'),
            str(ROOT/'game/MetroidPrimePort/platform/input/port_input_engine.cpp'),
            '-o',str(binary)],check=True)
        subprocess.run([binary],check=True,timeout=15)
