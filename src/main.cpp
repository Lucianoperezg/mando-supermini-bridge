// ============================================================
// homekit_bridge/src/main.cpp  v7
// ESP32-S3 + CC1101 — Bridge HomeKit
// Arduino-ESP32 3.x / IDF 5.x + HomeSpan 2.1.x
//
// Accesorios:
//   [2] Luz Ventilador  — on/off + brillo + temperatura color
//   [3] Ventilador      — on/off + velocidad 1-6
//   [4] Giro            — switch ON=Verano / OFF=Invierno
//   [5] Apagar Todo     — switch momentáneo
//
// Bugs corregidos respecto a v5/v6:
//   [B1] Orden WiFi idéntico a v5 que conecta — sin tocar stack antes de HomeSpan
//   [B2] delay(400) eliminado de SvcGiro::update() — no bloqueante
//   [B3] SvcFan: RotationSpeed=0 con Active=true apaga en vez de arrancar
//   [B4] SvcAllOff: timestamp local en vez de timeVal() incorrecto
//   [B5] stSave() diferido en SvcFan/SvcGiro — evita NVS write storm
//   [B6] cSpd->setVal(0) al apagar ventilador — slider iOS sincronizado
//   [B7] Umbrales hkToSpd/spdToHk simétricos y centrados
//   [B8] SvcLuz reescrita con plan de pasos no bloqueante
// ============================================================

#include <Arduino.h>
#include <Preferences.h>
#include "HomeSpan.h"
#include "fan_protocol.h"

// ── Estado persistente ───────────────────────────────────────
static Preferences gSt;

struct BridgeState {
    bool  lightOn  = false;
    int   bri      = 50;    // HomeKit 0..100
    int   ctMireds = 250;   // HomeKit mireds
    bool  fanOn    = false;
    int   fanSpd   = 3;     // 1..6
    bool  summer   = true;  // true=verano, false=invierno
};

static BridgeState S;

/** 
 * Carga el estado persistente desde NVS (flash)
 * Restaura: luz (on/off, brillo, CT), ventilador (on/off, velocidad), giro (dir)
 */
static void stLoad() {
    gSt.begin("bridge", true);
    S.lightOn  = gSt.getBool("lon",  false);
    S.bri      = gSt.getInt ("bri",  50);
    S.ctMireds = gSt.getInt ("ct",   250);
    S.fanOn    = gSt.getBool("fon",  false);
    S.fanSpd   = gSt.getInt ("fspd", 3);
    S.summer   = gSt.getBool("sum",  true);
    gSt.end();
    Serial.printf("[ST] luz=%s bri=%d ct=%d fan=%s vel=%d dir=%s\n",
        S.lightOn?"ON":"OFF", S.bri, S.ctMireds,
        S.fanOn?"ON":"OFF", S.fanSpd, S.summer?"VER":"INV");
}

static void stSave() {
    gSt.begin("bridge", false);
    gSt.putBool("lon",  S.lightOn);
    gSt.putInt ("bri",  S.bri);
    gSt.putInt ("ct",   S.ctMireds);
    gSt.putBool("fon",  S.fanOn);
    gSt.putInt ("fspd", S.fanSpd);
    gSt.putBool("sum",  S.summer);
    gSt.end();
}

// ── Envío RF con log

/**
 * Envía comando RF al ventilador
 * @param cmd Nombre del comando (ej: "LIGHT_ON", "SPEED2_A")
 * @return true si transmisión exitosa, false si fallo
 */
static bool rf(const char* cmd) { ─────────────────────────────────────────
static bool rf(const char* cmd) {
    bool ok = fanSend(cmd);
    Serial.printf("[RF] %-18s %s\n", cmd, ok ? "OK" : "FAIL");
    return ok;
}

// ── Velocidad ────────────────────────────────────────────────// Array de comandos RF para velocidades 1-6static constexpr const char* SPEED_CMDS[] = {
    "", "SPEED1_A", "SPEED2_A", "SPEED3_A",
    "SPEED4_A", "SPEED5_A", "SPEED6_A"
};
static constexpr int SPD_LEVELS[] = {0, 8, 25, 42, 59, 75, 100};
static constexpr int SPEED_MAX = 6;

/**
 * Retorna comando RF para la velocidad especificada
 * @param s Velocidad (1-6), valores fuera de rango → SPEED1_A
 * @return Nombre del comando RF
 */
static const char* speedCmd(int s) {
    return (s >= 1 && s <= SPEED_MAX) ? SPEED_CMDS[s] : SPEED_CMDS[1];
}

/**
 * Convierte valor HomeKit (0-100) a velocidad (0-6)
 * Umbrales centrados y simétricos: 0→off, 8→1, 25→2, 42→3, 59→4, 75→5, 100→6
 * @param hk Valor HomeKit 0-100
 * @return Velocidad 0-6
 */
static int hkToSpd(int hk) {
    if (hk <=  0) return 0;
    if (hk <=  8) return 1;
    if (hk <= 25) return 2;
    if (hk <= 42) return 3;
    if (hk <= 59) return 4;
    if (hk <= 75) return 5;
    return SPEED_MAX;
}

/**
 * Convierte velocidad (0-6) a valor HomeKit (0-100)
 * @param s Velocidad 0-6
 * @return Valor HomeKit 0-100
 */
static int spdToHk(int s) {
    return (s >= 0 && s <= SPEED_MAX) ? SPD_LEVELS[s] : 0;
}

// ── Brillo / CT helpers ──────────────────────────────────────
// Rangos: brillo 10 niveles (0-100%), CT 10 niveles (153-370 mireds)
static constexpr int BRI_LEVELS = 10;
static constexpr int CT_LEVELS  = 10;
static constexpr int CT_MIN     = 153;
static constexpr int CT_MAX     = 370;
static constexpr int CT_RANGE   = CT_MAX - CT_MIN;  // 217

/**
 * Convierte brillo HomeKit (0-100) a nivel discreto (0-10)
 * @param v Brillo HomeKit 0-100
 * @return Nivel 0-10
 */
static inline int hkBriToLevel(int v) { return constrain((v * BRI_LEVELS) / 100, 0, BRI_LEVELS); }

/**
 * Convierte nivel discreto (0-10) a brillo HomeKit (0-100)
 * @param l Nivel 0-10
 * @return Brillo HomeKit 0-100
 */
static inline int levelToHkBri(int l) { return map(constrain(l, 0, BRI_LEVELS), 0, BRI_LEVELS, 0, 100); }

/**
 * Convierte CT HomeKit (153-370 mireds) a nivel discreto (0-10)
 * @param m CT HomeKit en mireds
 * @return Nivel 0-10
 */
static inline int hkCtToLevel(int m)  { return constrain(((m - CT_MIN) * CT_LEVELS) / CT_RANGE, 0, CT_LEVELS); }

/**
 * Convierte nivel discreto (0-10) a CT HomeKit (153-370 mireds)
 * @param l Nivel 0-10
 * @return CT HomeKit en mireds
 */
static inline int levelToHkCt(int l)  { return map(constrain(l, 0, CT_LEVELS), 0, CT_LEVELS, CT_MIN, CT_MAX); }

// ============================================================
// Accesorio 2 — Luz
// [B8] Plan de pasos no bloqueante con shadow state y debounce
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

    /**
     * Constructor: inicializa características y estados iniciales
     */
    SvcLuz() {
        cOn  = new Characteristic::On(S.lightOn ? 1 : 0);
        cBri = new Characteristic::Brightness(S.bri);
        cCT  = new Characteristic::ColorTemperature(S.ctMireds);
        cBri->setRange(0, 100, 5);
        cCT->setRange(153, 370);
        target = { S.lightOn, hkBriToLevel(S.bri), hkCtToLevel(S.ctMireds) };
        shadow = { S.lightOn, false, hkBriToLevel(S.bri), hkCtToLevel(S.ctMireds) };
    }

    /**
     * Callback de HomeKit: procesa cambios de on/off, brillo, CT
     */
    boolean update() override {
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

    void addRF(const char* c, uint16_t w = TAP_MS) { 
        if (planLen < MAX_STEPS) plan[planLen++] = {STEP_RF, c, w}; 
    }

    /**
     * Agrega espera al plan de ejecución
     * @param w Duración de espera (ms)
     */
    void addDelay(uint16_t w) { 
        if (planLen < MAX_STEPS) plan[planLen++] = {STEP_DELAY, nullptr, w}; 
    }

    /**
     * Agrega punto de sincronización: guarda shadow → flash
     */
    void addCommit() { 
        if (planLen < MAX_STEPS) plan[planLen++] = {STEP_COMMIT, nullptr, 0}; 
    }

    /**
     * Verifica si el plan de ejecución está lleno
     * @return true si planLen >= MAX_STEPS (96)
     */
    bool isPlanFull() const { return planLen >= MAX_STEPS; }

    /**
     * Planifica cambio incremental de brillo (delta)
     * Usa PLUS/MINUS según dirección
     * @param f Nivel inicial
     * @param t Nivel target
     */
    void planBrightnessDelta(int f, int t) {
        const char* cmd = (t > f) ? "BRIGHT_PLUS_TAP" : "BRIGHT_MINUS_TAP";
        int delta = (t > f) ? (t - f) : (f - t);
        for (int i = 0; i < delta; i++) addRF(cmd);
    }

    /**
     * Planifica brillo absoluto: baja a 0, luego sube al target
     * Garantiza posición exacta
     * @param t Nivel target (0-10)
     */
    void planBrightnessAbsolute(int t) {
        for (int i = 0; i < BRI_LEVELS; i++) addRF("BRIGHT_MINUS_TAP");
        addDelay(BLOCK_DELAY_MS);
        for (int i = 0; i < t; i++) addRF("BRIGHT_PLUS_TAP");
    }

    /**
     * Planifica cambio incremental de CT (delta)
     * Usa WARM/COLD según dirección
     * @param f Nivel inicial
     * @param t Nivel target
     */
    void planCTDelta(int f, int t) {
        const char* cmd = (t > f) ? "WARM_PLUS_TAP" : "COLD_PLUS_TAP";
        int delta = (t > f) ? (t - f) : (f - t);
        for (int i = 0; i < delta; i++) addRF(cmd);
    }

    /**
     * Planifica CT absoluto: resetea a frío, luego sube al target
     * Garantiza posición exacta
     * @param t Nivel target (0-10)
     */
    void planCTAbsolute(int t) {
        for (int i = 0; i < CT_LEVELS; i++) addRF("COLD_PLUS_TAP");
        addDelay(BLOCK_DELAY_MS);
        for (int i = 0; i < t; i++) addRF("WARM_PLUS_TAP");
    }

    /**
     * Construye plan de ejecución: compara shadow vs target
     * Genera secuencia de comandos RF, delays y commits
     */
    void buildPlan() {
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

    /**
     * Confirma shadow → estado persistent
     * Guarda en NVS: on/off, brillo, CT
     */
    void commitShadow() {
        S.lightOn  = shadow.on;
        S.bri      = levelToHkBri(shadow.briLevel);
        S.ctMireds = levelToHkCt(shadow.ctLevel);
        stSave();
        Serial.printf("[COMMIT][LUZ] on=%d bri=%d ct=%d sync=%d\n",
            S.lightOn, S.bri, S.ctMireds, shadow.synced);
    }

    /**
     * Ejecuta un paso del plan actual
     * Maneja: STEP_RF (transmite), STEP_DELAY (espera), STEP_COMMIT (guarda)
     */
    void processPlan() {
        if (!running || millis() < nextStepAt) return;
        if (planPos >= planLen) { running = false; Serial.println("[PLAN][LUZ] done"); return; }
        Step& s = plan[planPos++];
        if (s.type == STEP_RF)     { if (!rf(s.cmd)) shadow.synced = false; nextStepAt = millis() + s.waitMs; return; }
        if (s.type == STEP_DELAY)  { nextStepAt = millis() + s.waitMs; return; }
        if (s.type == STEP_COMMIT) { commitShadow(); nextStepAt = millis(); return; }
    }

    /**
     * Loop HomeSpan: ejecuta plan, detecta cambios con debounce
     */
    void loop() override {
        processPlan();
        if (running) return;
        if (dirty && millis() - lastIntentMs >= DEBOUNCE_MS) {
            buildPlan(); dirty = false;
            if (planLen > 0) { running = true; nextStepAt = millis(); }
        }
    }
};

// ============================================================
// Accesorio 3 — Ventilador
// ============================================================
struct SvcFan : Service::Fan {
    SpanCharacteristic* cAct;
    SpanCharacteristic* cSpd;
    bool savePending = false;

    /**
     * Constructor: inicializa actividad y velocidad
     */
    SvcFan() {
        cAct = new Characteristic::Active(S.fanOn ? 1 : 0);
        cSpd = new Characteristic::RotationSpeed(spdToHk(S.fanSpd));
        cSpd->setRange(0, 100, 1);
    }

    /**
     * Callback HomeKit: procesa Active y RotationSpeed
     */
    boolean update() override {
        bool nAct = cAct->getNewVal<bool>();
        int  nSpd = hkToSpd(cSpd->getNewVal<int>());
        // [B3] speed=0 con active=true se trata como apagado
        if (!nAct || nSpd == 0) {
            S.fanOn = false;
            cSpd->setVal(0);   // [B6] sincroniza slider iOS
            rf("FAN_A");
        } else {
            S.fanOn  = true;
            S.fanSpd = nSpd;
            rf(speedCmd(S.fanSpd));
        }
        savePending = true;   // [B5] NVS diferido
        return true;
    }

    /**
     * Loop HomeSpan: ejecuta guardado diferido en NVS
     */
    void loop() override {
        if (savePending) { savePending = false; stSave(); }
    }
};

// ============================================================
// Accesorio 4 — Giro ventilador
// ON = Verano, OFF = Invierno
// ============================================================
struct SvcGiro : Service::Switch {
    SpanCharacteristic* cOn;
    // [B2] Máquina de estados — reemplaza delay(400) bloqueante
    enum GiroState { IDLE, WAIT_RF2 };
    GiroState     giroState   = IDLE;
    unsigned long giroWaitMs  = 0;
    bool          savePending = false;

    /**
     * Constructor: inicializa switch de dirección (verano/invierno)
     */
    SvcGiro() { cOn = new Characteristic::On(S.summer ? 1 : 0); }

    /**
     * Callback HomeKit: ON=verano, OFF=invierno
     */
    boolean update() override {
        S.summer = cOn->getNewVal<bool>();
        rf(S.summer ? "SUMMER_DIR_A" : "WINTER_DIR_A");
        giroState  = WAIT_RF2;
        giroWaitMs = millis() + 400;
        savePending = true;
        return true;
    }

    /**
     * Loop HomeSpan: máquina de estados no bloqueante
     * IDLE → WAIT_RF2 (400ms) → IDLE
     * Evita delay() bloqueante, sincroniza con NVS
     */
    void loop() override {
        if (giroState == WAIT_RF2 && millis() >= giroWaitMs) {
            rf("FAN_A");
            S.fanOn   = false;
            giroState = IDLE;
        }
        if (savePending && giroState == IDLE) { savePending = false; stSave(); }
    }
};

// ============================================================
// Accesorio 5 — Apagar Todo (switch momentáneo)
// ============================================================
struct SvcAllOff : Service::Switch {
    SpanCharacteristic* cOn;
    unsigned long triggeredAt = 0;   // [B4] timestamp local

    /**
     * Constructor: inicializa switch momentáneo (apagar todo)
     */
    SvcAllOff() { cOn = new Characteristic::On(0); }

    /**
     * Callback HomeKit: switch momentáneo (pulse)
     */
    boolean update() override {
        if (cOn->getNewVal<bool>()) {
            S.lightOn = false;
            S.fanOn   = false;
            rf("ALL_OFF_ON");
            stSave();
            triggeredAt = millis();
        }
        return true;
    }

    /**
     * Loop HomeSpan: resetea switch tras ~400ms (simula pulsador momentáneo)
     */
    void loop() override {
        if (cOn->getVal<bool>() && triggeredAt > 0 && millis() - triggeredAt > 400) {
            cOn->setVal(false);
            triggeredAt = 0;
        }
    }
};

// ============================================================
// Consola RF (CLI) — Comandos para debugging y configuración
// ============================================================

/**
 * Imprime lista de comandos disponibles en consola
 */
static void cliPrintHelp() {
    Serial.println(F("Commands:"));
    Serial.println(F("  rftest         - Test light commands"));
    Serial.println(F("  rfsend <CMD>   - Send raw command"));
    Serial.println(F("  rfcell <us>    - Set cell duration (microseconds)"));
    Serial.println(F("  rfid           - Show current ID"));
    Serial.println(F("  rfid nuevo|original|set <hex>"));
    Serial.println(F("  rfpair [ms]    - Pair with brute force"));
    Serial.println(F("  rfstatus       - Show RF status and bridge state"));
}

/**
 * Prueba RF: envía LIGHT_ON 3 veces con pausa de 600ms
 */
static void cliTestRF() {
    for (int i = 0; i < 3; i++) {
        rf("LIGHT_ON");
        delay(600);
    }
}

/**
 * Configura duración de celda RF en microsegundos
 * @param val String con número de microsegundos
 */
static void cliSetCell(const char* val) {
    if (!val) return;
    uint32_t newCell = (uint32_t)atoi(val);
    fanTxParams().cellUs = newCell;
    Serial.printf("[RF] cellUs=%lu\n", newCell);
}

/**
 * Maneja subcomandos rfid: muestra, set nuevo, original
 * @param subcmd Subcomando (nullptr, "nuevo", "original", "set")
 */
static void cliHandleId(const char* subcmd) {
    if (!subcmd) { 
        Serial.printf("0x%05lX\n", (unsigned long)fanGetAddr()); 
        return; 
    }
    if (!strcasecmp(subcmd, "nuevo"))    { fanSetAddr(FAN_ID_NUEVO);    Serial.println(F("OK")); }
    else if (!strcasecmp(subcmd, "original")) { fanSetAddr(FAN_ID_ORIGINAL); Serial.println(F("OK")); }
    else if (!strcasecmp(subcmd, "set")) { /* handled by caller */ }
}

/**
 * Muestra estado actual: RF (ID, cellUs, driver), luz, ventilador, giro
 */
static void cliShowStatus() {
    Serial.printf("ID=0x%05lX cellUs=%lu drv=%s\n",
        (unsigned long)fanGetAddr(), 
        (unsigned long)fanTxParams().cellUs,
        fanDerivedReady() ? "OK" : "RAW");
    Serial.printf("luz=%s bri=%d ct=%d fan=%s vel=%d dir=%s\n",
        S.lightOn  ? "ON" : "OFF", S.bri, S.ctMireds,
        S.fanOn    ? "ON" : "OFF", S.fanSpd, S.summer ? "VER" : "INV");
}

/**
 * Procesa línea de consola: parsea comandos RF (rftest, rfsend, rfcell, rfid, rfpair, rfstatus)
 * @param line String completo de comando
 */
static void cliProcess(char* line) {
    char* cmd = strtok(line, " \t");
    if (!cmd) return;
    
    if (!strcasecmp(cmd, "rfhelp"))  { cliPrintHelp(); return; }
    if (!strcasecmp(cmd, "rftest"))  { cliTestRF(); return; }
    if (!strcasecmp(cmd, "rfsend"))  { char* n = strtok(nullptr, " \t"); if (n) rf(n); return; }
    if (!strcasecmp(cmd, "rfcell"))  { cliSetCell(strtok(nullptr, " \t")); return; }
    if (!strcasecmp(cmd, "rfstatus")) { cliShowStatus(); return; }
    
    if (!strcasecmp(cmd, "rfid")) {
        char* s = strtok(nullptr, " \t");
        if (!s) { Serial.printf("0x%05lX\n", (unsigned long)fanGetAddr()); }
        else if (!strcasecmp(s, "set")) {
            char* v = strtok(nullptr, " \t");
            if (v) fanSetAddr((uint32_t)strtoul(v, nullptr, 0) & 0x3FFFFu);
            Serial.println(F("OK"));
        } else {
            cliHandleId(s);
        }
        return;
    }
    
    if (!strcasecmp(cmd, "rfpair")) {
        char* v = strtok(nullptr, " \t");
        fanPairBruteForce(fanGetAddr(), v ? (uint32_t)atoi(v) : 350);
        return;
    }
}

// ============================================================
// Inicialización (Setup)
// ============================================================

/**
 * Inicializa sistema:
 * 1. Serial (115200 baud)
 * 2. Estado persistente desde NVS
 * 3. RF: CC1101, RMT, protocolo
 * 4. HomeSpan: WiFi, accesorios (luz, ventilador, giro, apagar todo)
 *
 * Orden: Setup — orden idéntico a v5 que conecta correctamente
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println(F("\n=== Bridge HomeKit v7 ==="));

    stLoad();

    if (!fanRfInit())  { Serial.println(F("[ERR] CC1101")); while (true) delay(1000); }
    if (!fanRmtInit()) { Serial.println(F("[ERR] RMT"));    while (true) delay(1000); }
    if (!fanDeriveProtocol()) Serial.println(F("[WARN] modo RAW"));

    fanLoadAddr();
    Serial.printf("[OK] ID=0x%05lX cellUs=%lu\n",
        (unsigned long)fanGetAddr(), (unsigned long)fanTxParams().cellUs);

    // [B1] WiFi después del hardware — igual que v5
    homeSpan.setWifiCredentials("DIGIFIBRA-CkKf", "GFt4xQyS7ZXT");
    homeSpan.setPairingCode("46637726");
    homeSpan.setHostNameSuffix("fanbridge");
    homeSpan.setSketchVersion("7.0.0");
    homeSpan.setLogLevel(1);
    homeSpan.begin(Category::Bridges, "Bridge Ventilador RF");

    // [1] Bridge raíz
    new SpanAccessory();
        new Service::AccessoryInformation();
            new Characteristic::Identify();
            new Characteristic::Name("Bridge RF");
            new Characteristic::Manufacturer("DIY");
            new Characteristic::Model("ESP32S3+CC1101");
            new Characteristic::SerialNumber("001");
            new Characteristic::FirmwareRevision("7.0");

    // [2] Luz
    new SpanAccessory();
        new Service::AccessoryInformation();
            new Characteristic::Identify();
            new Characteristic::Name("Luz Ventilador");
        new SvcLuz();

    // [3] Ventilador
    new SpanAccessory();
        new Service::AccessoryInformation();
            new Characteristic::Identify();
            new Characteristic::Name("Ventilador");
        new SvcFan();

    // [4] Giro
    new SpanAccessory();
        new Service::AccessoryInformation();
            new Characteristic::Identify();
            new Characteristic::Name("Giro Ventilador");
        new SvcGiro();

    // [5] Apagar Todo
    new SpanAccessory();
        new Service::AccessoryInformation();
            new Characteristic::Identify();
            new Characteristic::Name("Apagar Todo");
        new SvcAllOff();
}

// ============================================================
// I/O Serial — Procesamiento de entrada
// ============================================================
static char   gSerialBuf[128];      // Buffer de línea
static size_t gSerialBufLen = 0;    // Índice actual del buffer

/**
 * Lee caracteres de Serial, acumula en buffer, procesa línea completa
 * Delimiter: '\n' o '\r'
 */
static void processSerialInput() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (gSerialBufLen > 0) {
                gSerialBuf[gSerialBufLen] = '\0';
                gSerialBufLen = 0;
                cliProcess(gSerialBuf);
            }
        } else if (gSerialBufLen < sizeof(gSerialBuf) - 1) {
            gSerialBuf[gSerialBufLen++] = c;
        }
    }
}

// ============================================================
// Loop Principal
// ============================================================

/**
 * Loop infinito: poll HomeSpan (ejecuta servicios), procesa entrada serial
 */
void loop() {
    homeSpan.poll();          // HomeSpan: ejecuta servicios y callbacks
    processSerialInput();     // Consola: procesa comandos RF
}