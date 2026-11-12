#pragma once

void lorawan_begin(void);               /* Mcu.begin */
void lorawan_update_idle_flag(void);    /* wasIdle = false unless in the SEND state */
void lorawan_step(void);                /* one pass of the LoRaWAN state machine (Class C) */
char lorawan_class_char(void);          /* 'A' or 'C' */
const char* lorawan_eui_text(void);     /* DevEUI as 16 hex characters */
