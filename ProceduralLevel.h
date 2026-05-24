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

inline void generateLevel(int difficulty, bool coopMode) {
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
            if(z == 4) {
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
