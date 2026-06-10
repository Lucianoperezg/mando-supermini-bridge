// ============================================================
// homekit_bridge/src/main.cpp  v7 (Refactorizado)
// ESP32-S3 + CC1101 — Bridge HomeKit
// Arduino-ESP32 3.x / IDF 5.x + HomeSpan 2.1.x
//
// Accesorios:
//   [2] Luz Ventilador  — on/off + brillo + temperatura color
//   [3] Ventilador      — on/off + velocidad 1-6
//   [4] Giro            — switch ON=Verano / OFF=Invierno
//   [5] Apagar Todo     — switch momentáneo
//   [6] Temporizador 2H — switch momentáneo (RF: 0x4F checksum 0x06)
//   [7] Temporizador 4H — switch momentáneo (RF: 0xC8 checksum 0x09)
// ============================================================

#include <Arduino.h>
#include "HomeSpan.h"
#include "bridge_state.h"
#include "fan_protocol.h"
#include "secrets.h"
#include "bridge_services.h"
#include "cli.h"
// Para control fino del driver Wi-Fi
#include <WiFi.h>
#include <esp_wifi.h>

// ============================================================
// Inicialización (Setup)
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println(F("\n=== Bridge HomeKit v7 (Refactorizado) ==="));

    // Carga de estado desde NVS
    stLoad();

    // Inicialización de la consola interactiva
    cliInit();

    // Limpieza del estado Wi‑Fi para evitar conflictos entre NVS y el driver
    WiFi.disconnect(true, true); // borra caché/credenciales en driver
    WiFi.mode(WIFI_OFF);
    delay(300); // pequeño respiro al hardware

    // Configuración y arranque de HomeSpan (WiFi gestionado por HomeSpan)
    homeSpan.setWifiCredentials(WIFI_SSID, WIFI_PASSWORD);
    homeSpan.setPairingCode(HOMEKIT_PAIRING_CODE);
    homeSpan.setHostNameSuffix("fanbridge");
    homeSpan.setSketchVersion("7.0.0");
    homeSpan.setLogLevel(1);
    homeSpan.begin(Category::Bridges, "Bridge Ventilador RF");

    // Esperar brevemente a que el driver intente conectar (con timeout)
    unsigned long t0 = millis();
    const unsigned long WIFI_WAIT_MS = 15000; // esperar hasta 15s
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < WIFI_WAIT_MS) {
        homeSpan.poll();
        delay(100);
    }

    // Desactivar Wi‑Fi Power Save para evitar latencias intermitentes
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Inicialización del hardware RF (después de estabilizar la red Wi‑Fi)
    if (!fanRfInit())  { Serial.println(F("[ERR] CC1101")); while (true) delay(1000); }
    if (!fanRmtInit()) { Serial.println(F("[ERR] RMT"));    while (true) delay(1000); }
    if (!fanDeriveProtocol()) Serial.println(F("[WARN] modo RAW"));

    fanLoadAddr();
    Serial.printf("[OK] ID=0x%05lX cellUs=%lu\n",
        (unsigned long)fanGetAddr(), (unsigned long)fanTxParams().cellUs);

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

    // [6] Temporizador 2H
    new SpanAccessory();
        new Service::AccessoryInformation();
            new Characteristic::Identify();
            new Characteristic::Name("Temporizador 2H");
        new SvcTimer(0x4F, 0x06);

    // [7] Temporizador 4H
    new SpanAccessory();
        new Service::AccessoryInformation();
            new Characteristic::Identify();
            new Characteristic::Name("Temporizador 4H");
        new SvcTimer(0xC8, 0x09);
}

// ============================================================
// Loop Principal
// ============================================================
void loop() {
    homeSpan.poll();      // HomeSpan: ejecuta servicios y callbacks
    cliPoll();            // Consola: procesa comandos RF interactivos
}
