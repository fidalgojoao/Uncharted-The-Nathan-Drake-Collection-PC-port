# Uncharted: The Nathan Drake Collection PC — With Multiplayer

![Uncharted 2 and Uncharted 3](assets/uncharted-gallery.png)

<p align="center">
  <a href="https://recomplauncher.com/game/uncharted-the-nathan-drake-collection-pc-port-2">
    <img src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" alt="Get it on RecompLauncher">
  </a>
</p>
<p align="center">
  <a href="../../releases/latest/download/Uncharted-windows.zip"><img src="https://img.shields.io/badge/%E2%AC%87%20DOWNLOAD-%20Windows-2ea44f?style=for-the-badge"></a>
  &nbsp;
  <a href="https://discord.com/invite/T545xWcpSw"><img src="https://img.shields.io/badge/JOIN-DISCORD-5865F2?style=for-the-badge&logo=discord&logoColor=white"></a>
</p>

A free, non-commercial fan project that runs the PS4 *Uncharted: The Nathan Drake Collection* natively on Windows, using your own copy. Includes *Drake's Fortune*, *Among Thieves* and *Drake's Deception*, plus an experimental **Uncharted 2 & Uncharted 3 multiplayer restoration**.

The games' x86-64 code runs directly on your PC, with a runtime for PS4 system libraries and a Vulkan renderer. This fork adds a C++ launcher based on the original launcher.

> **No game files included. Legally obtained copies required.** Not affiliated with or endorsed by Sony Interactive Entertainment, Naughty Dog or Bluepoint Games.

## Status

- **Singleplayer:** All three games play, with working menus, game switching, saves, audio, cutscenes and controllers. Uncharted 2 and 3 have been played at up to 4K and above 60 FPS on fast GPUs; Uncharted 3 has had less testing.
[Detailed status](analysis/STATUS.md) · [Changelog](/assets/CHANGELOG.md) · [Releases](../../releases)
- **Multiplayer:** Still in testing, but you can try it out already! See the [**progress**](/assets/DEVELOPMENT_PLAN.md) for our long-term development plan! You can also join our [**Discord server**](https://discord.com/invite/T545xWcpSw) to play with the community or report any issues!

## Requirements

- **OS:** Windows 10 or 11, 64-bit.
- **RAM:** About 6 GB free; 16 GB installed recommended.
- **CPU:** Intel Core 4th generation (Haswell) or newer, or any AMD Ryzen.
- **CPU features:** AVX, F16C, BMI1, MOVBE and LZCNT.
- **Graphics:** NVIDIA, AMD or Intel GPU with Vulkan 1.3 and current drivers.
- **Game:** Your own dumped PS4 Collection; update **1.02** recommended.

Four CPU cores can hold 60 FPS in Drake's Fortune and Uncharted 3. On Ryzen 7000/9000 integrated Radeon graphics, use **Low**: tested performance is 24–39 FPS at 720p.

## Install and update

1. Download **Uncharted-windows.zip** from [Releases](../../releases/latest/download/Uncharted-windows.zip) and extract it.
2. Open **Uncharted Launcher.exe** and select your dumped `CUSA02344` folder containing `eboot.bin` and `sce_sys`.
3. Add update **1.02** through **SETUP → step 2**, or place it beside the game as `CUSA02344-patch`.
4. Choose a game and preset, then press **PLAY**. First launch compiles shaders, so loading and stutter are expected.

- **In-game menu:** `Insert` or `R3 + L2`.
- **Settings:** Saved separately for each game.
- **Saves and logs:** In `analysis` beside the launcher.
- **Updates:** Use **UPDATE TO x.y.z** in the launcher. It checks SHA-256, preserves saves, settings and shader caches, and rolls back if an update fails.

See [Controls and PC setup](tools/README-Windows.txt) for more help.

## Uncharted 2 & Uncharted 3 multiplayer (beta)

Competitive, co-op and LAN party modes are restored through the **MULTIPLAYER** tab. Import requires your own copies and:

- **PS4 Collection 1.02:** CUSA02320 (EU) or CUSA02343 (US); their 1.02 executables are byte-identical.
- **PS3 Uncharted 3 GOTY:** BCES01670, BCES01692, BCUS99086 or BCAS25014.
- **PS3 data:** The game folder and RPCS3's `dev_hdd0/game` folder with Uncharted 2 GOTY update **1.10** and Uncharted 3 GOTY update **1.19**, the game data install and DLC map packs.
- **Executable:** Your Uncharted 2 decrypted 1.10 `EBOOT.elf` and Uncharted 3 decrypted 1.19 `EBOOT.elf`.
- **Storage:** About **14 GB free**.

Press **IMPORT** and follow the progress log.  Conversion takes approximately 12 minutes, depending on the type of drive (SSD or HDD), and resumes if the launcher is closed. Map and character conversions are still being completed; failed steps appear in the log.

## Legal status

- **No game content distributed:** The repository and releases contain no game files, game code or game-derived data. The launcher and importer download none; they use local files only.
- **Your copies, local processing:** You need legally obtained PS4 and, for multiplayer, PS3 copies. Converted files and patched game images are made on your PC and never shared by the project.
- **No piracy or decryption:** The project does not obtain games, decrypt them or help bypass protection on games you do not own. It uses your own dumps and rejects encrypted store PKGs.
- **Restoration code:** Multiplayer code was written from scratch to reproduce PS3 behaviour. Apart from short instruction patterns used to locate and verify patches, it contains no code from either game.
- **No bundled game keys or secrets:** Online-profile values are read locally from your own decrypted PS3 executable and placed only in your locally built game image. They never leave your PC.
- **Online service:** Community RPCN servers, with no connection to PlayStation Network, which no longer hosts these games' multiplayer.
- **No affiliation:** Not endorsed or supported by Sony Interactive Entertainment, Naughty Dog, Bluepoint Games or their affiliates. *Uncharted* belongs to Sony Interactive Entertainment LLC; PlayStation, PS3, PS4 and PlayStation logos are trademarks of Sony Interactive Entertainment Inc. Game content and other trademarks belong to their owners and are used only for identification.
- **Free and open source:** No donations, sales or ads. GPL v2 or later source is included for everything covered by that license. Provided “as is”, without warranty. See the license details below for separate packaged components.
- **Rights holders:** [Open an issue](../../issues) with any concern; it will be reviewed promptly.

## Contributing

A small number of regular contributors are welcome through fork-based pull requests.

**In scope:** Game-breaking glitches (crashes, freezes, soft-locks, broken audio or controls) and visual issues (colours, textures, geometry, effects or HUD).

**Out of scope:**

- Online play, RPCN and matchmaking, which are maintained privately.
- Build and release setup: `.github/workflows/`, `tools/release.sh`, `tools/VERSION`, launcher self-updates and `u3mp/patcher/maintainer/`.
- Game files in any form. Fixes must be code or recipes that run on the player's PC.

Comment on an issue and wait for a reply before starting. Submit one small fix per branch, rebased on `main`, with the problem, reproduction steps, changes and test results. The maintainer reviews, tests and merges. Follow [CONTRIBUTING.md](CONTRIBUTING.md); all pull requests are GPL v2 or later.

## Credits and license

- [**BilalReloaded**](https://github.com/bc160201983): Created the [original PC port](https://github.com/bc160201983/Uncharted-The-Nathan-Drake-Collection-PC-port), runtime, game fixes and launcher. This fork's launcher converts his Python launcher to C++. Without his port, this project would not exist.
- **Runtime:** Based on [gow3_pc](https://github.com/FelipePrado0/gow3_pc), retaining its `GOW3_*` names, and the [shadPS4](https://github.com/shadps4-emu/shadPS4) video core.
- **Temporal anti-aliasing:** Uses the games' motion-blur velocity pass for the camera, following [SuperMedo's Uncharted port](https://github.com/Supermedo/uncharted_pc) (GPL v2).
- **NVIDIA DLSS / DLAA:** NVIDIA DLSS SDK **310.9.16** (`nvngx_dlss.dll`, NVIDIA license in `licenses/nvidia-dlss`), with a bridge adapted from [IFreemz/shadPS4-Bloodborne-DLSS-FSR](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR) (MIT). NVIDIA, RTX and DLSS are NVIDIA Corporation trademarks.
- **Third-party libraries:** FSR (AMD), imgui, magic_enum, miniz, xbyak, LibAtrac9, FFmpeg, SDL3, [Qt 6](https://www.qt.io) (LGPL v3, unmodified shared libraries), [Python](https://www.python.org) (PSF), [Pillow](https://python-pillow.org) (HPND), [NumPy](https://numpy.org) (BSD) and [Opus](https://opus-codec.org) (BSD).
- [**João Fidalgo**](https://github.com/fidalgojoao): Entire planning and rework of all the launcher, Uncharted 2 multiplayer and Uncharted 3 multiplayer restoration and pak tools (`u3mp/`). TLS uses [BearSSL](https://bearssl.org) (MIT).
- [**Tha14**](https://github.com/Tha14): Huge contributions to the launcher, Uncharted 3 Multiplayer and Uncharted 2 Multiplayer.
- [**33martyr**](https://github.com/33martyr): Uncharted 3 Multiplayer Map and character visual fixes, including Yeti mask, Outdoor Jacket and map lighting.
- **RPCS3 and RPCN:** Online play uses [RPCS3's](https://rpcs3.net) RPCN service and communicates as RPCS3 does. RPCN is by **RipleyTom** and contributors ([source](https://github.com/RipleyTom/rpcn)); RPCS3 is by its team and contributors ([source](https://github.com/RPCS3/rpcs3)). Thank you to both. This project is independent and not endorsed by either; report this project's issues here.

**Fork license:** Created on **5 November 2026** from original commit [**`e0c99f9`**](https://github.com/fidalgojoao/Uncharted-The-Nathan-Drake-Collection-PC-port/commit/e0c99f9) (*Release 0.8.3*), published under **GNU GPL v2 or later** ([license](gow3_pc/LICENSE)). This fork follows those terms: copying, modifying and redistributing requires keeping the license and making the source available. Later license changes to the original do not apply to this copy, as the source code has already been fully rewritten. See the [GNU GPL v2 or later license](gow3_pc/LICENSE).

Windows packages include `LICENSE.txt` and bundled library licenses. `licenses/THIRD-PARTY.txt` lists DLL versions, licenses and sources. Packaged online components and the multiplayer helper are separate works by this fork's author, outside the GPL; **building this source alone does not provide online play**. See [Package details](tools/README-Windows.txt).

### Reusing this work

You may use these fixes in your own port. Keep copyright and license notices, and publish your source under the same license when distributing a build. Please also credit and link to [this project](https://github.com/fidalgojoao/Uncharted-The-Nathan-Drake-Collection-PC-port) in your README or release notes so people can trace each fix.
