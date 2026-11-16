# Installing Project Prime on PS4

Download and unpack the **one release ZIP**. It includes the PS4 PKG in `PS4/`,
the Windows helper in `GamePrep/`, controls and these instructions. You'll also
need a homebrew-enabled PS4 and your own original GameCube Metroid Prime image.
PS4 Slim has been tested; there isn't an established firmware or PS4 Pro list.

## 1. Get the ISO ready

**Already have an ISO or GCM?** A raw GameCube ISO/GCM is ready to use. Name it
`MetroidPrime.iso`. GamePrep can also check it before you copy it.

**Have an RVZ or ZIP?** Use the included **GamePrep PC helper**:

1. Open the `GamePrep/` folder from the unpacked release download.
2. Double-click `GamePrep.exe` and choose your file, or drag the file onto the EXE.
3. Wait for conversion and verification to finish.
4. Use `MetroidPrime.iso` inside the new `ProjectPrimeData` folder next to your input.

No Python installation is needed. If that output folder already exists, GamePrep
keeps it and creates `ProjectPrimeData-2` (or the next available number). The
window shows progress and the finished file's location.

RVZ is a lossless compressed disc format used by Dolphin, which recommends it
for compressed dumps. The PS4 port reads a raw ISO, so RVZ needs conversion
first. Renaming an RVZ to `.iso` does not convert it.
[Dolphin's format notes](https://dolphin-emu.org/docs/faq/#what-dump-formats-are-supported-by-dolphin)
explain the difference.

A ZIP must contain one supported ISO, GCM or RVZ. GamePrep preserves the input
and writes into a fresh output folder. It also produces an import receipt with
the image identity and SHA-256. If it rejects the image, use its error message;
don't copy a partial output to the PS4.

## 2. Install and copy

1. Install the PKG with your homebrew package installer, from USB or `/data/pkg/`.
2. In your FTP client, create `/data/project-prime/data/` on the PS4.
3. Transfer the prepared ISO in binary mode to:

   ```text
   /data/project-prime/data/MetroidPrime.iso
   ```

4. Wait for the transfer to finish, then launch Metroid Prime from the home screen.

Copy the ISO itself, not its enclosing folder, RVZ or ZIP. You can also use a
console file manager to copy it from USB to that same internal path. The port
does not automatically import a file left on USB.

The tested USA image is about 1.36 GiB. Leave additional room for the application,
saves and caches. The first launch can take longer while shaders are prepared.
The game reads the ISO directly; there is no texture-pack extraction step.

## Controls and updates

- **Options:** pause.
- **Hold touchpad click for about 0.8 seconds:** port graphics/controller settings.
- Saves and settings: `/data/project-prime/release/user/`.
- Disposable shader caches: `/data/project-prime/release/cache/`.
- Diagnostic logs: `/data/project-prime/release/logs/`.

Back up `user/` before changing builds. Keep that folder when updating or
clearing a cache. Updates use the same application identity, `PRME00013`.
FTP is only needed for copying files and collecting logs, not for playing.

## Accepted game versions

| GameCube disc | Reader accepts | PS4 gameplay testing |
|---|---|---|
| USA — `GM8E01` | Revisions 0, 1, 2 | Revision 0 tested |
| PAL — `GM8P01` | Revision 0 | Not yet accepted |

Disc number must be zero. Japanese, Wii/Trilogy and Switch images are not supported.
This build does not use Switch keys or Remastered assets.

Source and command-line tools: [GitHub](https://github.com/odiecay/metroid-prime-ps4).
