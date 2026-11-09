/* LoRaWAN settings, state machine, uplink payload and downlink callback.
   Only this file includes the Heltec/LoRaWAN library headers. */
/* heltec.h only exists in some Heltec library versions; LoRaWan_APP.h is the one that matters */
#if __has_include(<heltec.h>)
#include <heltec.h>
#endif
#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "app_config.h"
#include "node_state.h"
#include "display_link.h"
#include "lorawan_node.h"

/* LoRaWAN Configuration (credentials are in credentials.cpp) */
LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;
DeviceClass_t loraWanClass = CLASS_A;
uint32_t appTxDutyCycle = 15000;     // no longer used for sending (kept for the library)
bool overTheAirActivation = true;
bool loraWanAdr = true;
bool isTxConfirmed = false;
uint8_t appPort = 2;
uint8_t confirmedNbTrials = 4;

void lorawan_begin(void) {
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
}

void lorawan_update_idle_flag(void) {
  if (deviceState != DEVICE_STATE_SEND) wasIdle = false;
}

/* Uplink Payload: the message typed on the display */
void prepareTxFrame(uint8_t port)
{
  size_t n = pendingMsg.length();
  if (n > MAX_MSG_BYTES) n = MAX_MSG_BYTES;
#ifdef LORAWAN_APP_DATA_MAX_SIZE
  if (n > LORAWAN_APP_DATA_MAX_SIZE) n = LORAWAN_APP_DATA_MAX_SIZE;
#endif
  appDataSize = n;
  for (size_t i = 0; i < n; i++) {
    appData[i] = (uint8_t)pendingMsg[i];
  }
}

/* DOWNLINK CALLBACK */
void downLinkDataHandle(McpsIndication_t *mcpsIndication)
{
 if (!mcpsIndication->RxData)
 return;
 Serial.println();
 Serial.println("========== DOWNLINK RECEIVED ==========");
 Serial.printf("RX Window: %s\r\n",
 mcpsIndication->RxSlot ? "RXWIN2" : "RXWIN1");
 Serial.printf("FPort: %d\r\n",
 mcpsIndication->Port);
 Serial.printf("Payload Size: %d\r\n",
 mcpsIndication->BufferSize);
 Serial.print("HEX: ");
 for (uint8_t i = 0; i < mcpsIndication->BufferSize; i++)
 {
 Serial.printf("%02X ",
 mcpsIndication->Buffer[i]);
 }
 Serial.println();
 Serial.print("ASCII: ");
 for (uint8_t i = 0; i < mcpsIndication->BufferSize; i++)
 {
 uint8_t c = mcpsIndication->Buffer[i];
 if (c >= 32 && c <= 126)
 {
 Serial.print((char)c);
 }
 else
 {
 Serial.print('.');
 }
 }
 Serial.println();
 Serial.println("======================================");

 /* forward application downlinks to the display */
 if (mcpsIndication->Port != 0) {
   String text = "";
   for (uint8_t i = 0; i < mcpsIndication->BufferSize; i++) {
     uint8_t c = mcpsIndication->Buffer[i];
     text += (c >= 32 && c <= 126) ? (char)c : '?';
   }
   downlinkCount++;
   sendLine("RX:" + String((int)mcpsIndication->Rssi) + "," + text);
 }
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
 LoRaWAN.init(loraWanClass, loraWanRegion);
 LoRaWAN.setDefaultDR(3);
 deviceState = DEVICE_STATE_JOIN;
 break;
 }
 case DEVICE_STATE_JOIN:
 {
 joined = false;
 LoRaWAN.join();
 break;
 }
 case DEVICE_STATE_SEND:
 {
 joined = true;                        // the library only gets here after a join
 if (txPending) {
   wasIdle = false;
   prepareTxFrame(appPort);
   LoRaWAN.send();
   uplinkCount++;
   logLine("UL#" + String(uplinkCount) + ": " + pendingMsg);
   sendLine("STATUS:SENT");            // handed to the LoRaWAN stack
   txPending = false;
   pendingMsg = "";
   deviceState = DEVICE_STATE_CYCLE;
 } else if (pollPending) {
   wasIdle = false;
   pollPending = false;
   appPort = POLL_PORT;               // tiny uplink, only to open the receive windows
   appDataSize = 1;
   appData[0] = 0x00;
   LoRaWAN.send();
   appPort = 2;                       // back to the message port
   uplinkCount++;
   Serial.println("POLL uplink (collecting downlinks)");
   deviceState = DEVICE_STATE_CYCLE;
 } else {
   if (!wasIdle) {                     // just became idle: tell the display now
     wasIdle = true;
     display_link_heartbeat_now();
   }
   delay(2);                           // idle and awake: UART keeps working
 }
 break;
 }
 case DEVICE_STATE_CYCLE:
 {
 txDutyCycleTime = SETTLE_MS;          // let the TX and RX windows finish
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
