#include "bridge_services.h"
#include "bridge_state.h"
#include "fan_protocol.h"

// ── Función RF compartida ────────────────────────────────────
bool rf(const char* cmd) {
    bool ok = fanSend(cmd);
    Serial.printf("[RF] %-18s %s\n", cmd, ok ? "OK" : "FAIL");
    return ok;
}

// ── Constantes y helpers de velocidad ────────────────────────
static constexpr const char* SPEED_CMDS[] = {
    "", "SPEED1_A", "SPEED2_A", "SPEED3_A",
    "SPEED4_A", "SPEED5_A", "SPEED6_A"
};
static constexpr int SPD_LEVELS[] = {0, 8, 25, 42, 59, 75, 100};
static constexpr int SPEED_MAX = 6;

static const char* speedCmd(int s) {
    return (s >= 1 && s <= SPEED_MAX) ? SPEED_CMDS[s] : SPEED_CMDS[1];
}

static int hkToSpd(int hk) {
    if (hk <=  0) return 0;
    if (hk <=  8) return 1;
    if (hk <= 25) return 2;
    if (hk <= 42) return 3;
    if (hk <= 59) return 4;
    if (hk <= 75) return 5;
    return SPEED_MAX;
}

static int spdToHk(int s) {
    return (s >= 0 && s <= SPEED_MAX) ? SPD_LEVELS[s] : 0;
}

// ── Constantes y helpers de Brillo / CT ──────────────────────
static constexpr int BRI_LEVELS = 10;
static constexpr int CT_LEVELS  = 10;
static constexpr int CT_MIN     = 153;
static constexpr int CT_MAX     = 370;
static constexpr int CT_RANGE   = CT_MAX - CT_MIN;

static inline int hkBriToLevel(int v) { return constrain((v * BRI_LEVELS) / 100, 0, BRI_LEVELS); }
static inline int levelToHkBri(int l) { return map(constrain(l, 0, BRI_LEVELS), 0, BRI_LEVELS, 0, 100); }
static inline int hkCtToLevel(int m)  { return constrain(((m - CT_MIN) * CT_LEVELS) / CT_RANGE, 0, CT_LEVELS); }
static inline int levelToHkCt(int l)  { return map(constrain(l, 0, CT_LEVELS), 0, CT_LEVELS, CT_MIN, CT_MAX); }

// ============================================================
// Implementación SvcLuz
// ============================================================
SvcLuz::SvcLuz() {
    cOn  = new Characteristic::On(S.lightOn ? 1 : 0);
    cBri = new Characteristic::Brightness(S.bri);
    cCT  = new Characteristic::ColorTemperature(S.ctMireds);
    cBri->setRange(0, 100, 5);
    cCT->setRange(153, 370);
    target = { S.lightOn, hkBriToLevel(S.bri), hkCtToLevel(S.ctMireds) };
    shadow = { S.lightOn, false, hkBriToLevel(S.bri), hkCtToLevel(S.ctMireds) };
}

boolean SvcLuz::update() {
    bool nOn  = cOn->getNewVal<bool>();
    int  nBri = cBri->getNewVal<int>();
    int  nCT  = cCT->getNewVal<int>();
    target = { nOn, hkBriToLevel(nBri), hkCtToLevel(nCT) };
    if (!target.on) { target.briLevel = shadow.briLevel; target.ctLevel = shadow.ctLevel; }
    lastIntentMs = millis();
    dirty = true;
    Serial.printf("[HK][LUZ] on=%d bri=%d(lv=%d) ct=%d(lv=%d)\n",
        target.on, nBri, target.briLevel, nCT, target.ctLevel);
    return true;
}

void SvcLuz::addRF(const char* c, uint16_t w) { 
    if (planLen < MAX_STEPS) plan[planLen++] = {STEP_RF, c, w}; 
}

void SvcLuz::addDelay(uint16_t w) { 
    if (planLen < MAX_STEPS) plan[planLen++] = {STEP_DELAY, nullptr, w}; 
}

void SvcLuz::addCommit() { 
    if (planLen < MAX_STEPS) plan[planLen++] = {STEP_COMMIT, nullptr, 0}; 
}

bool SvcLuz::isPlanFull() const { return planLen >= MAX_STEPS; }

void SvcLuz::planBrightnessDelta(int f, int t) {
    const char* cmd = (t > f) ? "BRIGHT_PLUS_TAP" : "BRIGHT_MINUS_TAP";
    int delta = (t > f) ? (t - f) : (f - t);
    for (int i = 0; i < delta; i++) addRF(cmd);
}

void SvcLuz::planBrightnessAbsolute(int t) {
    for (int i = 0; i < BRI_LEVELS; i++) addRF("BRIGHT_MINUS_TAP");
    addDelay(BLOCK_DELAY_MS);
    for (int i = 0; i < t; i++) addRF("BRIGHT_PLUS_TAP");
}

void SvcLuz::planCTDelta(int f, int t) {
    const char* cmd = (t > f) ? "WARM_PLUS_TAP" : "COLD_PLUS_TAP";
    int delta = (t > f) ? (t - f) : (f - t);
    for (int i = 0; i < delta; i++) addRF(cmd);
}

void SvcLuz::planCTAbsolute(int t) {
    for (int i = 0; i < CT_LEVELS; i++) addRF("COLD_PLUS_TAP");
    addDelay(BLOCK_DELAY_MS);
    for (int i = 0; i < t; i++) addRF("WARM_PLUS_TAP");
}

void SvcLuz::buildPlan() {
    planLen = planPos = 0;
    Serial.printf("[PLAN][LUZ] shadow(on=%d bri=%d ct=%d sync=%d)->target(on=%d bri=%d ct=%d)\n",
        shadow.on, shadow.briLevel, shadow.ctLevel, shadow.synced,
        target.on, target.briLevel, target.ctLevel);
    
    if (!target.on) {
        if (shadow.on) { addRF("LIGHT_OFF", 300); shadow.on = false; }
        shadow.synced = false;
        addCommit();
        Serial.printf("[PLAN][LUZ] steps=%d\n", planLen);
        return;
    }
    
    if (!shadow.on) {
        addRF("LIGHT_ON", 400);
        shadow.on = true; 
        shadow.synced = false;
        addDelay(BLOCK_DELAY_MS);
    }
    
    bool briChanged = (target.briLevel != shadow.briLevel);
    bool ctChanged  = (target.ctLevel  != shadow.ctLevel);
    
    if (briChanged) {
        if (shadow.synced) {
            planBrightnessDelta(shadow.briLevel, target.briLevel);
        } else {
            planBrightnessAbsolute(target.briLevel);
        }
        shadow.briLevel = target.briLevel;
        addDelay(BLOCK_DELAY_MS);
    }
    
    if (ctChanged) {
        if (shadow.synced) {
            planCTDelta(shadow.ctLevel, target.ctLevel);
        } else {
            planCTAbsolute(target.ctLevel);
        }
        shadow.ctLevel = target.ctLevel;
        addDelay(BLOCK_DELAY_MS);
    }
    
    if (briChanged || ctChanged) shadow.synced = true;
    addCommit();
    Serial.printf("[PLAN][LUZ] steps=%d\n", planLen);
}

void SvcLuz::commitShadow() {
    S.lightOn  = shadow.on;
    S.bri      = levelToHkBri(shadow.briLevel);
    S.ctMireds = levelToHkCt(shadow.ctLevel);
    stSave();
    Serial.printf("[COMMIT][LUZ] on=%d bri=%d ct=%d sync=%d\n",
        S.lightOn, S.bri, S.ctMireds, shadow.synced);
}

void SvcLuz::processPlan() {
    if (!running || millis() < nextStepAt) return;
    if (planPos >= planLen) { running = false; Serial.println("[PLAN][LUZ] done"); return; }
    Step& s = plan[planPos++];
    if (s.type == STEP_RF)     { if (!rf(s.cmd)) shadow.synced = false; nextStepAt = millis() + s.waitMs; return; }
    if (s.type == STEP_DELAY)  { nextStepAt = millis() + s.waitMs; return; }
    if (s.type == STEP_COMMIT) { commitShadow(); nextStepAt = millis(); return; }
}

void SvcLuz::loop() {
    processPlan();
    if (running) return;
    if (dirty && millis() - lastIntentMs >= DEBOUNCE_MS) {
        buildPlan(); dirty = false;
        if (planLen > 0) { running = true; nextStepAt = millis(); }
    }
}

// ============================================================
// Implementación SvcFan
// ============================================================
SvcFan::SvcFan() {
    cAct = new Characteristic::Active(S.fanOn ? 1 : 0);
    cSpd = new Characteristic::RotationSpeed(spdToHk(S.fanSpd));
    cSpd->setRange(0, 100, 1);
}

boolean SvcFan::update() {
    bool nAct = cAct->getNewVal<bool>();
    int  nSpd = hkToSpd(cSpd->getNewVal<int>());
    if (!nAct || nSpd == 0) {
        S.fanOn = false;
        cSpd->setVal(0);
        rf("FAN_A");
    } else {
        S.fanOn  = true;
        S.fanSpd = nSpd;
        rf(speedCmd(S.fanSpd));
    }
    savePending = true;
    return true;
}

void SvcFan::loop() {
    if (savePending) { savePending = false; stSave(); }
}

// ============================================================
// Implementación SvcGiro
// ============================================================
SvcGiro::SvcGiro() { 
    cOn = new Characteristic::On(S.summer ? 1 : 0); 
}

boolean SvcGiro::update() {
    S.summer = cOn->getNewVal<bool>();
    rf(S.summer ? "SUMMER_DIR_A" : "WINTER_DIR_A");
    giroState  = WAIT_RF2;
    giroWaitMs = millis() + 400;
    savePending = true;
    return true;
}

void SvcGiro::loop() {
    if (giroState == WAIT_RF2 && millis() >= giroWaitMs) {
        rf("FAN_A");
        S.fanOn   = false;
        giroState = IDLE;
    }
    if (savePending && giroState == IDLE) { savePending = false; stSave(); }
}

// ============================================================
// Implementación SvcAllOff
// ============================================================
SvcAllOff::SvcAllOff() { 
    cOn = new Characteristic::On(0); 
}

boolean SvcAllOff::update() {
    if (cOn->getNewVal<bool>()) {
        S.lightOn = false;
        S.fanOn   = false;
        rf("ALL_OFF_ON");
        stSave();
        triggeredAt = millis();
    }
    return true;
}

void SvcAllOff::loop() {
    if (cOn->getVal<bool>() && triggeredAt > 0 && millis() - triggeredAt > 400) {
        cOn->setVal(false);
        triggeredAt = 0;
    }
}

// ============================================================
// Implementación SvcTimer
// ============================================================
SvcTimer::SvcTimer(uint8_t cmd, uint8_t chk) : cmd8(cmd), chk4(chk) { 
    cOn = new Characteristic::On(0); 
}

boolean SvcTimer::update() {
    if (cOn->getNewVal<bool>()) {
        fanSendManual(fanGetAddr(), cmd8, chk4, 6);
        triggeredAt = millis();
        Serial.printf("[HK][TIMER] Enviado CMD: 0x%02X CHK: 0x%02X\n", cmd8, chk4);
    }
    return true;
}

void SvcTimer::loop() {
    if (cOn->getVal<bool>() && triggeredAt > 0 && millis() - triggeredAt > 400) {
        cOn->setVal(false);
        triggeredAt = 0;
    }
}
