#pragma once
// ============================================================
// fan_protocol.h  —  Bridge HomeKit, IDF 5.x / Arduino-ESP32 3.x
//
// Driver RMT nuevo: driver/rmt_tx.h + rmt_new_copy_encoder
// Resolución 1 MHz → 1 tick = 1 µs exacto (sin factor de escala)
//
// Pines (ESP32-S3 SuperMini, confirmados):
//   CSN=10  SCK=12  MOSI=11  MISO=13  GDO0=9
//
// IDs RF:
//   FAN_ID_ORIGINAL = 0x2BF6B
//   FAN_ID_NUEVO    = 0x0511E  ← emparejado, usar por defecto
// ============================================================

#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_random.h>
#include "driver/rmt_tx.h"

// ── Pines ────────────────────────────────────────────────────
#define FAN_PIN_CSN  10
#define FAN_PIN_SCK  12
#define FAN_PIN_MOSI 11
#define FAN_PIN_MISO 13
#define FAN_PIN_GDO0 9

// ── IDs ──────────────────────────────────────────────────────
static constexpr uint32_t FAN_ID_ORIGINAL = 0x2BF6Bu;
static constexpr uint32_t FAN_ID_NUEVO    = 0x0511Eu;

// ── Registros CC1101 ─────────────────────────────────────────
// Configuración: ASK/OOK @ 433.920 MHz, 38.4 kbps, async serial mode
#define CC1101_SRES   0x30
#define CC1101_SCAL   0x33
#define CC1101_STX    0x35
#define CC1101_SIDLE  0x36
#define CC1101_SFRX   0x3A

static const struct { uint8_t addr; uint8_t val; } kCC1101Regs[] = {
    // GPIO Configuration
    {0x00, 0x2E}, // IOCFG2: GDO2 high-Z
    {0x01, 0x2E}, // IOCFG1: GDO1 high-Z
    {0x02, 0x0D}, // IOCFG0: async serial TX data
    
    // Data Format
    {0x03, 0x47}, // FIFOTHR
    {0x06, 0xFF}, // PKTLEN
    {0x07, 0x04}, // PKTCTRL1
    {0x08, 0x32}, // PKTCTRL0: async serial mode (PKT_FORMAT=3)
    
    // Frequency Synthesizer
    {0x0B, 0x06}, // FSCTRL1
    {0x0C, 0x00}, // FSCTRL0
    {0x0D, 0x10}, // FREQ2  ┐
    {0x0E, 0xB1}, // FREQ1  ├ 433.920 MHz (xosc=26MHz)
    {0x0F, 0x3B}, // FREQ0  ┘
    
    // Modem Configuration
    {0x10, 0x8A}, // MDMCFG4: 38.4 kbps baud rate
    {0x11, 0x83}, // MDMCFG3
    {0x12, 0x30}, // MDMCFG2: ASK/OOK modulation
    {0x13, 0x22}, // MDMCFG1
    {0x14, 0xF8}, // MDMCFG0
    {0x15, 0x15}, // DEVIATN
    
    // Main Radio Control State Machine
    {0x16, 0x07}, // MCSM2
    {0x17, 0x30}, // MCSM1: idle after TX/RX
    {0x18, 0x18}, // MCSM0: autocal idle→TX
    
    // Frequency Offset Compensation
    {0x19, 0x16}, // FOCCFG
    
    // Bit Synchronization
    {0x1A, 0x6C}, // BSCFG
    
    // AGC Control
    {0x1B, 0x43}, // AGCCTRL2
    {0x1C, 0x40}, // AGCCTRL1
    {0x1D, 0x91}, // AGCCTRL0
    
    // Wake On Radio
    {0x1E, 0x87}, // WOREVT1
    {0x1F, 0x6B}, // WOREVT0
    {0x20, 0xF8}, // WORCTRL
    
    // Front End RX Configuration
    {0x21, 0x56}, // FREND1
    {0x22, 0x11}, // FREND0: PA_POWER=1 (OOK 2 levels)
    
    // Frequency Synthesis Calibration
    {0x23, 0xE9}, // FSCAL3
    {0x24, 0x2A}, // FSCAL2
    {0x25, 0x00}, // FSCAL1
    {0x26, 0x1F}, // FSCAL0
    
    // Various Test Settings
    {0x2C, 0x81}, // TEST2
    {0x2D, 0x35}, // TEST1
    {0x2E, 0x09}, // TEST0
};

// ── Parámetros TX (ajustables por consola) ───────────────────
struct FanTxParams {
    uint32_t cellUs      = 250;  // µs por celda (1 tick = 1 µs en IDF5)
    uint32_t gapUs       = 8800; // pausa inter-trama
    uint32_t preLowUs    = 1000; // LOW antes del burst
    uint32_t preStxUs    = 1000; // espera tras STX
    uint32_t postBurstUs = 3000; // espera antes de SIDLE
    uint8_t  paValue     = 0x50; // PATABLE[1]
};
static FanTxParams gFanTx;

// ── Comandos medidos (capturas URH verificadas) ───────────────
struct FanCmd {
    const char* name;
    const char* f0;   // frame0 (celdas raw)
    const char* fN;   // frameN (celdas raw)
    uint8_t     reps; // repeticiones
};

static const FanCmd kCmds[] = {
  {"ALL_OFF_ON",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101000100011101110100011101000111011101000111011101",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101000100011101110100011101000111011101000111011101",5},
  {"ALL_OFF_OFF",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101000100011101110100011101110111011101000100011101",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101000100011101110100011101110111011101000100011101",6},
  {"LIGHT_ON",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101000111010001000100011101000100011101110100011101",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101000111010001000100011101000100011101110100011101",7},
  {"LIGHT_OFF",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101000111010001000100010001110111011101000111010001",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101000111010001000100010001110111011101000111010001",7},
  {"SUMMER_DIR_A",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101000100011101000100011101110100011101000100011101",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101000100011101000100011101110100011101000100011101",6},
  {"WINTER_DIR_A",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101110100010001000111011101110100011101000111011101",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101110100010001000111011101110100011101000111011101",6},
  {"SPEED1_A",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101110100010001000100010001110100010001110111011101",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101110100010001000100010001110100010001110111011101",5},
  {"SPEED2_A",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101110100010001110100011101000100010001000100010001",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101110100010001110100011101000100010001000100010001",5},
  {"SPEED3_A",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101110111011101000100011101110111010001110100010001",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101110111011101000100011101110111010001110100010001",4},
  {"SPEED4_A",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101000111010001110100010001110100011101000111010001",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101000111010001110100010001110100011101000111010001",6},
  {"SPEED5_A",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101000111011101110111011101000111010001110111011101",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101000111011101110111011101000111010001110111011101",5},
  {"SPEED6_A",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101000111011101000100010001000100011101000111011101",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101000111011101000100010001000100011101000111011101",6},
  {"FAN_A",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101110100011101110100010001110111010001110100011101",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101110100011101110100010001110111010001110100011101",4},
  {"BRIGHT_PLUS_TAP",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101000100010001110111010001110100010001110111010001",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101000100010001110111010001110100010001110111010001",5},
  {"BRIGHT_MINUS_TAP",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101000100011101000111010001000111010001110111010001",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101000100011101000111010001000111010001110111010001",5},
  {"COLD_PLUS_TAP",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101000100011101110111011101000100010001000111010001",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101000100011101110111011101000100010001000111010001",4},
  {"WARM_PLUS_TAP",
   "1110100011101000111011101110111011101110100011101110100011101000"
   "111011101000111010001110111011101000100010001110100010001",
   "111011101110100011101000111011101110111011101110100011101110100011101000"
   "111011101000111010001110111011101000100010001110100010001",40},
};
static constexpr size_t kNumCmds = sizeof(kCmds)/sizeof(kCmds[0]);

// ── Protocolo derivado ───────────────────────────────────────
struct FanDerived { const char* name; uint8_t cmd8; uint8_t reps; };
static FanDerived   gDerived[kNumCmds];
static size_t       gDerivedN     = 0;
static bool         gDerivedReady = false;
static uint32_t     gAddrOrig     = 0;
static uint32_t     gAddrCur      = FAN_ID_NUEVO;

static Preferences  gAddrPrefs;

// ── RMT IDF5 ────────────────────────────────────────────────
static rmt_channel_handle_t gRmtCh  = nullptr;
static rmt_encoder_handle_t gRmtEnc = nullptr;
static rmt_symbol_word_t    gSyms[5000];
static size_t               gSymN   = 0;

// ── CC1101 SPI helpers ───────────────────────────────────────
static inline void csL(){ digitalWrite(FAN_PIN_CSN, LOW); }
static inline void csH(){ digitalWrite(FAN_PIN_CSN, HIGH); }

static bool waitMiso(uint32_t ms=300){
    uint32_t t=millis();
    while(digitalRead(FAN_PIN_MISO)==HIGH)
        if(millis()-t>ms) return false;
    return true;
}
static void wrReg(uint8_t a, uint8_t v){
    csL(); if(!waitMiso()){csH();return;}
    SPI.transfer(a); SPI.transfer(v); csH();
}
static uint8_t rdStatus(uint8_t a){
    csL(); if(!waitMiso()){csH();return 0xFF;}
    SPI.transfer(a|0xC0); uint8_t v=SPI.transfer(0); csH(); return v;
}
static void wrBurst(uint8_t a, const uint8_t* d, size_t n){
    csL(); if(!waitMiso()){csH();return;}
    SPI.transfer(a|0x40);
    for(size_t i=0;i<n;i++) SPI.transfer(d[i]);
    csH();
}
static void strobe(uint8_t c){
    csL(); if(!waitMiso()){csH();return;}
    SPI.transfer(c); csH();
}

// ── CC1101 init ──────────────────────────────────────────────
bool fanRfInit(){
    pinMode(FAN_PIN_CSN,  OUTPUT); digitalWrite(FAN_PIN_CSN, HIGH);
    pinMode(FAN_PIN_GDO0, OUTPUT); digitalWrite(FAN_PIN_GDO0, LOW);
    pinMode(FAN_PIN_MISO, INPUT);

    SPI.begin(FAN_PIN_SCK, FAN_PIN_MISO, FAN_PIN_MOSI, FAN_PIN_CSN);
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    SPI.setFrequency(1000000);

    // Reset sequence
    csH(); delayMicroseconds(5);
    csL(); delayMicroseconds(10);
    csH(); delayMicroseconds(40);
    csL(); if(!waitMiso()){csH();return false;}
    SPI.transfer(CC1101_SRES);
    if(!waitMiso()){csH();return false;}
    csH(); delay(20);

    uint8_t pn = rdStatus(0x30); // PARTNUM
    uint8_t vv = rdStatus(0x31); // VERSION
    if(pn!=0x00 || vv!=0x14){
        Serial.printf("[CC1101] ERR PARTNUM=0x%02X VERSION=0x%02X\n",pn,vv);
        return false;
    }

    for(auto& r : kCC1101Regs) wrReg(r.addr, r.val);

    // PATABLE: [0]=off [1]=PA level
    uint8_t pa[2] = {0x00, gFanTx.paValue};
    wrBurst(0x3E, pa, 2);

    strobe(CC1101_SIDLE); delay(5);
    strobe(CC1101_SCAL);  delay(50);

    Serial.printf("[CC1101] OK v=0x%02X\n", vv);
    return true;
}

// ── RMT init (IDF5) ─────────────────────────────────────────
bool fanRmtInit(){
    rmt_tx_channel_config_t ch{};
    ch.gpio_num          = (gpio_num_t)FAN_PIN_GDO0;
    ch.clk_src           = RMT_CLK_SRC_DEFAULT;
    ch.resolution_hz     = 1000000; // 1 tick = 1 µs
    ch.mem_block_symbols = 64;
    ch.trans_queue_depth = 4;
    ch.flags.invert_out  = false;
    ch.flags.with_dma    = false;
    if(rmt_new_tx_channel(&ch, &gRmtCh)!=ESP_OK) return false;

    rmt_copy_encoder_config_t ec{};
    if(rmt_new_copy_encoder(&ec, &gRmtEnc)!=ESP_OK) return false;

    if(rmt_enable(gRmtCh)!=ESP_OK) return false;

    Serial.println(F("[RMT] OK 1µs/tick"));
    return true;
}

// ── Buffer de símbolos ───────────────────────────────────────
static void symClear(){ memset(gSyms,0,sizeof(gSyms)); gSymN=0; }

static bool symAdd(bool hi, uint32_t us){
    if(!us) return true;
    if(gSymN >= sizeof(gSyms)/sizeof(gSyms[0])) return false;
    // Fragmentar si >32767 µs
    while(us > 32767){
        rmt_symbol_word_t& s=gSyms[gSymN];
        s.level0=hi; s.duration0=32767;
        s.level1=hi; s.duration1=0;
        // El campo duration1=0 cierra este símbolo,
        // pero level1 debe ser igual para que no cambie
        // Usamos dos símbolos consecutivos del mismo nivel
        s.duration1=1; // mínimo para cerrar el símbolo
        gSymN++;
        us-=32767;
        if(gSymN >= sizeof(gSyms)/sizeof(gSyms[0])) return false;
    }
    rmt_symbol_word_t& s=gSyms[gSymN];
    if(!s.duration0){
        s.level0=hi; s.duration0=(uint16_t)us;
        return true;
    }
    if(!s.duration1){
        s.level1=hi; s.duration1=(uint16_t)us;
        gSymN++;
        return true;
    }
    gSymN++;
    if(gSymN >= sizeof(gSyms)/sizeof(gSyms[0])) return false;
    gSyms[gSymN].level0=hi; gSyms[gSymN].duration0=(uint16_t)us;
    return true;
}

static bool symAddGap(uint32_t us){
    // Fragmentar en chunks de 30000 µs para no superar 32767
    const uint32_t chunk=30000;
    while(us>chunk){ if(!symAdd(false,chunk)) return false; us-=chunk; }
    return us ? symAdd(false,us) : true;
}

static bool symAddCells(const char* s, uint32_t cellUs){
    if(!s||!*s) return true;
    char cur=s[0]; uint32_t run=1;
    for(size_t i=1;s[i];i++){
        if(s[i]==cur){ run++; }
        else{
            if(!symAdd(cur=='1', run*cellUs)) return false;
            cur=s[i]; run=1;
        }
    }
    return symAdd(cur=='1', run*cellUs);
}

static bool symFinish(){
    // Cerrar símbolo pendiente
    if(gSymN < sizeof(gSyms)/sizeof(gSyms[0])){
        rmt_symbol_word_t& s=gSyms[gSymN];
        if(s.duration0 && !s.duration1){
            s.level1=0; s.duration1=1; gSymN++;
        }
    }
    // Símbolo de cierre (obligatorio en IDF5: duration0=0)
    if(gSymN < sizeof(gSyms)/sizeof(gSyms[0])){
        gSyms[gSymN]={0,0,0,0}; gSymN++;
    }
    return gSymN > 1;
}

// ── Construcción y envío del burst ───────────────────────────
static bool buildBurst(const char* f0, const char* fN, uint8_t reps){
    symClear();
    if(!symAddGap(gFanTx.preLowUs))                    return false;
    if(!symAddCells(f0, gFanTx.cellUs))                return false;
    for(int i=0;i<(int)reps-1;i++){
        if(!symAddGap(gFanTx.gapUs))                   return false;
        if(!symAddCells(fN, gFanTx.cellUs))            return false;
    }
    return symFinish();
}

static bool transmit(){
    strobe(CC1101_STX);
    delayMicroseconds(gFanTx.preStxUs);

    rmt_transmit_config_t cfg{}; cfg.loop_count=0;
    esp_err_t e = rmt_transmit(gRmtCh, gRmtEnc,
                               gSyms, gSymN*sizeof(rmt_symbol_word_t), &cfg);
    if(e!=ESP_OK){
        strobe(CC1101_SIDLE);
        Serial.printf("[RMT] transmit err 0x%X\n", e);
        return false;
    }
    rmt_tx_wait_all_done(gRmtCh, pdMS_TO_TICKS(3000));
    delayMicroseconds(gFanTx.postBurstUs);
    strobe(CC1101_SIDLE);
    return true;
}

// ── Protocolo: codificación/decodificación ───────────────────
static uint8_t chk4(uint8_t c){ return ((c>>4)^(c&0xF)^0x0D)&0xF; }

static String toBits(uint32_t v, int n){
    String s; s.reserve(n);
    for(int i=n-1;i>=0;i--) s+=(char)('0'+((v>>i)&1));
    return s;
}
static uint32_t fromBits(const String& b){
    uint32_t v=0;
    for(size_t i=0;i<b.length();i++){v<<=1;if(b[i]=='1')v|=1;}
    return v;
}
static String cellsToBits(const char* cells, bool isN){
    String s;
    for(const char* p=cells;*p;p++) if(*p=='0'||*p=='1') s+=*p;
    if(s.isEmpty()||s[s.length()-1]!='1') return "";
    s.remove(s.length()-1);
    size_t exp = isN ? 128 : 120;
    if(s.length()!=exp) return "";
    String bits;
    for(size_t i=0;i<s.length();i+=4){
        String c=s.substring(i,i+4);
        if(c=="1110") bits+='1';
        else if(c=="1000") bits+='0';
        else return "";
    }
    return bits;
}
static String makeFrame(const String& bits){
    String o; o.reserve(bits.length()*4+1);
    for(size_t i=0;i<bits.length();i++) o+=(bits[i]=='1')?"1110":"1000";
    o+='1'; return o;
}
static String makePayload(uint32_t addr, uint8_t cmd8){
    return toBits(addr&0x3FFFF,18)+toBits(cmd8,8)+toBits(chk4(cmd8),4);
}
static String makePayloadChk(uint32_t addr, uint8_t cmd8, uint8_t c4){
    return toBits(addr&0x3FFFF,18)+toBits(cmd8,8)+toBits(c4&0xF,4);
}

// ── Gestión de ID ────────────────────────────────────────────
void fanSetAddr(uint32_t a){
    gAddrCur=a&0x3FFFF;
    gAddrPrefs.begin("fanrf",false);
    gAddrPrefs.putUInt("addr18",gAddrCur);
    gAddrPrefs.end();
}
void fanLoadAddr(){
    gAddrPrefs.begin("fanrf",true);
    uint32_t s=gAddrPrefs.getUInt("addr18",FAN_ID_NUEVO);
    gAddrPrefs.end();
    gAddrCur = (s<=0x3FFFF) ? s : FAN_ID_NUEVO;
}
uint32_t fanGetAddr()        { return gAddrCur; }
uint32_t fanGetOriginalAddr(){ return gAddrOrig; }

// ── Derivar protocolo desde tramas medidas ───────────────────
bool fanDeriveProtocol(){
    gDerivedN=0; gAddrOrig=0;
    for(size_t i=0;i<kNumCmds;i++){
        String bits=cellsToBits(kCmds[i].f0,false);
        if(bits.length()!=30){
            Serial.printf("[PROTO] decode error: %s\n",kCmds[i].name);
            return false;
        }
        uint32_t addr=fromBits(bits.substring(0,18));
        uint8_t  c8  =(uint8_t)fromBits(bits.substring(18,26));
        uint8_t  c4  =(uint8_t)fromBits(bits.substring(26,30));
        if(chk4(c8)!=c4){
            Serial.printf("[PROTO] CHK4 mismatch: %s\n",kCmds[i].name);
            return false;
        }
        if(i==0) gAddrOrig=addr;
        else if(addr!=gAddrOrig){
            Serial.printf("[PROTO] ADDR mismatch: %s\n",kCmds[i].name);
            return false;
        }
        gDerived[gDerivedN++]={kCmds[i].name, c8, kCmds[i].reps};
    }
    gDerivedReady = gDerivedN>0;
    return gDerivedReady;
}
bool fanDerivedReady(){ return gDerivedReady; }

// ── API pública ──────────────────────────────────────────────
bool fanSend(const char* name){
    // Modo derivado: regenera tramas con el ID actual
    if(gDerivedReady){
        for(size_t i=0;i<gDerivedN;i++){
            if(!strcasecmp(gDerived[i].name, name)){
                String p  = makePayload(gAddrCur, gDerived[i].cmd8);
                String f0 = makeFrame(p);
                String fN = makeFrame(String("11")+p);
                if(!buildBurst(f0.c_str(), fN.c_str(), gDerived[i].reps))
                    return false;
                return transmit();
            }
        }
    }
    // Fallback: tramas raw originales
    for(size_t i=0;i<kNumCmds;i++){
        if(!strcasecmp(kCmds[i].name, name)){
            if(!buildBurst(kCmds[i].f0, kCmds[i].fN, kCmds[i].reps))
                return false;
            return transmit();
        }
    }
    Serial.printf("[RF] comando no encontrado: %s\n", name);
    return false;
}

bool fanSendManual(uint32_t addr, uint8_t cmd8, uint8_t c4, uint8_t reps){
    String p  = makePayloadChk(addr, cmd8, c4);
    String f0 = makeFrame(p);
    String fN = makeFrame(String("11")+p);
    if(!buildBurst(f0.c_str(), fN.c_str(), reps)) return false;
    return transmit();
}

static constexpr uint8_t PAIR_CMD8 = 0xB7;
static constexpr uint8_t PAIR_REPS = 24;

void fanPairBruteForce(uint32_t addr, uint32_t dwellMs=350){
    Serial.printf("[PAIRBF] addr=0x%05lX dwell=%lums\n",
                  (unsigned long)addr,(unsigned long)dwellMs);
    for(uint8_t c=0;c<16;c++){
        Serial.printf("[PAIRBF] CHK4=0x%X\n",c);
        uint32_t t0=millis();
        while(millis()-t0<dwellMs){
            fanSendManual(addr,PAIR_CMD8,c,PAIR_REPS);
            delay(40);
        }
    }
    Serial.println(F("[PAIRBF] fin"));
}

FanTxParams& fanTxParams(){ return gFanTx; }