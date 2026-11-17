# Metroid Prime PS4 — Original HD

Experience Tallon IV in beautiful 1080p on PlayStation 4.
The goal was simple: bring Metroid Prime to the PS4 in the most authentic way possible.

I'd never played Metroid Prime, and I wanted to play it on my PS4. So I built
this port.

**Project Prime** brings the original GameCube game to PS4 with widescreen HD,
DualShock 4 controls, vibration and PlayStation button prompts. Install it as a
PKG, add your own game image, and launch it from the PS4 home screen.

This was made possible by **[PrimeDecomp](https://github.com/PrimeDecomp)** and
**Odrannnn and the [MetroidPrimePort contributors](https://github.com/timehikari/metroidprimeport)**.
Their decompilation and native port are the foundation. My work adapts that
foundation to PS4, from graphics and audio to controls, storage and packaging.

[Getting started](#getting-it-running) · [Controls](docs/CONTROLS.md) ·
[Performance Benchmarks](docs/PERFORMANCE.md) · [How it works](docs/TECHNICAL.md)

## What's included

- **1920×1080 rendering in Auto mode and a widescreen HUD**, with adjustable
  graphics and display settings.
- **DualShock 4 controls and vibration**, with twin-stick aiming, original-style
  buttons and remapping.
- **PlayStation button prompts** throughout the game.
- **Fullscreen menus**, with smoother sampling and bundled 1.5× upscaled movie backgrounds.
- **Asynchronous menu-music loading**, with playback gated until every audio chunk is ready.
- **A 3D PS4 in the Options screen**, with the menu's existing lighting and animation.
- **Persistent saves and settings** that stay in place when updating the application.
- **Renderer and loading improvements**, with shader caching, less CPU draw setup
  and fewer repeated shader-constant uploads.
- A controller-accessible settings panel for aim sensitivity, deadzones, inversion
  and other port options. No keyboard required.

## About the port

The game runs natively on the PS4. Its reconstructed game code is compiled for
the console's CPU; it doesn't run inside Dolphin or emulate a GameCube. Aurora
handles the game's original graphics interfaces, Dawn/WebGPU turns that work
into Vulkan commands, and a customised Mesa RADV integration brings it to the
PS4 GPU. Audio and controller input use native PS4 services too.

Aurora, Dawn and RADV are upstream projects, each with their own contributors.
The PS4 work connects and adapts them to Orbis, with extensive optimisation for
the Jaguar CPU and GCN GPU. The aim is to make Metroid Prime feel at home on PS4
while keeping the original game's look. **Original HD** uses the GameCube game
and its assets; it doesn't use Nintendo's Switch Remastered assets.

## How it runs

Metroid Prime originally targeted 60 FPS, and that remains the target here.
Many of the tested scenes run close to 60, while demanding views can fall below
it. A lot of the work has gone into reducing frame-time spikes, cutting repeated
rendering work and making delivery more consistent without sacrificing the
original visual identity.

**This is not a locked 60 FPS port.** [Performance Benchmarks](docs/PERFORMANCE.md)
explains what the PS4 Slim tests show, including demanding scenes, frame-time
percentiles and the difference between CPU preparation and GPU execution.

## Download

**One MediaFire ZIP**, `Metroid-Prime-PS4-1.0.zip`, includes the PS4 PKG, ready-to-run Windows GamePrep helper,
controls, setup instructions and licences. The download link is pending;
version 1.0 has not been published. The PKG is roughly **204 MiB**, with your
game image stored separately.

## Getting it running

You'll need a homebrew-enabled PS4 that can install unsigned PKGs and your own
original GameCube Metroid Prime image.

Unpack the download once: the application is in `PS4/`, and the PC helper is
`GamePrep/GamePrep.exe`.

1. **Prepare `MetroidPrime.iso` on your PC.** A raw ISO or GCM can be named
   `MetroidPrime.iso` directly. For an RVZ or ZIP, open the included
   `GamePrep/GamePrep.exe`, or drag your file onto the EXE. It converts and checks
   the image, then puts `MetroidPrime.iso` in a new `ProjectPrimeData` folder next
   to your input. No Python installation is needed.
2. **Install the PKG**, then copy the ISO by FTP to the exact path below. Create
   the folders if needed and wait for the transfer to finish.

   ```text
   /data/project-prime/data/MetroidPrime.iso
   ```

3. **Launch from the PS4 home screen.** FTP doesn't need to stay connected.

The PKG includes the nine 1.5× menu movies; no extra PC movie-preparation step is needed.

GamePrep doesn't download the game. Renaming an RVZ to ISO doesn't convert it.
Wii Trilogy and Switch images aren't supported. See the [installation guide](docs/INSTALLING.md)
for accepted versions, conversion and updates.

## Controls and saves

**Options** pauses the game. **Hold the touchpad button for about 0.8 seconds**
to open or close the port settings. The [controls guide](docs/CONTROLS.md)
covers the layouts, beam and visor selection. On the map, use the left stick
to pan and the right stick to rotate.

Save at the game's save stations. Saves and settings live in
`/data/project-prime/release/user/`; back up that folder and keep it when updating
or clearing caches.

## Source

The PS4 backend, tools, tests and integration patches are here. Start with
[how the port works](docs/TECHNICAL.md), the [source guide](docs/SOURCE.md), or
[building from source](docs/BUILDING.md).

Testing has included real PS4 Slim gameplay and repeatable rendering workloads.
A complete playthrough has not yet been verified. When reporting a problem,
include the build, room, what you were doing and whether it happens again.

## Credits

This port rests on a great deal of work by other people:

- **[PrimeDecomp](https://github.com/PrimeDecomp)** — the original game's decompilation.
- **Odrannnn and the [MetroidPrimePort contributors](https://github.com/timehikari/metroidprimeport)** — the native game-port foundation. The link is the preserved source fork used by this build; [source provenance](docs/SOURCE.md#upstream-foundation) records the original project.
- **[Aurora contributors](https://github.com/encounter/aurora)** — the source-level graphics and platform compatibility layer.
- **[Dawn and WebGPU contributors](https://github.com/google/dawn)** — WebGPU implementation and shader translation.
- **[Mesa RADV contributors](https://docs.mesa3d.org/drivers/radv.html)** and **[orbis-ports](https://github.com/orbis-ports/mesa-ps4)** — the Vulkan driver and its PS4 adaptation.
- **[OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain)** and **[orbis-compat contributors](https://github.com/orbis-ports/orbis-compat)** — homebrew toolchain and native platform support.
- **[LLVM](https://llvm.org/)** — the compiler and C++ runtime projects.
- **[MusyX](https://github.com/AxioDL/musyx)** — the audio mixer.
- **[nod](https://github.com/encounter/nod)** — disc-image tools and reader interfaces.
- **[SDL](https://github.com/libsdl-org/SDL)** and the other dependencies listed in the [upstream notices](LICENSES/port-notice.txt).
- **Nintendo and Retro Studios** — the original Metroid Prime.

Original licences and acknowledgements are preserved in [NOTICE.md](NOTICE.md)
and [LICENSES/](LICENSES/). This is an independent, unofficial homebrew project,
unaffiliated with Nintendo, Retro Studios or Sony Interactive Entertainment.
Please report PS4-port issues here; upstream developers do not maintain this port.
