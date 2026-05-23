# Twang on Steroids Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend Twang with co-op and versus multiplayer modes, procedural level generation, and an interactive mode-select screen while keeping all 16 original levels intact.

**Architecture:** Refactor single-player globals into a `Player` struct array; add a `GameMode` enum; implement procedural generation as a new code path alongside the existing switch/case; mode-select is a new stage that replaces the boot entry point. Two MPU6050 sensors share the I2C bus via the AD0 address pin — the second sensor is only initialized when a 2-player mode is selected.

**Tech Stack:** Arduino C++ (Arduino Mega), FastLED, toneAC, MPU6050/I2Cdev, RunningMedian

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `Player.h` | **Create** | Player struct — all per-player state |
| `ProceduralLevel.h` | **Create** | `generateLevel(difficulty, coopMode)` |
| `Enemy.h` | **Modify** | Add `isMarked`, `ownerIndex` fields |
| `TWANG.ino` | **Modify** | All game logic — see tasks below |

**Compile command used throughout:**
```bash
arduino-cli compile --fqbn arduino:avr:mega .
```
If `arduino-cli` is not installed, compile in the Arduino IDE instead.

---

### Task 1: Create Player.h

**Files:**
- Create: `Player.h`

- [ ] **Step 1: Create Player.h**

```cpp
#ifndef PLAYER_H
#define PLAYER_H

#include "FastLED.h"

struct Player {
  int position;
  bool alive;
  bool attacking;
  long attackMillis;
  long lastAttackTime;
  int lives;
  int tilt;
  int wobble;
  int mpuAddress;
  CRGB color;
  int positionModifier;
  long killTime;
  int kills;       // VERSUS: kill count toward 3-kill win
  long respawnAt;  // COOP: millis() timestamp to respawn, 0 = alive
};

#endif
```

- [ ] **Step 2: Compile**

```bash
arduino-cli compile --fqbn arduino:avr:mega .
```
Expected: no errors (file not included anywhere yet, no effect).

- [ ] **Step 3: Commit**

```bash
git add Player.h
git commit -m "feat: add Player struct"
```

---

### Task 2: Extend Enemy.h with marked-enemy fields

**Files:**
- Modify: `Enemy.h`

- [ ] **Step 1: Add fields to the public section of the Enemy class**

In `Enemy.h`, add after `int playerSide;` in the `public:` section:

```cpp
bool isMarked;
int ownerIndex;
```

- [ ] **Step 2: Initialize the new fields in Enemy::Spawn()**

At the end of `Enemy::Spawn()`, before the closing brace, add:

```cpp
isMarked = false;
ownerIndex = 0;
```

- [ ] **Step 3: Compile**

```bash
arduino-cli compile --fqbn arduino:avr:mega .
```
Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add Enemy.h
git commit -m "feat: add isMarked and ownerIndex to Enemy"
```

---

### Task 3: Refactor TWANG.ino — globals, setup, and getInput

**Files:**
- Modify: `TWANG.ino`

This task replaces all single-player globals with the `players[]` array and updates input handling for two MPU6050 sensors.

- [ ] **Step 1: Replace the globals block at the top of TWANG.ino**

Remove everything from `// MPU` through `int lives = 3;` and replace with:

```cpp
#include "Player.h"

// MPU — two sensors share I2C bus via AD0 pin
MPU6050 mpu0(0x68);  // AD0 → GND
MPU6050 mpu1(0x69);  // AD0 → VCC (initialized only when 2-player mode selected)
RunningMedian angleSamples0 = RunningMedian(5);
RunningMedian wobbleSamples0 = RunningMedian(5);
RunningMedian angleSamples1 = RunningMedian(5);
RunningMedian wobbleSamples1 = RunningMedian(5);

// LED setup
#define NUM_LEDS             300
#define DATA_PIN             3
#define CLOCK_PIN            4
#define LED_COLOR_ORDER      GRB
#define BRIGHTNESS           150
#define DIRECTION            1
#define MIN_REDRAW_INTERVAL  16
#define USE_GRAVITY          1
#define BEND_POINT           550
#define LED_TYPE             WS2812B

// GAME
float FREQUENCY_MULTIPLIER = 1.0;
long previousMillis = 0;
int levelNumber = 0;
long lastInputTime = 0;
#define TIMEOUT              30000
#define LEVEL_COUNT          16
#define MAX_VOLUME           15
iSin isin = iSin();

enum GameMode { SOLO_CLASSIC, SOLO_ENDLESS, COOP, VERSUS };
GameMode gameMode = SOLO_CLASSIC;
int playerCount = 1;
bool proceduralMode = false;
int proceduralDifficulty = 1;

Player players[2];

// JOYSTICK
#define JOYSTICK_ORIENTATION 1
#define JOYSTICK_DIRECTION   1
#define ATTACK_THRESHOLD     30000
#define JOYSTICK_DEADZONE    5

// WOBBLE ATTACK
#define ATTACK_WIDTH         50
int NORMAL_ATTACK_DURATION = 500;
int ATTACK_DURATION        = NORMAL_ATTACK_DURATION;
int NORMAL_PLAYER_SPEED    = 10;
int MAX_PLAYER_SPEED       = NORMAL_PLAYER_SPEED;
const unsigned long NORMAL_ATTACK_DELAY = 800;
long ATTACK_DELAY          = NORMAL_ATTACK_DELAY;

#define BOSS_WIDTH 40

// SYSTEM
char* stage;
long stageStartTime;
int lifeLEDs[3] = {52, 50, 40};
int modeSelectPool = -1;
```

- [ ] **Step 2: Replace setup()**

```cpp
void setup() {
    Serial.begin(9600);
    while (!Serial);

    Wire.begin();
    mpu0.initialize();

    FastLED.addLeds<LED_TYPE, DATA_PIN, LED_COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.setDither(1);

    for(int i = 0; i < 3; i++){
        pinMode(lifeLEDs[i], OUTPUT);
        digitalWrite(lifeLEDs[i], HIGH);
    }

    players[0].mpuAddress  = 0x68;
    players[0].color       = CRGB(0, 255, 0);
    players[0].position    = 200;
    players[0].alive       = true;
    players[0].lives       = 3;
    players[0].kills       = 0;
    players[0].respawnAt   = 0;

    players[1].mpuAddress  = 0x69;
    players[1].color       = CRGB(0, 255, 180);
    players[1].position    = 200;
    players[1].alive       = true;
    players[1].lives       = 3;
    players[1].kills       = 0;
    players[1].respawnAt   = 0;

    stage = "MODE_SELECT";
    stageStartTime = millis();
}
```

- [ ] **Step 3: Replace getInput()**

```cpp
void getInput() {
    int16_t ax, ay, az, gx, gy, gz;

    mpu0.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    {
        int a = (JOYSTICK_ORIENTATION == 0 ? ax : (JOYSTICK_ORIENTATION == 1 ? ay : az)) / 166;
        int g = (JOYSTICK_ORIENTATION == 0 ? gx : (JOYSTICK_ORIENTATION == 1 ? gy : gz));
        if(abs(a) < JOYSTICK_DEADZONE) a = 0;
        if(a > 0) a -= JOYSTICK_DEADZONE;
        if(a < 0) a += JOYSTICK_DEADZONE;
        angleSamples0.add(a);
        wobbleSamples0.add(g);
        players[0].tilt = angleSamples0.getMedian();
        if(JOYSTICK_DIRECTION == 1) players[0].tilt = -players[0].tilt;
        players[0].wobble = abs(wobbleSamples0.getHighest());
    }

    if(playerCount == 2) {
        mpu1.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
        {
            int a = (JOYSTICK_ORIENTATION == 0 ? ax : (JOYSTICK_ORIENTATION == 1 ? ay : az)) / 166;
            int g = (JOYSTICK_ORIENTATION == 0 ? gx : (JOYSTICK_ORIENTATION == 1 ? gy : gz));
            if(abs(a) < JOYSTICK_DEADZONE) a = 0;
            if(a > 0) a -= JOYSTICK_DEADZONE;
            if(a < 0) a += JOYSTICK_DEADZONE;
            angleSamples1.add(a);
            wobbleSamples1.add(g);
            players[1].tilt = angleSamples1.getMedian();
            if(JOYSTICK_DIRECTION == 1) players[1].tilt = -players[1].tilt;
            players[1].wobble = abs(wobbleSamples1.getHighest());
        }
    }
}
```

- [ ] **Step 4: Compile**

```bash
arduino-cli compile --fqbn arduino:avr:mega .
```
Expected: errors about undefined `playerPosition`, `attacking`, etc. — that is correct; they will be resolved in the next task. If there are errors about `mpu0`/`mpu1` not found, check that the `#include "MPU6050.h"` line is still present.

- [ ] **Step 5: Commit the partial refactor**

```bash
git add TWANG.ino
git commit -m "refactor: replace player globals with players[] array, dual MPU input"
```

---

### Task 4: Update all game functions to use players[]

**Files:**
- Modify: `TWANG.ino`

Replace every reference to the old globals (`playerPosition`, `playerAlive`, `attacking`, `joystickTilt`, `joystickWobble`, `playerPositionModifier`, `killTime`, `lives`) with the `players[]` array.

- [ ] **Step 1: Replace drawPlayer()**

```cpp
void drawPlayer() {
    for(int i = 0; i < playerCount; i++) {
        if(players[i].alive) {
            leds[getLED(players[i].position)] = players[i].color;
        }
    }
}
```

- [ ] **Step 2: Replace drawAttack()**

```cpp
void drawAttack() {
    for(int i = 0; i < playerCount; i++) {
        if(!players[i].attacking || !players[i].alive) continue;
        int n = map(millis() - players[i].attackMillis, 0, ATTACK_DURATION, 100, 5);
        for(int j = getLED(players[i].position - (ATTACK_WIDTH/2)) + 1;
                j <= getLED(players[i].position + (ATTACK_WIDTH/2)) - 1; j++){
            leds[j] = CRGB(0, 0, n);
        }
        if(n > 90) {
            leds[getLED(players[i].position)] = CRGB(255, 255, 255);
        } else {
            leds[getLED(players[i].position)] = players[i].color;
        }
        leds[getLED(players[i].position - (ATTACK_WIDTH/2))] = CRGB(n, n, 255);
        leds[getLED(players[i].position + (ATTACK_WIDTH/2))] = CRGB(n, n, 255);
    }
}
```

- [ ] **Step 3: Replace updateLives()**

```cpp
void updateLives() {
    int l = players[0].lives;
    for(int i = 0; i < 3; i++){
        digitalWrite(lifeLEDs[i], l > i ? HIGH : LOW);
    }
}
```

- [ ] **Step 4: Replace die() — now takes a player index**

```cpp
void die(int playerIndex) {
    players[playerIndex].alive = false;
    players[playerIndex].killTime = millis();

    for(int p = 0; p < particleCount; p++){
        particlePool[p].Spawn(players[playerIndex].position);
    }

    if(gameMode == VERSUS) {
        // In versus, lava/enemy deaths just respawn at start — no kill credit
        players[playerIndex].position = (playerIndex == 0) ? 0 : 1000;
        players[playerIndex].alive = true;
        players[playerIndex].attacking = false;
        return;
    }

    if(gameMode == COOP) {
        players[0].lives--;
        updateLives();
        if(players[0].lives <= 0) {
            players[0].lives = 3;
            noToneAC();
            stage = "MODE_SELECT";
            stageStartTime = millis();
            return;
        }
        players[playerIndex].respawnAt = millis() + 2000;
        return;
    }

    // SOLO_CLASSIC / SOLO_ENDLESS
    players[playerIndex].lives--;
    updateLives();
    if(players[playerIndex].lives <= 0) {
        levelNumber = 0;
        players[playerIndex].lives = 3;
        noToneAC();
        stage = "MODE_SELECT";
        stageStartTime = millis();
        return;
    }
    stageStartTime = millis();
    stage = "DEAD";
}
```

- [ ] **Step 5: Replace tickEnemies()**

```cpp
void tickEnemies() {
    for(int i = 0; i < enemyCount; i++){
        if(enemyPool[i].Alive()){
            enemyPool[i].Tick();

            // Attack kills — marked enemies only killable by their owner
            for(int p = 0; p < playerCount; p++) {
                if(!players[p].alive || !players[p].attacking) continue;
                bool canKill = !enemyPool[i].isMarked || enemyPool[i].ownerIndex == p;
                if(canKill &&
                   enemyPool[i]._pos > players[p].position - (ATTACK_WIDTH/2) &&
                   enemyPool[i]._pos < players[p].position + (ATTACK_WIDTH/2)) {
                    enemyPool[i].Kill();
                    SFXkill();
                    break;
                }
            }

            if(inLava(enemyPool[i]._pos)) {
                enemyPool[i].Kill();
                SFXkill();
            }

            if(enemyPool[i].Alive()) {
                CRGB eColor = CRGB(255, 0, 0);
                if(enemyPool[i].isMarked) {
                    eColor = (enemyPool[i].ownerIndex == 0) ? CRGB(0, 180, 0) : CRGB(0, 180, 130);
                }
                leds[getLED(enemyPool[i]._pos)] = eColor;
            }

            // Contact kill — marked enemies kill all players
            for(int p = 0; p < playerCount; p++) {
                if(!players[p].alive) continue;
                if((enemyPool[i].playerSide == 1  && enemyPool[i]._pos <= players[p].position) ||
                   (enemyPool[i].playerSide == -1 && enemyPool[i]._pos >= players[p].position)) {
                    if(enemyPool[i].isMarked) {
                        for(int pp = 0; pp < playerCount; pp++) die(pp);
                    } else {
                        die(p);
                    }
                    return;
                }
            }
        }
    }
}
```

- [ ] **Step 6: Replace the collision and attack sections in tickBoss()**

Inside the `if(bossPool[b].Alive())` block, replace the single-player collision and attack checks with:

```cpp
// CHECK COLLISION
for(int p = 0; p < playerCount; p++) {
    if(!players[p].alive) continue;
    if(getLED(players[p].position) > getLED(bossPool[b]._pos - BOSS_WIDTH/2) &&
       getLED(players[p].position) < getLED(bossPool[b]._pos + BOSS_WIDTH/2)){
        die(p);
        return;
    }
}
// CHECK FOR ATTACK
for(int p = 0; p < playerCount; p++) {
    if(!players[p].alive || !players[p].attacking) continue;
    if((getLED(players[p].position + (ATTACK_WIDTH/2)) >= getLED(bossPool[b]._pos - BOSS_WIDTH/2) &&
        getLED(players[p].position + (ATTACK_WIDTH/2)) <= getLED(bossPool[b]._pos + BOSS_WIDTH/2)) ||
       (getLED(players[p].position - (ATTACK_WIDTH/2)) <= getLED(bossPool[b]._pos + BOSS_WIDTH/2) &&
        getLED(players[p].position - (ATTACK_WIDTH/2)) >= getLED(bossPool[b]._pos - BOSS_WIDTH/2))) {
        bossPool[b].Hit();
        if(bossPool[b].Alive()) {
            moveBoss(b);
        } else {
            spawnPool[b*2].Kill();
            spawnPool[b*2+1].Kill();
        }
    }
}
```

- [ ] **Step 7: Replace the PLAY stage body in loop()**

The original `loop()` has two separate `if(stage == "PLAY")` blocks:
1. **Before** the `if(mm - previousMillis >= MIN_REDRAW_INTERVAL)` check — SFX only (runs every frame)
2. **Inside** the redraw check — movement, collision, rendering

Replace the SFX block (block 1):

```cpp
if(stage == "PLAY"){
    bool anyAttacking = false;
    for(int i = 0; i < playerCount; i++) if(players[i].attacking) anyAttacking = true;
    if(anyAttacking) {
        SFXattacking();
    } else {
        SFXtilt(players[0].tilt);
    }
} else if(stage == "DEAD"){
    SFXdead();
}
```

In the redraw block (block 2), replace the PLAY movement/attack/collision section. The tick/draw calls (`tickEnemies()`, `tickBoss()`, `tickSpawners()`, `tickLava()`, `tickConveyors()`, `tickParticles()`, `drawPlayer()`, `drawExit()`, `drawAttack()`, `FastLED.show()`) **stay in place** — only the movement and attack initiation code changes:

```cpp
if(stage == "PLAY"){
    for(int i = 0; i < playerCount; i++) {
        // COOP respawn
        if(!players[i].alive) {
            if(gameMode == COOP && players[i].respawnAt > 0 && mm >= players[i].respawnAt) {
                players[i].position  = 0;
                players[i].alive     = true;
                players[i].respawnAt = 0;
            }
            continue;
        }

        // Attack timing
        if(players[i].attacking && players[i].attackMillis + ATTACK_DURATION < mm) {
            players[i].attacking = false;
        }
        if(!players[i].attacking &&
           players[i].wobble > ATTACK_THRESHOLD &&
           mm - players[i].lastAttackTime >= (unsigned long)ATTACK_DELAY) {
            players[i].attackMillis    = mm;
            players[i].attacking       = true;
            players[i].lastAttackTime  = mm;
        }

        // Movement
        players[i].position += players[i].positionModifier;
        if(!players[i].attacking) {
            int moveAmount = (players[i].tilt / 6.0);
            if(DIRECTION) moveAmount = -moveAmount;
            moveAmount = constrain(moveAmount, -MAX_PLAYER_SPEED, MAX_PLAYER_SPEED);
            players[i].position -= moveAmount;
            if(players[i].position < 0) players[i].position = 0;
        }

        // Lava kill
        if(inLava(players[i].position)) {
            die(i);
            return;
        }

        // VERSUS: player-vs-player attack
        if(gameMode == VERSUS && players[i].attacking) {
            int other = 1 - i;
            if(players[other].alive &&
               players[other].position > players[i].position - (ATTACK_WIDTH/2) &&
               players[other].position < players[i].position + (ATTACK_WIDTH/2)) {
                versusKill(i);
                return;
            }
        }
    }

    // Win condition (not in VERSUS)
    if(gameMode != VERSUS) {
        bool exitReached = true;
        for(int i = 0; i < playerCount; i++) {
            if(!players[i].alive || players[i].position < 1000) { exitReached = false; break; }
        }
        if(exitReached && !bossPool[0].Alive() && !bossPool[1].Alive()) {
            levelComplete();
            return;
        }
    }
```

- [ ] **Step 8: Update SFXdead() to use players[0].killTime**

```cpp
void SFXdead(){
    int freq = max(1000 - (int)(millis() - players[0].killTime), 10);
    freq += random8(200);
    toneAC(freq * FREQUENCY_MULTIPLIER, MAX_VOLUME);
}
```

- [ ] **Step 9: Update tickConveyors() to set positionModifier on all players**

In `tickConveyors()`, replace the `playerPositionModifier = 0` and the player-in-conveyor section:

```cpp
// Reset all player modifiers
for(int p = 0; p < playerCount; p++) players[p].positionModifier = 0;

// ... (existing conveyor draw loop stays the same) ...

// Inside the conveyor loop, replace the playerPosition check:
for(int p = 0; p < playerCount; p++) {
    if(players[p].position > conveyorPool[i]._startPoint &&
       players[p].position < conveyorPool[i]._endPoint) {
        int mod = (MAX_PLAYER_SPEED - 4 <= 0) ? 1 : (MAX_PLAYER_SPEED - 4);
        players[p].positionModifier = (dir == -1) ? -mod : mod;
    }
}
```

- [ ] **Step 10: Compile**

```bash
arduino-cli compile --fqbn arduino:avr:mega .
```
Expected: no errors. If you see "versusKill not declared", that function is added in Task 6 — add a forward declaration `void versusKill(int killerIndex);` near the top of TWANG.ino for now.

- [ ] **Step 11: Commit**

```bash
git add TWANG.ino
git commit -m "refactor: update all game functions for players[] array"
```

---

### Task 5: Add spawnMarkedEnemy() and spawnSpawner() helpers

**Files:**
- Modify: `TWANG.ino`

These helpers are called from `ProceduralLevel.h` and need to be defined in `TWANG.ino`.

- [ ] **Step 1: Add spawnMarkedEnemy() alongside the existing spawnEnemy()**

```cpp
void spawnMarkedEnemy(int pos, int dir, int sp, int wobble, int ownerIndex) {
    for(int e = 0; e < enemyCount; e++){
        if(!enemyPool[e].Alive()){
            enemyPool[e].Spawn(pos, dir, sp, wobble);
            enemyPool[e].playerSide = pos > players[0].position ? 1 : -1;
            enemyPool[e].isMarked   = true;
            enemyPool[e].ownerIndex = ownerIndex;
            return;
        }
    }
}
```

- [ ] **Step 2: Add spawnSpawner() alongside the existing spawn helpers**

```cpp
void spawnSpawner(int pos, int rate, int sp, int dir, long activate) {
    for(int s = 0; s < spawnCount; s++){
        if(!spawnPool[s].Alive()){
            spawnPool[s].Spawn(pos, rate, sp, dir, activate);
            return;
        }
    }
}
```

- [ ] **Step 3: Compile**

```bash
arduino-cli compile --fqbn arduino:avr:mega .
```
Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add TWANG.ino
git commit -m "feat: add spawnMarkedEnemy() and spawnSpawner() helpers"
```

---

### Task 6: Implement versus kill logic

**Files:**
- Modify: `TWANG.ino`

- [ ] **Step 1: Add versusKill() before loop()**

```cpp
void versusKill(int killerIndex) {
    int victimIndex = 1 - killerIndex;
    players[killerIndex].kills++;

    // Flash strip with killer color
    for(int i = 0; i < NUM_LEDS; i++) leds[i] = players[killerIndex].color;
    FastLED.show();
    delay(500);

    // Show kill count: pulses at each end
    for(int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Black;
    for(int k = 0; k < players[0].kills; k++) leds[k] = players[0].color;
    for(int k = 0; k < players[1].kills; k++) leds[NUM_LEDS-1-k] = players[1].color;
    FastLED.show();
    delay(1000);

    // Respawn victim
    players[victimIndex].position  = (victimIndex == 0) ? 0 : 1000;
    players[victimIndex].alive     = true;
    players[victimIndex].attacking = false;

    if(players[killerIndex].kills >= 3) {
        stage = "WIN";
        stageStartTime = millis();
    }
}
```

- [ ] **Step 2: Update levelComplete() to handle procedural mode difficulty increment**

In `levelComplete()`, add before the `stage = "WIN"` line:

```cpp
if(proceduralMode) proceduralDifficulty++;
```

- [ ] **Step 3: Update the WIN and DEAD stage handling in loop()**

Replace the existing WIN and DEAD stage blocks with:

```cpp
} else if(stage == "WIN") {
    SFXwin();
    if(millis() - stageStartTime > 3000) {
        if(gameMode == VERSUS) {
            // Return to mode select after versus win
            players[0].kills = 0;
            players[1].kills = 0;
            noToneAC();
            stage = "MODE_SELECT";
            stageStartTime = millis();
        } else {
            nextLevel();
        }
    }
} else if(stage == "DEAD") {
    SFXdead();
    if(!tickParticles()) {
        loadLevel();
    }
} else if(stage == "COMPLETE") {
    SFXcomplete();
    if(millis() - stageStartTime > 5000) {
        noToneAC();
        stage = "MODE_SELECT";
        stageStartTime = millis();
    }
```

- [ ] **Step 4: Compile**

```bash
arduino-cli compile --fqbn arduino:avr:mega .
```
Expected: no errors.

- [ ] **Step 5: Commit**

```bash
git add TWANG.ino
git commit -m "feat: implement versus kill tracking, flash, and win condition"
```

---

### Task 7: Implement mode select screen

**Files:**
- Modify: `TWANG.ino`

- [ ] **Step 1: Add tickModeSelect() before loop()**

```cpp
#define POOL_SOLO    200
#define POOL_COOP    500
#define POOL_VERSUS  800
#define POOL_HALF     30

void drawModeSelectPools(long mm) {
    CRGB colors[3] = {CRGB(0,255,0), CRGB(0,255,180), CRGB(180,0,255)};
    int pools[3]   = {POOL_SOLO, POOL_COOP, POOL_VERSUS};
    int brightness = 80 + (int)(sin(mm / 300.0) * 60.0);
    for(int p = 0; p < 3; p++) {
        int startLED = getLED(pools[p] - POOL_HALF);
        int endLED   = getLED(pools[p] + POOL_HALF);
        for(int i = startLED; i <= endLED; i++) {
            leds[i] = colors[p];
            leds[i].nscale8(brightness);
        }
    }
}

void SFXmodeSelect(int pool, long mm) {
    int soloNotes[4]   = {440, 550, 660, 880};
    int coopNotes[4]   = {440, 554, 440, 554};
    int versusNotes[4] = {110, 110, 165, 110};
    int rate = 200;
    int* notes;
    if(pool == 0)      { notes = soloNotes;   rate = 150; }
    else if(pool == 1) { notes = coopNotes;   rate = 300; }
    else               { notes = versusNotes; rate = 400; }
    int idx = (mm / rate) % 4;
    toneAC(notes[idx] * FREQUENCY_MULTIPLIER, MAX_VOLUME);
}

void tickModeSelect() {
    long mm = millis();

    int moveAmount = (players[0].tilt / 6.0);
    if(DIRECTION) moveAmount = -moveAmount;
    moveAmount = constrain(moveAmount, -MAX_PLAYER_SPEED, MAX_PLAYER_SPEED);
    players[0].position -= moveAmount;
    players[0].position  = constrain(players[0].position, 0, 1000);

    modeSelectPool = -1;
    if(abs(players[0].position - POOL_SOLO)   <= POOL_HALF) modeSelectPool = 0;
    if(abs(players[0].position - POOL_COOP)   <= POOL_HALF) modeSelectPool = 1;
    if(abs(players[0].position - POOL_VERSUS) <= POOL_HALF) modeSelectPool = 2;

    if(modeSelectPool >= 0 && players[0].wobble > ATTACK_THRESHOLD) {
        noToneAC();
        if(modeSelectPool == 0) {
            stage = "SOLO_SELECT";
            stageStartTime = mm;
        } else if(modeSelectPool == 1) {
            gameMode           = COOP;
            playerCount        = 2;
            proceduralMode     = true;
            proceduralDifficulty = 1;
            mpu1.initialize();
            players[0].lives   = 3;
            players[1].lives   = 3;
            players[1].color   = CRGB(0, 255, 180);
            levelNumber        = 0;
            loadLevel();
        } else {
            gameMode           = VERSUS;
            playerCount        = 2;
            proceduralMode     = true;
            proceduralDifficulty = 2;
            mpu1.initialize();
            players[0].position = 0;
            players[1].position = 1000;
            players[1].color    = CRGB(180, 0, 255);
            players[0].kills    = 0;
            players[1].kills    = 0;
            levelNumber         = 0;
            loadLevel();
        }
        return;
    }

    for(int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(200);
    drawModeSelectPools(mm);
    leds[getLED(players[0].position)] = CRGB(255, 255, 255);

    if(modeSelectPool >= 0) {
        SFXmodeSelect(modeSelectPool, mm);
    } else {
        noToneAC();
    }

    if(abs(players[0].tilt) > JOYSTICK_DEADZONE) lastInputTime = mm;
    if(lastInputTime + TIMEOUT < mm) stage = "SCREENSAVER";
}
```

- [ ] **Step 2: Add tickSoloSelect() after tickModeSelect()**

```cpp
void tickSoloSelect() {
    long mm = millis();

    int moveAmount = (players[0].tilt / 6.0);
    if(DIRECTION) moveAmount = -moveAmount;
    moveAmount = constrain(moveAmount, -MAX_PLAYER_SPEED, MAX_PLAYER_SPEED);
    players[0].position -= moveAmount;
    players[0].position  = constrain(players[0].position, 0, 1000);

    bool onClassic = players[0].position <= 500;

    for(int i = 0; i < NUM_LEDS/2; i++) leds[i] = CRGB(0, 60, 0);
    for(int i = NUM_LEDS/2; i < NUM_LEDS; i++) leds[i] = CRGB(50, 50, 50);
    leds[getLED(100)] = CRGB(0, 255, 0);
    leds[getLED(900)] = CRGB(200, 200, 200);
    leds[getLED(players[0].position)] = CRGB(255, 255, 255);

    if(players[0].wobble > ATTACK_THRESHOLD) {
        noToneAC();
        gameMode         = onClassic ? SOLO_CLASSIC : SOLO_ENDLESS;
        playerCount      = 1;
        proceduralMode   = (gameMode == SOLO_ENDLESS);
        proceduralDifficulty = 1;
        players[0].lives = 3;
        players[0].position = 0;
        levelNumber      = 0;
        loadLevel();
    }
}
```

- [ ] **Step 3: Wire MODE_SELECT and SOLO_SELECT into loop()**

In `loop()`, inside the `if(mm - previousMillis >= MIN_REDRAW_INTERVAL)` block, **after** the existing `getInput()` call (which already runs at the top of that block), add before the stage checks:

```cpp
// After the existing getInput() call:
if(stage == "MODE_SELECT") {
    tickModeSelect();
    FastLED.show();
    previousMillis = mm;
    return;
} else if(stage == "SOLO_SELECT") {
    tickSoloSelect();
    FastLED.show();
    previousMillis = mm;
    return;
}
```

Also update the screensaver escape: when the screensaver is active and the player tilts, set `stage = "MODE_SELECT"` instead of `stage = "WIN"`:

```cpp
if(abs(players[0].tilt) > JOYSTICK_DEADZONE) {
    lastInputTime = mm;
    if(stage == "SCREENSAVER") {
        players[0].position = 200;
        stage = "MODE_SELECT";
        stageStartTime = mm;
    }
}
```

- [ ] **Step 4: Compile**

```bash
arduino-cli compile --fqbn arduino:avr:mega .
```
Expected: no errors.

- [ ] **Step 5: Commit**

```bash
git add TWANG.ino
git commit -m "feat: add mode select screen with pool navigation and per-pool music"
```

---

### Task 8: Create ProceduralLevel.h

**Files:**
- Create: `ProceduralLevel.h`

- [ ] **Step 1: Create ProceduralLevel.h**

```cpp
#ifndef PROCEDURAL_LEVEL_H
#define PROCEDURAL_LEVEL_H

// Forward declarations — these functions are defined in TWANG.ino
void spawnEnemy(int pos, int dir, int sp, int wobble);
void spawnMarkedEnemy(int pos, int dir, int sp, int wobble, int ownerIndex);
void spawnLava(int left, int right, int ontime, int offtime, int offset, char* state);
void spawnConveyor(int startPoint, int endPoint, int dir);
void spawnSpawner(int pos, int rate, int sp, int dir, long activate);
void spawnBoss(int bossInd, int pos);

static int _lavaTotal = 0;
#define MAX_LAVA_COVERAGE 300  // 30% of 1000 world units

static bool _lavaOverlaps(int left, int right) {
    return (_lavaTotal + (right - left)) > MAX_LAVA_COVERAGE;
}

static void _trySpawnLava(int left, int right, int ontime, int offtime, int offset, char* state) {
    if(left < 100) return;  // protect spawn area
    if(_lavaOverlaps(left, right)) return;
    spawnLava(left, right, ontime, offtime, offset, state);
    _lavaTotal += (right - left);
}

void generateLevel(int difficulty, bool coopMode) {
    _lavaTotal = 0;
    int tier = constrain(difficulty / 4 + 1, 1, 4);

    // 5 zones — position 0-100 always safe (player spawn)
    int zoneStarts[5] = {150, 300, 450, 600, 750};
    int zoneEnds[5]   = {280, 430, 580, 730, 900};

    for(int z = 0; z < 5; z++) {
        int zs  = zoneStarts[z];
        int ze  = zoneEnds[z];
        int mid = (zs + ze) / 2;
        int roll = random(100);

        if(tier == 1) {
            if(roll < 50) {
                spawnEnemy(mid, random(2), random(3), 0);
            } else if(roll < 70) {
                int lw = zs + random(20);
                int lr = min(lw + 50 + (int)random(30), ze);
                _trySpawnLava(lw, lr, 2000, 2000, 0, "OFF");
            }
            // else: empty zone

        } else if(tier == 2) {
            if(roll < 30) {
                spawnEnemy(mid, random(2), 1 + random(4), 0);
            } else if(roll < 55) {
                int lw = zs + random(20);
                int lr = min(lw + 40 + (int)random(40), ze);
                _trySpawnLava(lw, lr, 1500, 1500, random(1000), "OFF");
            } else if(roll < 75) {
                spawnConveyor(zs, ze, random(2) ? 1 : -1);
            } else if(roll < 90) {
                spawnEnemy(mid, random(2), 1 + random(3), 0);
                spawnConveyor(zs, ze, random(2) ? 1 : -1);
            }
            if(coopMode && random(100) < 25) {
                spawnMarkedEnemy(mid + (int)random(40) - 20, random(2), 1 + random(3), 0, random(2));
            }

        } else if(tier == 3) {
            if(roll < 25) {
                spawnEnemy(mid, random(2), 2 + random(5), 100 + random(150));
            } else if(roll < 45) {
                int lw = zs + random(10);
                int lr = min(lw + 40 + (int)random(50), ze);
                _trySpawnLava(lw, lr, 1000, 1200, random(1500), "OFF");
                if(random(2)) spawnConveyor(zs, ze, random(2) ? 1 : -1);
            } else if(roll < 65) {
                spawnConveyor(zs, ze, random(2) ? 1 : -1);
                spawnEnemy(mid, random(2), 2 + random(4), 0);
            } else if(roll < 80) {
                spawnSpawner(random(2) ? ze : zs, 3000, 2 + random(3), random(2), 0);
            }
            if(coopMode && random(100) < 35) {
                spawnMarkedEnemy(mid + (int)random(30) - 15, random(2), 2 + random(3), 0, random(2));
            }

        } else { // tier 4
            if(roll < 20 && z == 4) {
                spawnBoss(0, 800);
            } else if(roll < 45) {
                int lw = zs + random(10);
                int lr = min(lw + 50 + (int)random(60), ze);
                _trySpawnLava(lw, lr, 800, 1000, random(2000), "OFF");
                spawnConveyor(zs, ze, random(2) ? 1 : -1);
            } else if(roll < 70) {
                spawnConveyor(zs, ze, random(2) ? 1 : -1);
                spawnEnemy(mid - 30, random(2), 3 + random(4), 0);
                spawnEnemy(mid + 30, random(2), 2 + random(4), 0);
            } else {
                int lw = zs + random(10);
                int lr = min(lw + 40 + (int)random(50), ze);
                _trySpawnLava(lw, lr, 700, 900, random(1500), "ON");
                spawnEnemy(mid, random(2), 2 + random(5), 100 + random(150));
            }
            if(coopMode && random(100) < 50) {
                spawnMarkedEnemy(mid + (int)random(40) - 20, random(2), 2 + random(4), 0, random(2));
            }
        }
    }
}

#endif
```

- [ ] **Step 2: Include ProceduralLevel.h in TWANG.ino**

At the top of `TWANG.ino`, after the other `#include` lines:

```cpp
#include "ProceduralLevel.h"
```

- [ ] **Step 3: Compile**

```bash
arduino-cli compile --fqbn arduino:avr:mega .
```
Expected: no errors. If you see "multiple definition" errors for `_lavaTotal`, ensure the `static` keyword is present on that variable in ProceduralLevel.h.

- [ ] **Step 4: Commit**

```bash
git add ProceduralLevel.h TWANG.ino
git commit -m "feat: add procedural level generator with 4 difficulty tiers"
```

---

### Task 9: Wire procedural generator into loadLevel()

**Files:**
- Modify: `TWANG.ino`

- [ ] **Step 1: Add the procedural branch at the top of loadLevel()**

In `loadLevel()`, after `cleanupLevel();` and before `playerPosition = 0;` (now `players[0].position = 0;`), add:

```cpp
// Reset player positions
for(int i = 0; i < playerCount; i++) {
    players[i].alive          = true;
    players[i].attacking      = false;
    players[i].positionModifier = 0;
    players[i].respawnAt      = 0;
}
players[0].position = 0;
if(gameMode == VERSUS) {
    players[0].position = 0;
    players[1].position = 1000;
} else {
    players[1].position = 0;
}

if(proceduralMode) {
    randomSeed(millis());
    generateLevel(proceduralDifficulty, gameMode == COOP);
    stageStartTime = millis();
    stage = "PLAY";
    return;
}
```

Remove the old `playerPosition = 0; playerAlive = 1;` lines that were below.

- [ ] **Step 2: Compile**

```bash
arduino-cli compile --fqbn arduino:avr:mega .
```
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add TWANG.ino
git commit -m "feat: wire procedural generator into loadLevel()"
```

---

### Task 10: Final integration — full compile, upload, and verify

- [ ] **Step 1: Full compile**

```bash
arduino-cli compile --fqbn arduino:avr:mega .
```
Expected: clean compile with no errors.

- [ ] **Step 2: Find your Arduino port**

```bash
arduino-cli board list
```
Look for a line with `arduino:avr:mega`. Note the port (e.g. `/dev/cu.usbmodem14101` on Mac).

- [ ] **Step 3: Upload**

```bash
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn arduino:avr:mega .
```

- [ ] **Step 4: Manual verification checklist**

- [ ] Boot → three colored pools visible on strip (green/cyan/purple)
- [ ] White LED (player) moves with tilt; stays within strip bounds
- [ ] Standing on SOLO pool → ascending beeps play
- [ ] Standing on COOP pool → two-tone melody plays
- [ ] Standing on VERSUS pool → low pulse plays
- [ ] Wobble on SOLO → sub-select screen (left half green, right half white)
- [ ] Wobble left half → classic mode, level 1 loads (hand-crafted)
- [ ] Wobble right half → endless mode, procedural level loads (different each attempt)
- [ ] Wobble on COOP → two players appear (green + cyan), shared lives, marked enemies glow player color, only owner can kill their marked enemy
- [ ] Wobble on VERSUS → players at opposite ends, killing each other flashes strip, first to 3 kills wins
- [ ] After any game over → returns to mode select
- [ ] Screensaver triggers after 30s idle on mode select; tilt to return

- [ ] **Step 5: Push**

```bash
git push origin main
```
