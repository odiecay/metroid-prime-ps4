from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class CacheRecoveryTests(unittest.TestCase):
    def test_production_helper_with_real_sqlite_and_corrupt_files(self):
        build=ROOT/'build/sqlite-cache-recovery';build.mkdir(exist_ok=True)
        # Keep actual production helper bodies. Only its logging and UTF-8 path
        # adapters are replaced; SQLite opening, checking and recovery are real.
        source=(ROOT/'game/MetroidPrimePort/extern/aurora/lib/sqlite_utils.hpp').read_text()
        source=source.replace('#pragma once','').replace('#include "internal.hpp"','').replace('#include "io.hpp"','')
        preamble='''#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <type_traits>
#include <string_view>
namespace fs=std::filesystem;
namespace aurora { struct Module {
 template<class... T> void warn(const char*,T&&...) {}
 template<class... T> void error(const char*,T&&...) {}
}; namespace io { inline fs::path fs_path_from_string(const std::string& p) {return fs::path(p);} } }
'''
        translation=build/'actual_helpers.cpp'
        translation.write_text(preamble+source+'\n'+(ROOT/'tests/sqlite_cache_recovery.cpp').read_text())
        sqlite=ROOT/'build/game-native/_deps/sqlite3-src'
        sqlite_obj=build/'sqlite.o'
        subprocess.run(['gcc-14','-std=c11','-O1','-D_GNU_SOURCE','-fsanitize=address,undefined',
            '-c',str(sqlite/'sqlite3.c'),'-o',str(sqlite_obj)],check=True)
        binary=build/'test'
        subprocess.run(['g++-14','-std=c++20','-O1','-fsanitize=address,undefined',
            '-I'+str(sqlite),str(translation),str(sqlite_obj),'-pthread','-ldl','-lm','-o',str(binary)],check=True)
        with tempfile.TemporaryDirectory() as temp:
            result=subprocess.run([binary,temp],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
