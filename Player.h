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
