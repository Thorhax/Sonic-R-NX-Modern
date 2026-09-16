# Playing Sonic R on Nintendo Switch

*Looking for Windows / macOS / Linux or Dreamcast instead? See [desktop.md](desktop.md) or [dreamcast.md](dreamcast.md). Back to the [README](../README.md).*

> **You supply your own game data.** This port is **only the game engine** — it does **not** include any of Sonic R's proprietary assets. You must own a copy of the game to supply the track, model, texture, and music data. See [Game data you supply](#game-data-you-supply).

---

## Installation

### 1. Download Release
1. Download `Sonic-R-NX-Modern-v1.0.0.zip` (or `sonicr.nro`) from the [Releases](https://github.com/Thorhax/Sonic-R-NX-Modern/releases) section.
2. Extract the archive onto your Nintendo Switch microSD card root.
   - The executable should reside at: `sdmc:/switch/sonicr/sonicr.nro`

### 2. Copy Game Data
Copy the required retail data folders from your PC copy of Sonic R into the `sdmc:/switch/sonicr/` directory:

| Folder / File | Description |
|---|---|
| `AI/` | Opponent AI navigation and pathfinding data |
| `BIN/` | 3D models, menu screens, title graphics, font data |
| `CITY/` | Radical City track geometry and textures |
| `EMERALD/` | Radiant Emerald track geometry and textures |
| `FACTORY/` | Reactive Factory track geometry and textures |
| `GENERAL/` | Shared textures, character art, weather effects (`SONICR.BIT`) |
| `ISLAND/` | Resort Island track geometry and textures |
| `RUIN/` | Regal Ruin track geometry and textures |
| `SOUND/` | Sound effect WAV/ADP banks |
| `SONICR.INF` | Original game configuration / version stamp |

> **Note:** Folders must be uppercase (or matching standard layout) so the filesystem finds them reliably.

### 3. Soundtrack (BGM)
Place your soundtrack audio tracks in:
`sdmc:/switch/sonicr/MUSIC/`

Name the tracks by CD track number:
- `track2.<ext>` through `track21.<ext>`

Supported music formats:
- `.adx` (CRI ADX ADPCM)
- `.son` (Raw 44.1kHz 16-bit stereo PCM)
- `.wav` (PCM WAV)
- `.adp` (Yamaha ADPCM)
- `.ogg` (Ogg Vorbis)
- `.mp3` (MPEG Layer 3)
- `.flac` (FLAC)

### 4. Launching the Game
1. Insert your microSD card into your Nintendo Switch.
2. Open the Homebrew Menu (hbmenu).
   - **Recommended:** Launch the Homebrew Menu via **Title Override** (hold **R** while launching any installed Switch game / cartridge) for full RAM access rather than Applet Mode.
3. Select **Sonic R** and play!

---

## Controls

The Nintendo Switch port supports both Handheld Mode (Joy-Cons) and docked/tabletop play with Pro Controllers:

| Switch Button | In-Race Action | Menu Action |
|---|---|---|
| **Left Analog Stick / D-Pad** | Steer Left / Right | Navigate Menu |
| **B Button** | Jump / Confirm | Confirm / Select |
| **A Button** | Accelerate / Action | Cancel / Back |
| **Y Button** | Brake / Reverse / Drift | - |
| **X Button** | Look Behind / Change View | - |
| **L / R Shoulders** | Lean / Sharp Drift Turn | Page Left / Right |
| **ZL / ZR Triggers** | Accelerate / Brake | - |
| **Plus (+)** | Pause Game / Start | Select / Start |
| **Minus (-)** | Options Menu | Back |
| **Minus (-) + Plus (+)** | Quick Exit to hbmenu | Quick Exit |

---

## Building from Source

Building requires [devkitPro](https://devkitpro.org/) with `devkitA64` and `switch-portlibs` (SDL2, SDL2_mixer, mesa/EGL, libnx).

### Using Docker (Recommended)

From the root of this repository:

```bash
docker run --rm -v $(pwd):/build -w /build/source/switch devkitpro/devkita64:latest make -j$(nproc)
```

The compiled homebrew binary `sonicr.nro` will be generated in `source/switch/`.
