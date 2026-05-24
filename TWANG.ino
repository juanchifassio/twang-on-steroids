// Required libs
#include "FastLED.h"
#include "I2Cdev.h"
#include "MPU6050.h"
#include "Wire.h"
#include "toneAC.h"
#include "iSin.h"
#include "RunningMedian.h"

// Included libs
#include "Enemy.h"
#include "Particle.h"
#include "Spawner.h"
#include "Lava.h"
#include "Boss.h"
#include "Conveyor.h"
#include "ProceduralLevel.h"

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

// POOLS
Enemy enemyPool[10] = {
    Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy()
};
int const enemyCount = 10;

Particle particlePool[40] = {
    Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle()
};
int const particleCount = 40;

Spawner spawnPool[4] = {
    Spawner(), Spawner(), Spawner(), Spawner()
};
int const spawnCount = 4;

Lava lavaPool[16] = {
    Lava(), Lava(), Lava(), Lava(), Lava(), Lava(), Lava(), Lava()
};
int const lavaCount = 16;

Conveyor conveyorPool[8] = {
    Conveyor(), Conveyor(), Conveyor(), Conveyor(), Conveyor(), Conveyor(), Conveyor(), Conveyor()
};
int const conveyorCount = 8;

// Boss boss = Boss();
Boss bossPool[2] = {
    Boss(), Boss()
};
int const bossCount = 2;

CRGB leds[NUM_LEDS];

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

    if(players[0].wobble > ATTACK_THRESHOLD && millis() - stageStartTime > 500) {
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

void loop() {
    long mm = millis();
    int brightness = 0;
    
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
    
    if (mm - previousMillis >= MIN_REDRAW_INTERVAL) {
        getInput();
        long frameTimer = mm;
        previousMillis = mm;

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

        if(abs(players[0].tilt) > JOYSTICK_DEADZONE) {
            lastInputTime = mm;
            if(stage == "SCREENSAVER") {
                players[0].position = 200;
                stage = "MODE_SELECT";
                stageStartTime = mm;
            }
        } else {
            if(lastInputTime+TIMEOUT < mm){
                stage = "SCREENSAVER";
            }
        }
        if(stage == "SCREENSAVER"){
            screenSaverTick();
        }else if(stage == "PLAY"){
            // PLAYING
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

            // Ticks and draw calls
            FastLED.clear();
            tickConveyors();
            tickSpawners();
            tickBoss();
            tickLava();
            tickEnemies();
            drawPlayer();
            drawAttack();
            drawExit();
        } else if(stage == "WIN") {
            SFXwin();
            if(millis() - stageStartTime > 3000) {
                if(gameMode == VERSUS) {
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
        }else if(stage == "GAMEOVER"){
            // GAME OVER!
            FastLED.clear();
            stageStartTime = 0;
        }
        
        Serial.print(millis()-mm);
        Serial.print(" - ");
        FastLED.show();
        Serial.println(millis()-mm);
    }
}

void normalizeParams(){
    MAX_PLAYER_SPEED = NORMAL_PLAYER_SPEED;
    FREQUENCY_MULTIPLIER = 1.0;
    ATTACK_DURATION = NORMAL_ATTACK_DURATION;
    ATTACK_DELAY = NORMAL_ATTACK_DELAY;
}

// ---------------------------------
// ------------ LEVELS -------------
// ---------------------------------
void loadLevel(){
    normalizeParams();
    updateLives();
    cleanupLevel();
    for(int i = 0; i < playerCount; i++) {
        players[i].alive = true;
        players[i].attacking = false;
        players[i].positionModifier = 0;
        players[i].respawnAt = 0;
    }
    players[0].position = 0;
    if(gameMode == VERSUS) {
        players[1].position = 1000;
    } else {
        players[1].position = 0;
    }

    if(proceduralMode) {
        randomSeed(millis());
        generateLevel(proceduralDifficulty, gameMode == COOP);
        stage          = "PLAY";
        stageStartTime = millis();
        return;
    }

    switch(levelNumber){
        case 0: // Difficulty: 0/10
            // Left or right?
            players[0].position = 200;
            spawnEnemy(1, 0, 0, 0);
            break;
        case 1: // Difficulty: 0/10
            // Slow moving enemy
            spawnEnemy(900, 0, 1, 0);
            break;
        case 2: // Difficulty: 1/10
            // Spawning enemies at exit every 2 seconds
            spawnPool[0].Spawn(1000, 3000, 2, 0, 0);
            break;
        case 3: // Difficulty: 1/10
            // Lava intro
            spawnLava(400, 490, 2000, 2000, 0, "OFF");
            spawnPool[0].Spawn(1000, 5500, 3, 0, 0);
            break;
        case 4: // Difficulty: 3/10
            // Async enemy
            spawnEnemy(700, 1, 7, 275);
            spawnEnemy(500, 1, 5, 250);
            break;
        case 5: // Difficulty: 2/10
            // Conveyor
            spawnConveyor(100, 600, -1);
            spawnEnemy(800, 0, 0, 0);
            break;
        case 6: // Difficulty: 5/10
            // Conveyor of enemies
            spawnConveyor(50, 1000, 1);
            spawnEnemy(300, 0, 0, 0);
            spawnEnemy(400, 0, 0, 0);
            spawnEnemy(500, 0, 0, 0);
            spawnEnemy(600, 0, 0, 0);
            spawnEnemy(700, 0, 0, 0);
            spawnEnemy(800, 0, 0, 0);
            spawnEnemy(900, 0, 0, 0);
            break;
        case 7: // Difficulty: 5/10
            // Lava Corridor
            spawnLava(100, 200, 750, 1000, -2000, "OFF");
            spawnLava(100, 150, 1000, 1500, 0, "OFF");
            spawnLava(150, 200, 1000, 1500, -1000, "OFF");
            spawnLava(200, 250, 1000, 1500, -2000, "OFF");
            spawnLava(250, 292, 1000, 1500, 0, "OFF");
            spawnEnemy(295, 0, 0, 0);
            spawnLava(300, 350, 1000, 1500, -1000, "OFF");
            spawnLava(350, 400, 1000, 1500, -2000, "OFF");
            spawnLava(400, 450, 1000, 1500, 0, "OFF");
            spawnLava(450, 500, 1000, 1500, -1000, "OFF");
            spawnLava(500, 550, 1000, 1500, -2000, "OFF");
            spawnLava(550, 590, 1000, 1500, 0, "OFF");
            spawnEnemy(595, 0, 0, 0);
            spawnLava(598, 650, 1000, 1500, -1000, "OFF");
            spawnLava(650, 700, 1000, 1500, -2000, "OFF");
            spawnLava(700, 850, 1000, 1500, 0, "OFF");
            spawnLava(850, 900, 1000, 1500, -1000, "OFF");
            spawnEnemy(904, 0, 0, 0);
            break;
        case 8: // Difficulty: 5/10
            // slowmo, async enemies and spawner
            MAX_PLAYER_SPEED = 4;
            FREQUENCY_MULTIPLIER = 0.2;
            ATTACK_DURATION = 850;
            ATTACK_DELAY = 1500;
            spawnPool[0].Spawn(900, 2250, 1, 0, 0);
            spawnPool[0].Spawn(300, 2250, 1, -1, 0);
            spawnEnemy(700, 1, 7, 275);
            spawnEnemy(500, 1, 5, 250);
            break;
        case 9: // Difficulty: 7/10
            // miniBoss
            spawnBoss(0, 800);
            break;
        case 10: // Difficulty: 7/10
            // mini lava pools with conveyors and spawner
            spawnConveyor(50, 200, 1);
            spawnConveyor(200, 350, -1);
            spawnConveyor(350, 500, 1);
            spawnConveyor(500, 650, -1);
            spawnConveyor(650, 800, 1);
            spawnConveyor(800, 950, -1);
            spawnConveyor(950, 1000, 1);
            spawnEnemy(200, 0, 0, 0);
            spawnEnemy(350, 0, 0, 0);
            spawnEnemy(500, 0, 0, 0);
            spawnEnemy(650, 0, 0, 0);
            spawnEnemy(800, 0, 0, 0);
            spawnEnemy(950, 0, 0, 0);
            spawnPool[0].Spawn(1000, 3800, 4, 0, 0);
            break;
        case 11: // Difficulty: 7/10
            // Conveyor and async enemies
            spawnConveyor(100, 200, 1);
            spawnConveyor(200, 375, -1);
            spawnConveyor(410, 500, 1);
            spawnConveyor(575, 650, -1);
            spawnConveyor(800, 950, -1);
            spawnEnemy(225, 1, 7, 125);
            spawnEnemy(325, 1, 5, 250);
            spawnEnemy(500, 1, 5, 250);
            spawnEnemy(700, 1, 7, 275);
            spawnPool[0].Spawn(1000, 3000, 2, 0, 0);
            break;
        case 12: // Difficulty: 8/10
            // Conveyor of enemies and lava
            spawnConveyor(50, 200, 1);
            spawnLava(195, 300, 750, 1000, 0, "OFF");
            spawnConveyor(300, 450, -1);
            spawnLava(500, 550, 1500, 1500, 250, "OFF");
            spawnConveyor(550, 700, -1);
            spawnConveyor(700, 850, 1);
            spawnLava(850, 950, 500, 2000, 0, "OFF");
            spawnPool[1].Spawn(525, 3000, 1, 0, 0);
            spawnPool[0].Spawn(1000, 3000, 2, 0, 0);
            break;
        case 13: // Difficulty: 5/10
            // Lava run
            spawnLava(195, 300, 2000, 2000, 0, "OFF");
            spawnLava(350, 455, 2000, 2000, 0, "OFF");
            spawnLava(510, 610, 2000, 2000, 0, "OFF");
            spawnLava(660, 760, 2000, 2000, 0, "OFF");
            spawnPool[0].Spawn(0, 3800, 4, 1, 0);
            spawnPool[1].Spawn(1000, 5000, 4, 0, 0);
            break;
        case 14: // Difficulty: 9/10
            // fastmode, conveyor, lava and spawner
            MAX_PLAYER_SPEED = 14;
            FREQUENCY_MULTIPLIER = 1.7;
            ATTACK_DURATION = 200;
            ATTACK_DELAY = 200;
            spawnConveyor(150, 200, 1);
            spawnConveyor(250, 300, -1);
            spawnConveyor(350, 400, 1);
            spawnConveyor(450, 500, -1);
            spawnConveyor(750, 800, 1);
            spawnConveyor(850, 900, -1);
            spawnLava(200, 250, 2000, 2000, 0, "ON");
            spawnLava(400, 450, 2000, 2000, 0, "OFF");
            spawnLava(600, 650, 2000, 2000, 0, "ON");
            spawnLava(800, 850, 2000, 2000, 0, "OFF");
            spawnPool[0].Spawn(1000, 500, 6, 0, 0);
            spawnPool[1].Spawn(500, 1000, 6, 0, 0);
            spawnEnemy(370, 0, 0, 0);
            spawnEnemy(470, 0, 0, 0);
            spawnEnemy(670, 0, 0, 0);
            spawnEnemy(900, 0, 0, 0);
            break;
        case 15: // Difficulty: 7/10
            // Sin enemy #2
            spawnEnemy(700, 1, 7, 275);
            spawnEnemy(500, 1, 5, 250);
            spawnPool[0].Spawn(1000, 5500, 4, 0, 3000);
            spawnPool[1].Spawn(0, 5500, 5, 1, 10000);
            spawnConveyor(100, 900, -1);
            break;
        case 16: // Difficulty: 10/10
            // Boss
            spawnLava(10, 80, 1000, 1000, 0, "OFF");
            spawnLava(920, 1000, 1000, 1000, 0, "OFF");
            spawnBoss(0, 800);
            spawnBoss(1, 200);
            break;
    }
    stageStartTime = millis();
    stage = "PLAY";
}

void spawnBoss(int bossInd, int pos){
    if(!bossPool[bossInd].Alive()){
        bossPool[bossInd].Spawn(pos);
        moveBoss(bossInd);
        return;
    }
}

void moveBoss(int bossInd){
    int spawnSpeed = 3500;
    if(bossPool[bossInd]._lives == 2) spawnSpeed = 2500;
    if(bossPool[bossInd]._lives == 1) spawnSpeed = 2000;
    int spawnerIndRight = bossInd*2;
    int spawnerIndLeft = bossInd*2 + 1;
    spawnPool[spawnerIndRight].Spawn(bossPool[bossInd]._pos, spawnSpeed, 3, 0, 0);
    spawnPool[spawnerIndLeft].Spawn(bossPool[bossInd]._pos, spawnSpeed, 3, 1, 0);
}

void spawnEnemy(int pos, int dir, int sp, int wobble){
    for(int e = 0; e<enemyCount; e++){
        if(!enemyPool[e].Alive()){
            enemyPool[e].Spawn(pos, dir, sp, wobble);
            enemyPool[e].playerSide = pos > players[0].position ? 1 : -1;
            return;
        }
    }
}

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

void spawnLava(int left, int right, int ontime, int offtime, int offset, char* state){
    for(int i = 0; i<lavaCount; i++){
        if(!lavaPool[i].Alive()){
            lavaPool[i].Spawn(left, right, ontime, offtime, offset, state);
            return;
        }
    }
}

void spawnConveyor(int startPoint, int endPoint, int dir){
    for(int i = 0; i<conveyorCount; i++){
        if(!conveyorPool[i]._alive){
            conveyorPool[i].Spawn(startPoint, endPoint, dir);
            return;
        }
    }
}

void spawnSpawner(int pos, int rate, int sp, int dir, long activate) {
    for(int s = 0; s < spawnCount; s++){
        if(!spawnPool[s].Alive()){
            spawnPool[s].Spawn(pos, rate, sp, dir, activate);
            return;
        }
    }
}

void cleanupLevel(){
    for(int i = 0; i<bossCount; i++){
        bossPool[i].Kill();
    }
    for(int i = 0; i<enemyCount; i++){
        enemyPool[i].Kill();
    }
    for(int i = 0; i<particleCount; i++){
        particlePool[i].Kill();
    }
    for(int i = 0; i<spawnCount; i++){
        spawnPool[i].Kill();
    }
    for(int i = 0; i<lavaCount; i++){
        lavaPool[i].Kill();
    }
    for(int i = 0; i<conveyorCount; i++){
        conveyorPool[i].Kill();
    }
}

void levelComplete(){
    stageStartTime = millis();
    if(proceduralMode) proceduralDifficulty++;
    stage = "WIN";
    if(levelNumber == LEVEL_COUNT) stage = "COMPLETE";
    players[0].lives = 3;
    updateLives();
}

void nextLevel(){
    levelNumber ++;
    if(levelNumber > LEVEL_COUNT) levelNumber = 0;
    loadLevel();
}

void gameOver(){
    levelNumber = 0;
    loadLevel();
}

void die(int playerIndex) {
    players[playerIndex].alive = false;
    players[playerIndex].killTime = millis();

    if(gameMode == SOLO_CLASSIC || gameMode == SOLO_ENDLESS) {
        for(int p = 0; p < particleCount; p++){
            particlePool[p].Spawn(players[playerIndex].position);
        }
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

// ----------------------------------
// -------- TICKS & RENDERS ---------
// ----------------------------------
void tickEnemies(){
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
                        die(0); // marked enemy kills — costs 1 shared life, kills all visually
                        if(playerCount == 2 && players[1].alive) {
                            players[1].alive = false;
                            players[1].respawnAt = millis() + 2000;
                        }
                    } else {
                        die(p);
                    }
                    return;
                }
            }
        }
    }
}

void tickBoss(){
    // DRAW
    for(int b = 0; b<bossCount; b++){
      if(bossPool[b].Alive()) {
        bossPool[b]._ticks ++;
        for(int i = getLED(bossPool[b]._pos-BOSS_WIDTH/2); i<=getLED(bossPool[b]._pos+BOSS_WIDTH/2); i++){
            leds[i] = CRGB::DarkRed;
            leds[i] %= 100;
        }
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
      }
    }
}

void drawPlayer(){
    for(int i = 0; i < playerCount; i++) {
        if(players[i].alive) {
            leds[getLED(players[i].position)] = players[i].color;
        }
    }
}

void drawExit(){
    if(!bossPool[0].Alive() && !bossPool[1].Alive()){
        leds[NUM_LEDS-1] = CRGB(0, 0, 255);
    }
}

void tickSpawners(){
    long mm = millis();
    for(int s = 0; s<spawnCount; s++){
        if(spawnPool[s].Alive() && spawnPool[s]._activate < mm){
            if(spawnPool[s]._lastSpawned + spawnPool[s]._rate < mm || spawnPool[s]._lastSpawned == 0){
                spawnEnemy(spawnPool[s]._pos, spawnPool[s]._dir, spawnPool[s]._sp, 0);
                spawnPool[s]._lastSpawned = mm;
            }
        }
    }
}

void tickLava(){
    int A, B, p, i, brightness, flicker;
    long mm = millis();
    Lava LP;
    for(i = 0; i<lavaCount; i++){
        flicker = random8(5);
        LP = lavaPool[i];
        if(LP.Alive()){
            A = getLED(LP._left);
            B = getLED(LP._right);
            if(LP._state == "OFF"){
                if(LP._lastOn + LP._offtime < mm){
                    LP._state = "ON";
                    LP._lastOn = mm;
                }
                for(p = A; p<= B; p++){
                    leds[p] = CRGB(3+flicker, (3+flicker)/1.5, 0);
                }
            }else if(LP._state == "ON"){
                if(LP._lastOn + LP._ontime < mm){
                    LP._state = "OFF";
                    LP._lastOn = mm;
                }
                for(p = A; p<= B; p++){
                    leds[p] = CRGB(150+flicker, 100+flicker, 0);
                }
            }
        }
        lavaPool[i] = LP;
    }
}

bool tickParticles(){
    bool stillActive = false;
    for(int p = 0; p < particleCount; p++){
        if(particlePool[p].Alive()){
            particlePool[p].Tick(USE_GRAVITY);
            leds[getLED(particlePool[p]._pos)] += CRGB(particlePool[p]._power, 0, 0);
            stillActive = true;
        }
    }
    return stillActive;
}

void tickConveyors(){
    int b, dir, n, i, ss, ee, led;
    long m = 10000+millis();

    // Reset all player modifiers at top of function
    for(int p = 0; p < playerCount; p++) players[p].positionModifier = 0;

    for(i = 0; i<conveyorCount; i++){
        if(conveyorPool[i]._alive){
            dir = conveyorPool[i]._dir;
            ss = getLED(conveyorPool[i]._startPoint);
            ee = getLED(conveyorPool[i]._endPoint);
            for(led = ss; led<ee; led++){
                b = 5;
                n = (-led + (m/100)) % 5;
                if(dir == -1) n = (led + (m/100)) % 5;
                b = (5-n)/2.0;
                if(b > 0) leds[led] = CRGB(0, 0, b);
            }

            for(int p = 0; p < playerCount; p++) {
                if(players[p].position > conveyorPool[i]._startPoint &&
                   players[p].position < conveyorPool[i]._endPoint) {
                    int mod = (MAX_PLAYER_SPEED - 4 <= 0) ? 1 : (MAX_PLAYER_SPEED - 4);
                    players[p].positionModifier = (dir == -1) ? -mod : mod;
                }
            }
        }
    }
}

void drawAttack(){
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

int getLED(int pos){
    // The world is 1000 pixels wide, this converts world units into an LED number
    return constrain((int)map(pos, 0, 1000, 0, NUM_LEDS-1), 0, NUM_LEDS-1);
}

bool inLava(int pos){
    // Returns if the player is in active lava
    int i;
    Lava LP;
    for(i = 0; i<lavaCount; i++){
        LP = lavaPool[i];
        if(LP.Alive() && LP._state == "ON"){
            if(LP._left < pos && LP._right > pos) return true;
        }
    }
    return false;
}

void updateLives(){
    int l = players[0].lives;
    for(int i = 0; i < 3; i++){
        digitalWrite(lifeLEDs[i], l > i ? HIGH : LOW);
    }
}


// ---------------------------------
// --------- SCREENSAVER -----------
// ---------------------------------
void screenSaverTick(){
    int n, b, c, i;
    long mm = millis();
    int mode = (mm/20000)%2;
    
    for(i = 0; i<NUM_LEDS; i++){
        leds[i].nscale8(250);
    }
    if(mode == 0){
        // Marching green <> orange
        n = (mm/250)%10;
        b = 10+((sin(mm/500.00)+1)*20.00);
        c = 20+((sin(mm/5000.00)+1)*33);
        for(i = 0; i<NUM_LEDS; i++){
            if(i%10 == n){
                leds[i] = CHSV( c, 255, 150);
            }
        }
    }else if(mode == 1){
        // Random flashes
        randomSeed(mm);
        for(i = 0; i<NUM_LEDS; i++){
            if(random8(200) == 0){
                leds[i] = CHSV( 25, 255, 100);
            }
        }
    }
}

// ---------------------------------
// ----------- JOYSTICK ------------
// ---------------------------------
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


// ---------------------------------
// -------------- SFX --------------
// ---------------------------------
void SFXtilt(int amount){
    int f = map(abs(amount), 0, 90, 80, 900)+random8(100);
    if(players[0].positionModifier < 0) f -= 500;
    if(players[0].positionModifier > 0) f += 200;
    toneAC(f * FREQUENCY_MULTIPLIER, min(min(abs(amount)/9, 5), MAX_VOLUME));
}

void SFXattacking(){
    int freq = map(sin(millis()/2.0)*1000.0, -1000, 1000, 500, 600);
    if(random8(5)== 0){
      freq *= 3;
    }
    toneAC(freq * FREQUENCY_MULTIPLIER, MAX_VOLUME);
}

void SFXdead(){
    int freq = max(1000 - (int)(millis() - players[0].killTime), 10);
    freq += random8(200);
    toneAC(freq * FREQUENCY_MULTIPLIER, MAX_VOLUME);
}

void SFXkill(){
    toneAC(2000 * FREQUENCY_MULTIPLIER, MAX_VOLUME, 1000, true);
}

void SFXwin(){
    int freq = (millis()-stageStartTime)/3.0;
    freq += map(sin(millis()/20.0)*1000.0, -1000, 1000, 0, 20);
    int vol = 10;//max(10 - (millis()-stageStartTime)/200, 0);
    toneAC(freq * FREQUENCY_MULTIPLIER, MAX_VOLUME);
}

void SFXcomplete(){
    noToneAC();
}