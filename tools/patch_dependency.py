#!/usr/bin/env python3
"""Apply a pinned dependency patch once, rejecting unexpected source changes."""
from pathlib import Path
import subprocess
import sys

source, patch = map(lambda value: Path(value).resolve(), sys.argv[1:])
command = ['patch', '-d', str(source), '-p1', '--batch', '-i', str(patch)]
check = subprocess.run([*command, '--forward', '--dry-run'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
if check.returncode == 0:
    subprocess.run([*command, '--forward'], check=True)
else:
    reverse = subprocess.run([*command, '--reverse', '--dry-run'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if reverse.returncode:
        raise SystemExit('Dependency patch does not match pinned source:\n' + check.stdout.decode())
    print('Dependency patch already applied:', patch.name)
