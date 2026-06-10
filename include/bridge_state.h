#pragma once

#include <Arduino.h>

struct BridgeState {
    bool lightOn = false;
    int bri = 50;
    int ctMireds = 250;
    bool fanOn = false;
    int fanSpd = 3;
    bool summer = true;
};

extern BridgeState S;

void stLoad();
void stSave();
