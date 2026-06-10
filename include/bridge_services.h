#pragma once

#include "HomeSpan.h"

/**
 * @brief Función auxiliar compartida para el envío de comandos RF por transceptor.
 *        Realiza la transmisión por RF y escribe su estado por puerto serie.
 * @param cmd Nombre del comando RF a enviar (ej. "LIGHT_ON")
 * @return true si el envío fue exitoso, false en caso contrario
 */
bool rf(const char* cmd);

// ============================================================
// Accesorio 2 — Luz
// ============================================================
struct SvcLuz : Service::LightBulb {
    SpanCharacteristic* cOn;
    SpanCharacteristic* cBri;
    SpanCharacteristic* cCT;

    static constexpr unsigned long DEBOUNCE_MS   = 700;
    static constexpr uint16_t      TAP_MS         = 250;
    static constexpr uint16_t      BLOCK_DELAY_MS = 350;
    static constexpr int           MAX_STEPS       = 96;

    struct LightTarget { bool on; int briLevel, ctLevel; };
    struct LightShadow { bool on, synced; int briLevel, ctLevel; };
    enum   StepType    { STEP_RF, STEP_DELAY, STEP_COMMIT };
    struct Step        { StepType type; const char* cmd; uint16_t waitMs; };

    LightTarget   target{};
    LightShadow   shadow{};
    Step          plan[MAX_STEPS];
    int           planLen = 0, planPos = 0;
    bool          running = false, dirty = false;
    unsigned long lastIntentMs = 0, nextStepAt = 0;

    SvcLuz();
    boolean update() override;
    void loop() override;

private:
    void addRF(const char* c, uint16_t w = TAP_MS);
    void addDelay(uint16_t w);
    void addCommit();
    bool isPlanFull() const;
    void planBrightnessDelta(int f, int t);
    void planBrightnessAbsolute(int t);
    void planCTDelta(int f, int t);
    void planCTAbsolute(int t);
    void buildPlan();
    void commitShadow();
    void processPlan();
};

// ============================================================
// Accesorio 3 — Ventilador
// ============================================================
struct SvcFan : Service::Fan {
    SpanCharacteristic* cAct;
    SpanCharacteristic* cSpd;
    bool savePending = false;

    SvcFan();
    boolean update() override;
    void loop() override;
};

// ============================================================
// Accesorio 4 — Giro ventilador
// ON = Verano, OFF = Invierno
// ============================================================
struct SvcGiro : Service::Switch {
    SpanCharacteristic* cOn;
    enum GiroState { IDLE, WAIT_RF2 };
    GiroState     giroState   = IDLE;
    unsigned long giroWaitMs  = 0;
    bool          savePending = false;

    SvcGiro();
    boolean update() override;
    void loop() override;
};

// ============================================================
// Accesorio 5 — Apagar Todo (switch momentáneo)
// ============================================================
struct SvcAllOff : Service::Switch {
    SpanCharacteristic* cOn;
    unsigned long triggeredAt = 0;

    SvcAllOff();
    boolean update() override;
    void loop() override;
};

// ============================================================
// Accesorios 6 y 7 — Temporizadores de hardware (momentáneos)
// ============================================================
struct SvcTimer : Service::Switch {
    SpanCharacteristic* cOn;
    unsigned long triggeredAt = 0;
    uint8_t cmd8;
    uint8_t chk4;

    SvcTimer(uint8_t cmd, uint8_t chk);
    boolean update() override;
    void loop() override;
};
