#include "bridge_state.h"

#include <Preferences.h>
#include <nvs.h>

static Preferences gStatePrefs;

static bool bridgeStateNamespaceExists() {
    nvs_opaque_iterator_t *it = nullptr;
    if (nvs_entry_find("nvs", "bridge", NVS_TYPE_ANY, &it) != ESP_OK || !it) {
        return false;
    }
    nvs_release_iterator(it);
    return true;
}

BridgeState S;

void stLoad() {
    if (bridgeStateNamespaceExists()) {
        gStatePrefs.begin("bridge", true);
        S.lightOn = gStatePrefs.getBool("lon", false);
        S.bri = constrain(gStatePrefs.getInt("bri", 50), 0, 100);
        S.ctMireds = constrain(gStatePrefs.getInt("ct", 250), 153, 370);
        S.fanOn = gStatePrefs.getBool("fon", false);
        S.fanSpd = constrain(gStatePrefs.getInt("fspd", 3), 1, 6);
        S.summer = gStatePrefs.getBool("sum", true);
        gStatePrefs.end();
    } else {
        S.lightOn = false;
        S.bri = 50;
        S.ctMireds = 250;
        S.fanOn = false;
        S.fanSpd = 3;
        S.summer = true;
    }

    Serial.printf("[ST] luz=%s bri=%d ct=%d fan=%s vel=%d dir=%s\n",
        S.lightOn ? "ON" : "OFF",
        S.bri,
        S.ctMireds,
        S.fanOn ? "ON" : "OFF",
        S.fanSpd,
        S.summer ? "VER" : "INV");
}

void stSave() {
    gStatePrefs.begin("bridge", false);
    gStatePrefs.putBool("lon", S.lightOn);
    gStatePrefs.putInt("bri", constrain(S.bri, 0, 100));
    gStatePrefs.putInt("ct", constrain(S.ctMireds, 153, 370));
    gStatePrefs.putBool("fon", S.fanOn);
    gStatePrefs.putInt("fspd", constrain(S.fanSpd, 1, 6));
    gStatePrefs.putBool("sum", S.summer);
    gStatePrefs.end();
}
