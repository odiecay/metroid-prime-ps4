#!/usr/bin/env python3
"""Run real-reader host regressions under ASan and UBSan."""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
runtime = subprocess.check_output(['clang++-19', '-print-file-name=libclang_rt.asan-x86_64.so'], text=True).strip()
compiler = 'clang++-19'
if not Path(runtime).is_file():
    compiler = 'g++'
    runtime = subprocess.check_output([compiler, '-print-file-name=libasan.so'], text=True).strip()
    if not Path(runtime).is_file():
        raise SystemExit('No installed host ASan runtime')
env = os.environ.copy()
env['PRIME_READER_CXX'] = compiler
env['PRIME_READER_SANITIZERS'] = 'address,undefined'
env['LD_PRELOAD'] = runtime
env['LD_LIBRARY_PATH'] = str(Path(runtime).parent)
# CPython itself retains interpreter allocations. Reader ownership/close-once
# assertions run normally; this invocation checks memory access and undefined
# behavior, without claiming a whole-interpreter leak check.
env['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
raise SystemExit(subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests',
                                 '-p', 'test_iso_reader.py', '-v'], cwd=root, env=env).returncode)
