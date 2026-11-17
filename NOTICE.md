# Source notices

This repository contains the native PS4 integration, build tools and recorded
patches. Per-file licence headers and the original third-party notices remain
applicable. There is no blanket licence grant over the recovered game code.

The OpenOrbis-derived linker script in `platform/ps4/orbis-native.ld` retains
its GPL-3.0-only notice. The upstream port licence and NOTICE are collected
under `LICENSES/`, together with the native dependency notices and startup
module source pins. Full upstream engine and SDK trees are fetched separately
from the versions recorded in `upstream.lock.json`.

No game image, keys, saves, SDK binaries or promotional artwork are included.

The original controller diagram in `docs/images/ps4-controls.svg` is CC0-1.0.

The authored PS4 case geometry and generator retain their MIT terms; see
their SPDX headers and `LICENSES/port-license.txt`. The embedded Sony, PS and
PS4 marks use the source-linked PD-textlogo vector outlines identified in
`tools/generate_menu_console.py`.
