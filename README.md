# Sonic R NX Modern (Nintendo Switch Port)

**Author:** Thorhax

[![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?logo=kofi&logoColor=white)](https://ko-fi.com/thorhax)
[![Support on Patreon](https://img.shields.io/badge/Patreon-Support%20me-F96854?logo=patreon&logoColor=white)](https://www.patreon.com/c/Thorhax)
[![GitHub Release](https://img.shields.io/github/v/release/Thorhax/Sonic-R-NX-Modern?include_prereleases&color=blue)](https://github.com/Thorhax/Sonic-R-NX-Modern/releases)
[![devkitPro](https://img.shields.io/badge/devkitPro-devkitA64-32a852.svg)](https://devkitpro.org)

Modern Nintendo Switch port of **Sonic R** (the 1998 Sega PC release), decompiled from the original executable and rebuilt as modern, portable C.

This port runs natively on the Nintendo Switch using [devkitA64](https://devkitpro.org/), [libnx](https://github.com/switchbrew/libnx), SDL2, and hardware-accelerated OpenGL ES 2.0 (via mesa / deko3d). It delivers full 60 FPS gameplay, widescreen 720p presentation, complete Joy-Con and Pro Controller button mapping, background soundtrack playback, and time attack ghost/save support.

---

## Platforms

- **▶ Nintendo Switch** → **[docs/switch.md](docs/switch.md)** *(Quick guide below)*
- **▶ Windows, macOS & Linux** → **[docs/desktop.md](docs/desktop.md)**
- **▶ Sega Dreamcast** → **[docs/dreamcast.md](docs/dreamcast.md)**

---

## Installation & Setup Instructions

> **Asset Notice:** This repository contains **only the game engine and Switch platform code**. It does **NOT** include Sonic R's copyrighted game assets (tracks, models, textures, or soundtrack). You must supply the data files from your own legally-owned retail PC copy of Sonic R.

### 1. Download the Switch Homebrew Release
1. Go to the [Releases](https://github.com/Thorhax/Sonic-R-NX-Modern/releases) page and download `Sonic-R-NX-Modern-v1.0.0.zip` (or `sonicr.nro`).
2. Extract the `.zip` archive to the root of your Switch's microSD card.
3. The executable should be located at:
   ```
   sdmc:/switch/sonicr/sonicr.nro
   ```

### 2. Copy Game Data from PC
Copy the following data folders from your installed 1998 PC release of Sonic R into the `sdmc:/switch/sonicr/` folder on your SD card:

| Folder / File | Contents |
|---|---|
| `AI/` | AI opponent navigation data |
| `BIN/` | 3D models, fonts, menu screens, title assets |
| `CITY/` | Radical City level geometry and textures |
| `EMERALD/` | Radiant Emerald level geometry and textures |
| `FACTORY/` | Reactive Factory level geometry and textures |
| `GENERAL/` | Shared textures, character art, weather sprites (`SONICR.BIT`) |
| `ISLAND/` | Resort Island level geometry and textures |
| `RUIN/` | Regal Ruin level geometry and textures |
| `SOUND/` | Sound effects audio banks |
| `SONICR.INF` | Game configuration info file |

> **Folder Casing:** Ensure folder names are uppercase (e.g., `GENERAL`, `ISLAND`, `BIN`) for filesystem compatibility.

### 3. Setup Soundtrack Music (Optional but Recommended)
Create a `MUSIC` folder inside `sdmc:/switch/sonicr/`:
```
sdmc:/switch/sonicr/MUSIC/
```
Place your soundtrack audio tracks in this folder, named according to CD track numbers:
- `track2.<ext>` through `track21.<ext>`

Supported audio formats:
- `.adx`, `.son`, `.wav`, `.adp`, `.ogg`, `.mp3`, `.flac`

### 4. Play
1. Insert the microSD card back into your Nintendo Switch.
2. Launch the Homebrew Menu (hbmenu).
   - *Tip:* Launch hbmenu via **Title Override** (hold **R** while opening any installed game or cartridge) to provide full RAM access.
3. Select **Sonic R** from hbmenu to play!

---

## Controls

| Switch Button | Action (In-Race) | Action (Menus) |
|---|---|---|
| **Left Analog Stick / D-Pad** | Steer Left / Right | Navigate Selection |
| **B Button** | Jump / Action | Confirm / Select |
| **A Button** | Accelerate | Cancel / Back |
| **Y Button** | Brake / Reverse | - |
| **X Button** | Change View / Rear Camera | - |
| **L / R Shoulders** | Sharp Drift Turn | Previous / Next Tab |
| **ZL / ZR Triggers** | Accelerate / Brake | - |
| **Plus (+)** | Pause Game / Start | Select / Start |
| **Minus (-)** | In-Game Options | Back |
| **Minus (-) + Plus (+)** | Exit to hbmenu | Exit to hbmenu |

---

## Building from Source

Building the Nintendo Switch binary requires [devkitPro](https://devkitpro.org/) with `devkitA64` and `switch-portlibs`.

### Build with Docker

```bash
docker run --rm \
  -v $(pwd):/build \
  -w /build/source/switch \
  devkitpro/devkita64:latest \
  make -j$(nproc)
```

The compiled `sonicr.nro` will be in `source/switch/`.

---

## Support & Donations

❤️ **Enjoying the port?**
If this project helped you out, consider buying me a coffee on Ko-fi or supporting on Patreon to support future homebrew ports and updates!

[![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?logo=kofi&logoColor=white)](https://ko-fi.com/thorhax) [![Support on Patreon](https://img.shields.io/badge/Patreon-Support%20me-F96854?logo=patreon&logoColor=white)](https://www.patreon.com/c/Thorhax)

---

## Credits & Legal

- **Sega & Traveller's Tales**: Original *Sonic R* creators and IP holders.
- **jnmartin84**: Outstanding reverse engineering and C reimplementation of Sonic R ([jnmartin84/sonic-r](https://github.com/jnmartin84/sonic-r)).
- **Thorhax**: Nintendo Switch port, devkitA64 build integration, GLES2 shader pipeline, and packaging.
- **devkitPro / libnx contributors**: Switch homebrew SDK and toolchains.

This is a non-commercial preservation project and is not affiliated with or endorsed by Sega.
