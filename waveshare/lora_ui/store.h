#pragma once
#include <stddef.h>

/* Flash storage (ESP32 NVS through the Preferences library that ships with the ESP32 core).
   Keys: "ct" = contact table, "h0".."h8" = the last messages of each chat. */
void   store_init(void);
bool   store_load_contacts(void* buf, size_t len);              /* false if missing or size differs */
void   store_save_contacts(const void* buf, size_t len);
size_t store_load_thread(int thread, void* buf, size_t maxlen); /* bytes read, 0 if none */
void   store_save_thread(int thread, const void* buf, size_t len);
void   store_erase_thread(int thread);
