#include "store.h"
#include <Arduino.h>
#include <Preferences.h>

static Preferences prefs;
static bool store_ok = false;

static void thread_key(int t, char* k) { snprintf(k, 8, "h%d", t); }

void store_init(void) {
    store_ok = prefs.begin("lora", false);      /* namespace "lora", read/write */
}

bool store_load_contacts(void* buf, size_t len) {
    if (!store_ok) return false;
    if (prefs.getBytesLength("ct") != len) return false;
    return prefs.getBytes("ct", buf, len) == len;
}

void store_save_contacts(const void* buf, size_t len) {
    if (store_ok) prefs.putBytes("ct", buf, len);
}

size_t store_load_thread(int t, void* buf, size_t maxlen) {
    if (!store_ok) return 0;
    char k[8]; thread_key(t, k);
    size_t n = prefs.getBytesLength(k);
    if (n == 0 || n > maxlen) return 0;
    return prefs.getBytes(k, buf, n);
}

void store_save_thread(int t, const void* buf, size_t len) {
    if (!store_ok) return;
    char k[8]; thread_key(t, k);
    if (len == 0) prefs.remove(k);
    else prefs.putBytes(k, buf, len);
}

void store_erase_thread(int t) {
    if (!store_ok) return;
    char k[8]; thread_key(t, k);
    prefs.remove(k);
}
