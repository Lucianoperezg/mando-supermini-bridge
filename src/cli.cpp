#include "cli.h"

#include <Arduino.h>
#include "bridge_state.h"
#include "fan_protocol.h"

// Declaración externa de la función auxiliar rf() que estará implementada en bridge_services.cpp
bool rf(const char* cmd);

// Buffer de línea de comandos serial
static char   gSerialBuf[128];
static size_t gSerialBufLen = 0;

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

void cliInit() {
    gSerialBufLen = 0;
    memset(gSerialBuf, 0, sizeof(gSerialBuf));
}

void cliPoll() {
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
