#include "lora_modem.h"
#include "config.h"
#include "app_data.h"
#include "store.h"
#include <Arduino.h>

static LoraEntry hist[LORA_HIST_MAX];
static int hist_n = 0;
static uint32_t next_seq = 0;
static uint32_t drop_count = 0;
static uint32_t result_rev = 0;

static char line_buf[304];
static uint16_t line_len = 0;

static bool modem_seen = false;
static int radio_state = -1;
static uint32_t last_modem_ms = 0;
static uint32_t rx_count = 0, tx_count = 0;
static char last_rssi[12] = "--";

static char info_freq[12] = "--";
static char info_sf[8]    = "--";
static char info_bw[12]   = "--";
static char info_pwr[8]   = "--";
static char info_fw[24]   = "--";

#define LORA_POLL_WINDOW_MS 10000
static bool poll_on = false;
static uint32_t poll_ms = 0;
static bool poll_active(void) { return poll_on && (millis() - poll_ms < LORA_POLL_WINDOW_MS); }

/* ---------- history ---------- */
static void hist_remove_at(int i) {
    memmove(&hist[i], &hist[i + 1], sizeof(LoraEntry) * (hist_n - i - 1));
    hist_n--;
    drop_count++;                        /* tells the chat its cached bubbles are stale */
}

/* Keeps only the newest LORA_PER_THREAD entries of each chat (RAM and flash hold the same set). */
static LoraEntry* hist_add(LoraKind kind, uint8_t thread, const char* text, const char* info) {
    int cnt = 0, first = -1;
    for (int i = 0; i < hist_n; i++)
        if (hist[i].thread == thread) { if (first < 0) first = i; cnt++; }
    if (cnt >= LORA_PER_THREAD) hist_remove_at(first);
    if (hist_n == LORA_HIST_MAX) hist_remove_at(0);      /* safety net, cannot normally happen */
    LoraEntry* e = &hist[hist_n++];
    e->kind = kind;
    e->thread = thread;
    snprintf(e->text, sizeof(e->text), "%s", text);
    snprintf(e->info, sizeof(e->info), "%s", info ? info : "");
    e->t_ms = millis();
    e->seq = next_seq++;
    e->lbl = NULL;
    return e;
}

/* ---------- flash persistence (one blob per chat, rewritten when that chat changes) ---------- */
struct StoredMsg { uint8_t kind; char text[64]; char info[24]; };   /* kind: 0 received, 1 sent, 2 failed */

static void persist_thread(int t) {
    StoredMsg buf[LORA_PER_THREAD];
    int n = 0;
    for (int i = 0; i < hist_n && n < LORA_PER_THREAD; i++) {
        if (hist[i].thread != t) continue;
        StoredMsg& s = buf[n++];
        memset(&s, 0, sizeof(s));
        s.kind = (hist[i].kind == LORA_RX_MSG) ? 0 : (hist[i].kind == LORA_TX_SENT) ? 1 : 2;   /* still-sending counts as failed */
        snprintf(s.text, sizeof(s.text), "%s", hist[i].text);
        snprintf(s.info, sizeof(s.info), "%s", hist[i].info);
    }
    store_save_thread(t, buf, n * sizeof(StoredMsg));
}

static void set_tx_result(LoraEntry* e, LoraKind kind) {
    e->kind = kind;
    result_rev++;
    persist_thread(e->thread);
}

static LoraEntry* oldest_sending(void) {
    for (int i = 0; i < hist_n; i++)
        if (hist[i].kind == LORA_TX_SENDING) return &hist[i];
    return NULL;
}

/* ---------- INFO:KEY=VAL,KEY=VAL ---------- */
static void copy_val(char* dst, size_t n, const char* v) {
    if (v[0]) snprintf(dst, n, "%s", v);
}

static void parse_info(char* kv) {
    char* save = NULL;
    for (char* tok = strtok_r(kv, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char* eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = tok;
        const char* val = eq + 1;
        if      (!strcmp(key, "FREQ")) copy_val(info_freq, sizeof(info_freq), val);
        else if (!strcmp(key, "SF"))   copy_val(info_sf,   sizeof(info_sf),   val);
        else if (!strcmp(key, "BW"))   copy_val(info_bw,   sizeof(info_bw),   val);
        else if (!strcmp(key, "PWR"))  copy_val(info_pwr,  sizeof(info_pwr),  val);
        else if (!strcmp(key, "FW"))   copy_val(info_fw,   sizeof(info_fw),   val);
    }
}

/* ---------- line handler ---------- */
static void handle_line(char* line) {
    while (*line == ' ') line++;
    size_t n = strlen(line);
    while (n && line[n - 1] == ' ') line[--n] = '\0';
    if (n == 0) return;

    modem_seen = true;
    last_modem_ms = millis();

    if (strncmp(line, "RX:", 3) == 0) {
        char* comma = strchr(line + 3, ',');
        if (comma && comma > line + 3) {
            *comma = '\0';
            const char* rssi = line + 3;
            const char* body = comma + 1;

            int thread = 0;                                  // untagged goes to thread 0
            const char* colon = strchr(body, ':');
            if (colon && colon - body <= MAX_TAG_LEN) {
                int t = app_thread_for_tag(body, (size_t)(colon - body));
                if (t > 0) { thread = t; body = colon + 1; if (*body == ' ') body++; }
            }

            char info[24];
            snprintf(info, sizeof(info), "RSSI %.11s dBm", rssi);
            snprintf(last_rssi, sizeof(last_rssi), "%s", rssi);
            rx_count++;
            hist_add(LORA_RX_MSG, (uint8_t)thread, body, info);
            persist_thread(thread);
#if LORA_SERIAL_LOG
            Serial.printf("[LoRa] RX %s: %s\n", info, body);
#endif
        }
    } else if (strncmp(line, "STATUS:", 7) == 0) {
        const char* st = line + 7;
        if (strcmp(st, "SENT") == 0) {
            LoraEntry* e = oldest_sending();
            if (e) set_tx_result(e, LORA_TX_SENT);
        } else if (strcmp(st, "FAIL") == 0) {
            LoraEntry* e = oldest_sending();
            if (e) set_tx_result(e, LORA_TX_FAILED);
        } else if (strcmp(st, "POLLFAIL") == 0) {
            poll_on = false;
        } else if (strcmp(st, "READY") == 0) {
            radio_state = 1;
        } else if (strncmp(st, "NORADIO", 7) == 0) {
            radio_state = 0;
        }
    } else if (strncmp(line, "LOG:", 4) == 0) {
        Serial.printf("[modem] %s\n", line);        /* diagnostics: USB monitor only */
    } else if (strncmp(line, "INFO:", 5) == 0) {
        Serial.printf("[modem] %s\n", line);
        parse_info(line + 5);                       /* modifies the buffer; print first */
    }
}

/* ---------- public API ---------- */
void lora_init(void) {
    Serial1.setRxBufferSize(1024);   /* must be before begin() */
    Serial1.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
}

void lora_poll(void) {
    int budget = 256;                /* cap per pass so the UI stays responsive */
    while (budget-- > 0 && Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\n') {
            line_buf[line_len] = '\0';
            handle_line(line_buf);
            line_len = 0;
        } else if (c >= 32 && c < 127) {
            if (line_len < 300) line_buf[line_len++] = c;
        }
    }

    uint32_t now = millis();
    for (int i = 0; i < hist_n; i++) {
        if (hist[i].kind == LORA_TX_SENDING && now - hist[i].t_ms > LORA_TX_TIMEOUT) {
            set_tx_result(&hist[i], LORA_TX_FAILED);
        }
    }
}

int lora_hist_count(void) { return hist_n; }
LoraEntry* lora_hist_at(int i) { return (i >= 0 && i < hist_n) ? &hist[i] : NULL; }

LoraEntry* lora_send(const char* txt, int thread) {
    char clean[201];
    snprintf(clean, sizeof(clean), "%s", txt);
    for (char* c = clean; *c; c++) if (*c < 32 || *c > 126) *c = ' ';

    LoraEntry* e = hist_add(LORA_TX_SENDING, (uint8_t)thread, clean, "");
    const char* tag = app_tag(thread);
    if (tag[0]) Serial1.printf("TX:%s:%s\n", tag, clean);
    else        Serial1.printf("TX:%s\n", clean);
#if LORA_SERIAL_LOG
    Serial.printf("[LoRa] TX queued (thread %d): %s\n", thread, clean);
#endif
    tx_count++;
    return e;
}

uint32_t lora_next_seq(void)   { return next_seq; }
uint32_t lora_drop_count(void) { return drop_count; }
uint32_t lora_result_rev(void) { return result_rev; }

const char* lora_modem_status_text(void) {
    if (!modem_seen) return "waiting for modem";
    if (millis() - last_modem_ms > MODEM_SILENT_MS) return "modem silent";
    if (radio_state == 0) return "no radio";
    if (poll_active()) return "checking messages";
    return "online";
}

int lora_radio_state(void)      { return radio_state; }
uint32_t lora_rx_count(void)    { return rx_count; }
uint32_t lora_tx_count(void)    { return tx_count; }
const char* lora_last_rssi(void){ return last_rssi; }

const char* lora_info_freq(void) { return info_freq; }
const char* lora_info_sf(void)   { return info_sf; }
const char* lora_info_bw(void)   { return info_bw; }
const char* lora_info_pwr(void)  { return info_pwr; }
const char* lora_info_fw(void)   { return info_fw; }

bool lora_request_poll(void) {
    if (poll_active()) return false;      // ignore taps while a poll is running
    Serial1.print("CMD:POLL\n");
    poll_on = true;
    poll_ms = millis();
    return true;
}

void lora_history_load(void) {
    for (int t = 0; t < NUM_MESSAGES; t++) {
        if (!contact_used(t)) continue;
        StoredMsg buf[LORA_PER_THREAD];
        int n = (int)(store_load_thread(t, buf, sizeof(buf)) / sizeof(StoredMsg));
        for (int k = 0; k < n; k++) {
            buf[k].text[sizeof(buf[k].text) - 1] = '\0';
            buf[k].info[sizeof(buf[k].info) - 1] = '\0';
            LoraKind kind = (buf[k].kind == 0) ? LORA_RX_MSG : (buf[k].kind == 1) ? LORA_TX_SENT : LORA_TX_FAILED;
            hist_add(kind, (uint8_t)t, buf[k].text, buf[k].info);
        }
        if (n > 0) {                                         /* inbox preview from the newest message */
            const StoredMsg& last = buf[n - 1];
            if (last.kind == 0) snprintf(messages[t].preview, sizeof(messages[t].preview), "%s", last.text);
            else                snprintf(messages[t].preview, sizeof(messages[t].preview), "You: %s", last.text);
        }
    }
}

void lora_purge_thread(int thread) {
    for (int i = hist_n - 1; i >= 0; i--)
        if (hist[i].thread == thread) hist_remove_at(i);
    store_erase_thread(thread);
}
