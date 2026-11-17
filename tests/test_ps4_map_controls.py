"""Exercise PS4 map defaults through the actual parser, engine and PAD output."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / 'game/MetroidPrimePort'


def fn(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    end, depth = brace+1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]+'\n'


class PS4MapControlsTests(unittest.TestCase):
    def test_actual_bindings_contexts_saved_profiles_and_glyphs(self):
        dev = (GAME/'platform/input/port_input_devices.cpp').read_text()
        prompts = (GAME/'platform/port_prompts.cpp').read_text()
        parts = [fn(dev, name) for name in ('Input Make(', 'void BindIn(', 'static void BindMapDefaults(',
                 'Action PadAxisAction(', 'Action PadBitAction(', 'float PastGameClamp(', 's8 StickX(',
                 's8 StickY(', 'u8 Trigger(')]
        menu = dev[dev.index('struct SMenuAction {'):dev.index('constexpr uint16_t kMenus =')]
        prompt_parts = [fn(dev, name) for name in ('static void UpdatePromptBindings(',
                        'static bool PromptInputFor(', 'bool PromptAxisInput(')]
        generated = '\n'.join(parts)+menu+'\n'+fn(dev, 'PADStatus Synthesize(')+'\n'.join(prompt_parts)
        out = ROOT/'build/ps4-map-controls-tests'
        out.mkdir(exist_ok=True)
        (out/'map_functions.inc').write_text(generated)
        (out/'map_glyph.inc').write_text(fn(prompts, 'std::string PadStemForAxis('))
        # Every normal selected/custom profile receives the same fallback pass.
        base = fn(dev, 'void BuildBaseProfile(')
        current = fn(dev, 'void BuildProfile(')
        self.assertIn('BindMapDefaults(out);', base)
        self.assertGreater(current.index('BindMapDefaults(out);'), current.rindex('PortInput::Overlay('))
        # The original translated Rotate/Pan/Zoom labels remain attached to
        # their semantic game-stick icons; PromptAxisInput resolves physical axes.
        mapper = (GAME/'src/MetroidPrime/CAutoMapper.cpp').read_text()
        self.assertLess(mapper.index('gpTweakPlayerRes->x20_lStick[x2e4_lStickPos]'), mapper.index('strTable->GetString(0x2e)'))
        self.assertLess(mapper.index('gpTweakPlayerRes->x48_cStick[x2e8_rStickPos]'), mapper.index('strTable->GetString(0x2f)'))
        for native, sanitizer in ((False, False), (True, False), (True, True)):
            binary = out/('sanitized' if sanitizer else 'native' if native else 'other')
            command = ['g++-14', '-std=c++20', '-O2', '-DTARGET_PC', '-I'+str(out),
                '-I'+str(GAME/'platform/include'), '-I'+str(GAME/'extern/aurora/include'),
                '-I'+str(ROOT/'external/SDL-release-3.4.10/include'),
                str(ROOT/'tests/ps4_map_controls.cpp'), str(GAME/'platform/input/port_input_engine.cpp'),
                str(GAME/'platform/input/port_input_bindings.cpp'), '-o', str(binary)]
            if native:
                command += ['-D__ORBIS__']
            if sanitizer:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            subprocess.run(command, check=True)
            result = subprocess.run([binary], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertIn('map_samples=150 legacy_profiles=4 custom_preserved=1 glyphs_follow=1', result.stdout)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
