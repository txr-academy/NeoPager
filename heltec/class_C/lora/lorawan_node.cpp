/* LoRaWAN settings, state machine, uplink payload and downlink callback  --  CLASS C.
   Only this file includes the Heltec/LoRaWAN library headers.

   Class C in one paragraph: after every uplink the stack opens RX1/RX2 as usual, then keeps the
   radio listening on the RX2 channel until the next uplink. The network server can therefore push
   a downlink at any moment. Two consequences for this firmware:
     1. The device must send ONE uplink after each join (the "hello") so the server starts
        scheduling Class C downlinks for the session.
     2. While idle we must keep servicing the radio, otherwise a downlink that arrives in RX2
        is never processed. (Class A only needed this right after an uplink.) */
/* heltec.h only exists in some Heltec library versions; LoRaWan_APP.h is the one that matters */
#if __has_include(<heltec.h>)
#include <heltec.h>
#endif
#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <string.h>
#include <stdlib.h>
#include "app_config.h"
#include "node_state.h"
#include "display_link.h"
#include "lorawan_node.h"

/* credentials.cpp defines these; re-declared so the EUI can be shown on the display */
extern uint8_t devEui[];
extern uint8_t appEui[];

/* LoRaWAN Configuration (credentials are in credentials.cpp) */
LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;
DeviceClass_t loraWanClass = CLASS_C;        // <<< was CLASS_A
uint32_t appTxDutyCycle = 15000;             // not used for sending (kept for the library)
bool overTheAirActivation = true;
bool loraWanAdr = true;
bool isTxConfirmed = false;                  // keep false: confirmed uplinks make the gateway transmit an ACK
uint8_t appPort = MSG_PORT;
uint8_t confirmedNbTrials = 4;

/* ---------- local state ---------- */
static TxItem cur;                           /* message being sent */
static bool helloSent = false;
static bool helloRepeatPending = false;
static unsigned long helloRepeatAt = 0;
static unsigned long lastUplinkMs = 0;
static char euiText[17] = "";

char lorawan_class_char(void) { return loraWanClass == CLASS_C ? 'C' : 'A'; }
const char* lorawan_eui_text(void) { return euiText; }

static void build_eui_text(void) {
  for (int i = 0; i < 8; i++) snprintf(euiText + i * 2, 3, "%02X", devEui[i]);
}

void lorawan_begin(void) {
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
}

void lorawan_update_idle_flag(void) {
  if (deviceState != DEVICE_STATE_SEND) wasIdle = false;
}

/* Keep the receiver serviced while idle (Class C). See SERVICE_VIA_SLEEP in app_config.h. */
static void service_radio(void) {
#if SERVICE_VIA_SLEEP
  LoRaWAN.sleep(loraWanClass);
#else
  Radio.IrqProcess();
#endif
  delay(1);                                  /* yield; adds at most 1 ms latency */
}

/* Uplink payload: the message typed on the display (port MSG_PORT) or a 1-byte hello (HELLO_PORT) */
void prepareTxFrame(uint8_t port)
{
  if (port == HELLO_PORT) {
    appDataSize = 1;
    appData[0] = 0x01;
    return;
  }
  size_t n = strlen(cur.text);
  if (n > MAX_MSG_BYTES) n = MAX_MSG_BYTES;
#ifdef LORAWAN_APP_DATA_MAX_SIZE
  if (n > LORAWAN_APP_DATA_MAX_SIZE) n = LORAWAN_APP_DATA_MAX_SIZE;
#endif
  appDataSize = n;
  for (size_t i = 0; i < n; i++) appData[i] = (uint8_t)cur.text[i];
}

static void send_on_port(uint8_t port) {
  appPort = port;
  prepareTxFrame(port);
  LoRaWAN.send();
  appPort = MSG_PORT;
  uplinkCount++;
  lastUplinkMs = millis();
}

/* DOWNLINK CALLBACK  (Class C: this can fire at any time, also between our uplinks) */
void downLinkDataHandle(McpsIndication_t *mcpsIndication)
{
  if (!mcpsIndication->RxData) return;

  uint8_t len = mcpsIndication->BufferSize;
  char text[96];
  size_t n = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
  for (size_t i = 0; i < n; i++) {
    uint8_t c = mcpsIndication->Buffer[i];
    text[i] = (c >= 32 && c <= 126) ? (char)c : '?';
  }
  text[n] = '\0';

  /* one compact log line (slot 0/1 = RX1/RX2, 2 = Class C continuous RX) */
  Serial.printf("[DL] slot=%d port=%d len=%d rssi=%d: %s\r\n",
                (int)mcpsIndication->RxSlot, (int)mcpsIndication->Port, (int)len,
                (int)mcpsIndication->Rssi, text);

  if (mcpsIndication->Port == 0) return;     /* MAC commands only */

  downlinkCount++;
  lastRssi = (int)mcpsIndication->Rssi;

  char out[140];
  if (strncmp(text, ACK_PREFIX, strlen(ACK_PREFIX)) == 0) {
    /* delivery receipt from the relay: "#A:<crc8 hex>" -> STATUS:ACK:<id> (never shown as a message) */
    const char* hex = text + strlen(ACK_PREFIX);
    char* endp = NULL;
    long crc = strtol(hex, &endp, 16);
    uint16_t id = 0;
    if (endp != hex && crc >= 0 && crc <= 255 && ack_match((uint8_t)crc, &id)) {
      snprintf(out, sizeof(out), "STATUS:ACK:%u", (unsigned)id);
      sendLineDeferred(out);
    }
    return;
  }
  snprintf(out, sizeof(out), "RX:%d,%s", (int)mcpsIndication->Rssi, text);
  sendLineDeferred(out);                     /* flushed from loop(): the callback never blocks on the UART */
}

void lorawan_step(void)
{
  switch (deviceState)
  {
    case DEVICE_STATE_INIT:
    {
#if (LORAWAN_DEVEUI_AUTO)
      LoRaWAN.generateDeveuiByChipID();
#endif
      build_eui_text();
      Serial.printf("DevEUI: %s  (register this in the network server, class C)\r\n", euiText);
      LoRaWAN.init(loraWanClass, loraWanRegion);
      LoRaWAN.setDefaultDR(3);
      deviceState = DEVICE_STATE_JOIN;
      break;
    }
    case DEVICE_STATE_JOIN:
    {
      joined = false;
      helloSent = false;                     /* a new session needs a new hello */
      LoRaWAN.join();
      break;
    }
    case DEVICE_STATE_SEND:
    {
      joined = true;                         // the library only gets here after a join

      /* 1) hello after every join: tells the server the device is up so it can schedule Class C downlinks */
      if (!helloSent) {
        helloSent = true;
        wasIdle = false;
        send_on_port(HELLO_PORT);
        logf("HELLO uplink (Class C session start)");
        if (HELLO_REPEAT_MS) { helloRepeatPending = true; helloRepeatAt = millis() + HELLO_REPEAT_MS; }
        display_link_heartbeat_now();
        deviceState = DEVICE_STATE_CYCLE;
        break;
      }

      /* 2) next queued chat message */
      if (txq_pop(&cur)) {
        wasIdle = false;
        send_on_port(MSG_PORT);
        if (cur.id) ack_record(cur.id, cur.text);   // so the relay's "#A:<crc>" can be mapped back to this id
        logf("UL#%lu id=%u: %s", uplinkCount, (unsigned)cur.id, cur.text);
        send_status("SENT", cur.id);         // handed to the LoRaWAN stack
        deviceState = DEVICE_STATE_CYCLE;
        break;
      }

      /* 3) one-off repeat hello, then a slow keep-alive */
      if ((helloRepeatPending && (long)(millis() - helloRepeatAt) >= 0) ||
          (KEEPALIVE_MS && millis() - lastUplinkMs >= KEEPALIVE_MS)) {
        helloRepeatPending = false;
        wasIdle = false;
        send_on_port(HELLO_PORT);
        Serial.println("keep-alive uplink");
        deviceState = DEVICE_STATE_CYCLE;
        break;
      }

      /* 4) idle: listening (Class C) */
      if (!wasIdle) {                        // just became idle: tell the display now
        wasIdle = true;
        display_link_heartbeat_now();
      }
      service_radio();
      break;
    }
    case DEVICE_STATE_CYCLE:
    {
      txDutyCycleTime = TX_GUARD_MS;         // TX + RX1 window, then back to idle (RX2 stays open)
      LoRaWAN.cycle(txDutyCycleTime);
      deviceState = DEVICE_STATE_SLEEP;
      break;
    }
    case DEVICE_STATE_SLEEP:
    {
      LoRaWAN.sleep(loraWanClass);
      break;
    }
    default:
    {
      deviceState = DEVICE_STATE_INIT;
      break;
    }
  }
}
