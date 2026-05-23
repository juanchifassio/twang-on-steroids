# Twang on Steroids — Design Spec
Date: 2026-05-23

## Overview

Twang on Steroids extends the original 1D LED dungeon crawler with three game modes (Solo, Co-op, Versus), procedural level generation, an interactive mode-select screen, and support for two MPU6050 controllers on a single Arduino.

Hardware target: Arduino + MPU6050 + WS2812B LED strip (300 LEDs). Designed to be migratable to Raspberry Pi later.

---

## 1. Architecture Changes

### 1.1 Player Struct

All single-player globals are refactored into a `Player` struct. Two instances live in a `players[2]` array. Solo mode uses only `players[0]`.

```cpp
struct Player {
  int position;
  bool alive;
  bool attacking;
  long attackMillis;
  int lives;
  int tilt;
  int wobble;
  int mpuAddress;  // 0x68 or 0x69
  CRGB color;
};

Player players[2];
int playerCount = 1;
```

All existing functions that reference `playerPosition`, `attacking`, etc. are updated to loop over `players[]`.

### 1.2 Game Mode Enum

```cpp
enum GameMode { SOLO, COOP, VERSUS };
GameMode gameMode = SOLO;
```

A `switch(gameMode)` in win/lose/attack logic handles mode-specific behaviour. Shared mechanics (enemy ticking, lava, conveyors) remain unchanged — they just check all active players.

### 1.3 Level Source Flag

```cpp
bool proceduralMode = false;
```

`loadLevel()` checks this flag: if false, runs the existing 16-level switch/case (classic mode); if true, calls `generateLevel(difficulty)`.

---

## 2. Mode Select Screen

Replaces the screensaver as the entry point. It is a clean mini-level — no enemies, no obstacles.

### Layout

Three glowing pools on the strip:
- **SOLO** at world position 200 — green
- **COOP** at world position 500 — cyan
- **VERSUS** at world position 800 — purple

Each pool spans ~60 world units and pulses in brightness (like lava, but inviting).

### Interaction

- Player 1 navigates using tilt as normal
- Standing inside a pool's range triggers its melody loop
- Wobble attack confirms the selection — pool flashes white, mode locks in
- `playerCount` is set to 1 (SOLO) or 2 (COOP/VERSUS) after confirmation

### Music Per Pool

Using `toneAC`, each pool plays a distinct looping arpeggio while the player stands on it:
- **SOLO** — fast, tense ascending beeps
- **COOP** — slower two-tone harmonious loop
- **VERSUS** — low, aggressive pulse

Music stops immediately on selection.

---

## 3. Solo Mode

Behaviour is identical to the original game with one addition: a mode choice at the start of solo play.

- **Classic** — plays the original 16 hand-crafted levels in order
- **Endless** — procedural generation with infinite difficulty scaling

Mode choice happens inside the SOLO pool: once the player enters it, the strip shows two sub-zones — tilt left for Classic (green), tilt right for Endless (white). Wobble confirms. This keeps the selection entirely within the existing tilt/wobble input model.

---

## 4. Procedural Level Generation

### Difficulty Tiers

| Tier | Approx level equivalent | Elements unlocked |
|------|--------------------------|-------------------|
| 1 | 1–4 | 1-2 slow enemies, simple lava |
| 2 | 5–8 | Conveyors, spawners |
| 3 | 9–13 | All elements combined |
| 4 | 14+ | Bosses, max enemy count, fast lava |

Tier = `constrain(difficulty / 4, 1, 4)`.

### Generation Rules

1. Seed with `millis()` at game start — different every run
2. Divide the 1000-unit world into zones (~200 units each)
3. For each zone, pick ingredients from the tier's weighted list
4. Safety constraints:
   - Lava covers no more than 30% of the strip simultaneously
   - A safe gap of at least 100 units near position 0 (spawn area)
   - No two lava segments overlap
   - Enemy spawn positions never overlap with lava-ON zones

### Usage by Mode

- **Solo classic** — switch/case levels 0–16 only
- **Solo endless** — procedural from difficulty 1, increments each level
- **Co-op** — always procedural, starts at tier 1
- **Versus** — always procedural obstacles, starts at tier 2

---

## 5. Co-op Mode

### Setup

- `playerCount = 2`
- Player 1: green (`CRGB(0, 255, 0)`), MPU at `0x68`
- Player 2: cyan (`CRGB(0, 255, 180)`), MPU at `0x69`
- Shared life pool: 3 lives total

### Win Condition

Both players must reach the exit (world position 1000) to complete the level.

### Death & Respawn

- When a player dies: lose 1 shared life, trigger particle explosion at death position, respawn at position 0 after 2 seconds
- Surviving player must hold out during respawn delay
- 0 shared lives = game over, return to mode select

### Player-Specific Enemies

A new enemy variant: `MarkedEnemy`. Each marked enemy is assigned to one player on spawn.

- Marked enemy for player 1 glows green (slightly darker than player)
- Marked enemy for player 2 glows cyan (slightly darker than player)
- Only the assigned player's wobble attack can kill it
- On contact with **either** player, both die (costs 1 shared life)
- Marked enemies appear from tier 2 onwards in co-op procedural generation

Rather than subclassing (problematic with Arduino's pool pattern), two fields are added to the existing `Enemy` class:

```cpp
bool isMarked;
int ownerIndex; // 0 or 1, only relevant if isMarked
```

The existing `enemyPool[10]` is reused — marked enemies are just regular enemies with these flags set.

### Input

Both MPU6050s read every frame in `getInput()`. `accelgyro.setAddr()` switches between addresses.

---

## 6. Versus Mode

### Setup

- `playerCount = 2`
- Player 1 starts at position 0 (green), Player 2 starts at position 1000 (purple `CRGB(180, 0, 255)`)
- No shared lives — each player has independent kill tracking
- Procedural obstacles (enemies, lava) generated in the middle zones

### Win Condition

First player to land 3 kills wins. A "kill" is: landing a wobble attack on the other player while within `ATTACK_WIDTH` distance.

### Kill Feedback

After each kill:
- The strip flashes the killer's color for 500ms
- The killed player respawns at their starting position
- Kill count displayed as pulses at each end of the strip (1–3 pulses)

### No Exit

The level never ends via reaching position 1000 — only via kill count.

---

## 7. Two MPU6050 Wiring

- Both MPU6050s on the same I2C bus (SDA/SCL)
- MPU6050 #1: `AD0` pin → GND → address `0x68` (player 1)
- MPU6050 #2: `AD0` pin → VCC → address `0x69` (player 2)
- In solo mode, only address `0x68` is read — no second sensor required

---

## 8. What Stays the Same

- All 16 hand-crafted levels untouched
- FastLED rendering pipeline
- toneAC sound system
- Lava, Conveyor, Spawner, Boss, Particle classes (minor updates to check all players)
- Life LED pins (52, 50, 40) — used for player 1 in all modes
- Screensaver triggers after timeout on mode select screen
