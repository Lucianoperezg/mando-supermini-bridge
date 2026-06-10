#pragma once

#include <Arduino.h>

#define FAN_PIN_CSN  10
#define FAN_PIN_SCK  12
#define FAN_PIN_MOSI 11
#define FAN_PIN_MISO 13
#define FAN_PIN_GDO0 9

static constexpr uint32_t FAN_ID_ORIGINAL = 0x2BF6Bu;
static constexpr uint32_t FAN_ID_NUEVO    = 0x0511Eu;

struct FanTxParams {
    uint32_t cellUs      = 250;
    uint32_t gapUs       = 8800;
    uint32_t preLowUs    = 1000;
    uint32_t preStxUs    = 1000;
    uint32_t postBurstUs = 3000;
    uint8_t  paValue     = 0x50;
};

bool fanRfInit();
bool fanRmtInit();
bool fanDeriveProtocol();
bool fanDerivedReady();

bool fanSend(const char* name);
bool fanSendManual(uint32_t addr, uint8_t cmd8, uint8_t c4, uint8_t reps);
void fanPairBruteForce(uint32_t addr, uint32_t dwellMs = 350);

void fanSetAddr(uint32_t addr);
void fanLoadAddr();
uint32_t fanGetAddr();
uint32_t fanGetOriginalAddr();

FanTxParams& fanTxParams();
