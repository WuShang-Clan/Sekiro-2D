# Sekiro 2D — Shadows Die Twice

A 2D posture-based samurai duelling game built with C++ and SFML 2.6.1, inspired by Sekiro. It features a six-boss campaign, a 2-player versus mode, deflection and posture combat, deathblow cinematics, three save slots, and a fully procedural fallback for all art, sound and arenas, so it runs even with no asset files.

## Features

### Combat System
- Posture bar alongside health: break your enemy's posture, then land a **DEATHBLOW**
- **Block** (hold) costs posture only, no health
- **Deflect** (tap block just before a hit) fills the *attacker's* posture instead
- **Slash combo** (up to 3 hits) plus a **jump slash** in the air
- **Perilous attacks** with on-screen warnings:
  - **Sweep** (low, unblockable): jump over it for a **Jump Counter**
  - **Thrust** (unblockable): dash toward it for a **Mikiri Counter**
- **Barrage** chains that can only be survived by deflecting every strike
- **Spear and matchlock gun** attacks (bullets can be deflected, blocked or jumped over)
- Two lives: resurrect with 60% health, or accept death
- Hit-stop, screen shake, slow-motion deathblow camera, sparks, shockwaves and blood effects

### Game Modes
- **Campaign**: 6 chapters with story text, a boss each, and an epilogue ending
- **Versus**: 2-player local duel, first to win 2 rounds
- **Samurai Challenge**: pick any single boss and fight just that duel
- **How to Play**: 8 built-in tutorial pages

### Bosses

| # | Boss | Weapon | Gimmick |
|---|---|---|---|
| 1 | Ashigaru Captain | Katana | Learn to deflect |
| 2 | Crimson Duelist | Odachi (great sword) | Sweeps: jump counter |
| 3 | Shadow Monk | Nodachi (long sword) | Thrusts: mikiri counter |
| 4 | Blade Saint | Twin wakizashi | Barrage: deflect only |
| 5 | Matchlock Hayate | Yari spear + matchlock gun | Long reach + bullets |
| 6 | Sword Demon | Kurenai-maru | All moves, **3 lives** |

### Progress and Stats
- 3 save slots (`savegame1.sav` – `savegame3.sav`), with auto-save on death
- Completed-run history in `runs.sav` (time, deaths, resurrections, personal bests)
- End-of-game rank: *Stubborn Ronin → Seasoned Samurai → Master Swordsman → Flawless Demon-Slayer*

### Presentation
- Unique procedural arena for each level (sunset gate, moonlit castle, bamboo temple, snowy pass, burning village, crimson lily field)
- Stick-figure rig with IK arms, unique gear and weapons per boss, drawn in code
- Synthesized sound effects (swings, parries, clashes, deathblow, resurrection bell and more)
- Per-level music tracks with fades, and mute toggle

## Quick Run
1. Download `Sekiro2D.zip` from the **Releases** page (or build from source below)
2. Extract it and keep the `assets/` folder next to the executable
3. Run `Sekiro2D.exe`

## Project Structure

**Repository**

```
Sekiro2D/
│
├── main.cpp                 (single-file game source)
├── README.md
├── .gitignore
└── assets/
    ├── font.ttf
    ├── font_title.ttf
    ├── background.png
    ├── player1/  player2/
    ├── boss/  boss1/ ... boss6/
    ├── backgrounds/   (title, menu, versus, bg1 ... bg6)
    ├── ui/            (danger.png, tutorial_1 ... tutorial_6)
    ├── sounds/        (.wav / .ogg)
    └── music/         (.ogg / .wav / .flac)
```

**Release download (`Sekiro2D.zip`, see the Releases page)**

```
Sekiro2D/
│
├── Sekiro2D.exe
├── sfml-graphics-2.dll
├── sfml-window-2.dll
├── sfml-system-2.dll
├── sfml-audio-2.dll
├── openal32.dll
├── assets/
├── savegame1.sav ... savegame3.sav   (created when you play)
└── runs.sav                          (created when you finish a run)
```



## Requirements

| Requirement | Details |
|---|---|
| Compiler | MSVC / GCC / Clang (C++17) |
| SFML | Version **2.6.1** |
| OS | Windows 10/11, Linux and macOS (compatible) |
| Font | `assets/font.ttf` (falls back to a system font such as Arial or DejaVu) |

## Building from Source

### Windows (Visual Studio)

1. Install [SFML 2.6.1](https://www.sfml-dev.org/download/sfml/2.6.1/)
2. Create a new Visual Studio C++ project and add `main.cpp`
3. Set the language standard to **C++17**
4. Configure project properties:
   - **Include Directories** → `<SFML>/include`
   - **Library Directories** → `<SFML>/lib`
   - **Linker → Additional Dependencies**:
     ```
     sfml-graphics.lib
     sfml-window.lib
     sfml-system.lib
     sfml-audio.lib
     ```
5. Copy the SFML `.dll` files next to the output executable
6. Build and run

### Windows (g++ / MinGW) and Linux

```bash
g++ -std=c++17 -o Sekiro2D main.cpp -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio
```

## Assets (all optional)

The game is playable with **no assets at all**. Anything missing is replaced by a built-in fallback:

| Missing | Fallback |
|---|---|
| Character sprites | Procedural stick-figure samurai with per-boss gear |
| Backgrounds | Procedural arena for each level |
| Sound effects | Synthesized in code |
| Music | Silent |
| Font | System font (Arial / DejaVu) |

### Sprite animations
Place PNGs in `assets/player1/`, `assets/player2/` and `assets/boss1/` … `assets/boss6/` (or a shared `assets/boss/`). Use either a single image (`idle.png`) or numbered frames (`idle_0.png`, `idle_1.png`, ...).

Supported animation names:
`idle, walk, dash, jump, slash, airslash, thrust, sweep, barrage, spear, shoot, block, parry, mikiri, jumpcounter, hurt, deflected, clash, broken, dead, execute`

Missing animations fall back automatically (for example `parry` → `block`). Add an optional `fps.txt` with a single number to set the idle/walk frame rate.

### Sound effects
Put `.wav` or `.ogg` files in `assets/sounds/` named:
`swing, hit, block, parry, mikiri, jumpcounter, clash, deathblow, jump, break, dash, revive, gun, select, confirm`

### Music
Put tracks in `assets/music/` named: `title, boss1, boss2, boss3, boss4, boss5, boss6, versus` (`.ogg`, `.wav`, `.flac`).

### Art tweaks
Edit the `cfg` namespace at the top of the source to change sprite height, facing direction of your art (`PLAYER_ART_FACES_RIGHT`, `BOSS_ART_FACES_RIGHT`), texture smoothing, gravity, parry window, music volume and more.

## Controls

### Player 1 (Campaign and Versus)

| Key | Action |
|---|---|
| A / D | Move |
| W | Jump |
| J | Slash (tap again for a combo) |
| J (in the air) | Jump slash (once per jump, no blocking while airborne) |
| K | Block (hold) / Deflect (tap just before impact) |
| L | Dash (toward an enemy thrust = Mikiri) |
| U | Thrust (perilous) |

### Player 2 (Versus)

| Key | Action |
|---|---|
| Left / Right | Move |
| Up | Jump |
| , (comma) | Slash / jump slash |
| . (period) | Block / Deflect |
| / | Dash |
| ; (semicolon) | Thrust |

### General

| Key | Action |
|---|---|
| Enter / Space | Confirm |
| Up / Down / Left / Right | Navigate menus |
| 1 – 6 | Pick a boss in Samurai Challenge |
| P | Pause / go back |
| R | Restart the current duel |
| Enter (when fallen) | Resurrect with 60% health |
| Backspace (when fallen) | Accept death |
| F1 | Debug view (hitboxes and states) |
| F2 | Mute music |
| F11 / Alt + Enter | Toggle fullscreen |

## Tips
- Deflecting is the best defence: tap block once, then wait. Mashing it won't work.
- Posture recovers slower the lower your health is, and holding block stops recovery.
- Sweep → **jump**. Thrust → **dash in**. Barrage → **deflect every swing**.
- A deathblow drains a whole life bar, so land one on each of the Sword Demon's three lives.

## Author

**Muhammad Hammad Anis**
Sekiro 2D v1.0

## License

This project is for educational purposes. It is a fan-made tribute and is not affiliated with FromSoftware or Activision.
