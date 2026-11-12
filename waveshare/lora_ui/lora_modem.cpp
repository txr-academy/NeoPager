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
static uint16_t next_id = 1;

static char line_buf[304];
static uint16_t line_len = 0;

enum { LS_UNKNOWN = 0, LS_JOINING, LS_LISTENING, LS_BUSY };

static bool modem_seen = false;
static int radio_state = -1;
static uint8_t link_state = LS_UNKNOWN;
static uint32_t last_modem_ms = 0;
static uint32_t rx_count = 0, tx_count = 0;
static uint32_t last_rx_ms = 0;
static bool rx_ever = false;
static char last_rssi[12] = "--";

static char info_class[4]  = "--";
static char info_net[16]   = "--";
static char info_region[12]= "--";
static char info_rx2[28]   = "--";
static char info_dr[6]     = "--";
static char info_join[6]   = "--";
static char info_ul[12]    = "--";
static char info_dl[12]    = "--";
static char info_eui[20]   = "--";
static char info_fw[24]    = "--";
static int  modem_proto    = LORA_PROTO_DEFAULT;
#if LORA_ASSUME_CLASS_C
static char modem_class = 'C';
#else
static char modem_class = 'A';
#endif

/* Class A fallback: poll uplink (only used when the modem reports CLASS=A) */
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
    e->id = 0;
    e->lbl = NULL;
    return e;
}

/* ---------- flash persistence (one blob per chat, rewritten when that chat changes) ---------- */
/* kind: 0 received, 1 sent, 2 failed, 3 delivered. Messages still SENDING/QUEUED at power-off are stored as failed. */
struct StoredMsg { uint8_t kind; char text[64]; char info[24]; };

static void persist_thread(int t) {
    StoredMsg buf[LORA_PER_THREAD];
    int n = 0;
    for (int i = 0; i < hist_n && n < LORA_PER_THREAD; i++) {
        if (hist[i].thread != t) continue;
        StoredMsg& s = buf[n++];
        memset(&s, 0, sizeof(s));
        switch (hist[i].kind) {
            case LORA_RX_MSG:       s.kind = 0; break;
            case LORA_TX_SENT:      s.kind = 1; break;
            case LORA_TX_DELIVERED: s.kind = 3; break;
            default:                s.kind = 2; break;
        }
        snprintf(s.text, sizeof(s.text), "%s", hist[i].text);
        snprintf(s.info, sizeof(s.info), "%s", hist[i].info);
    }
    store_save_thread(t, buf, n * sizeof(StoredMsg));
}

/* Moves a TX entry to a new state. Flash is rewritten only for states worth keeping. */
static void set_tx_state(LoraEntry* e, LoraKind kind) {
    if (e->kind == kind) return;
    e->kind = kind;
    result_rev++;
    if (kind == LORA_TX_SENT || kind == LORA_TX_DELIVERED || kind == LORA_TX_FAILED) persist_thread(e->thread);
}

static bool is_pending(LoraKind k) { return k == LORA_TX_SENDING || k == LORA_TX_QUEUED; }

static LoraEntry* find_tx_by_id(uint16_t id) {
    for (int i = hist_n - 1; i >= 0; i--)               /* newest first: ids wrap after 65535 */
        if (hist[i].kind != LORA_RX_MSG && hist[i].id == id) return &hist[i];
    return NULL;
}

static LoraEntry* oldest_pending(void) {                /* only for modems that answer without an id (protocol 1) */
    for (int i = 0; i < hist_n; i++)
        if (is_pending(hist[i].kind)) return &hist[i];
    return NULL;
}

static uint16_t alloc_id(void) {
    uint16_t id = next_id++;
    if (next_id == 0) next_id = 1;
    return id;
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
        if      (!strcmp(key, "PROTO")) { int p = atoi(val); if (p >= 1) modem_proto = p; }
        else if (!strcmp(key, "CLASS")) { copy_val(info_class, sizeof(info_class), val); if (val[0] == 'A' || val[0] == 'C') modem_class = val[0]; }
        else if (!strcmp(key, "NET"))   copy_val(info_net,    sizeof(info_net),    val);
        else if (!strcmp(key, "REG"))   copy_val(info_region, sizeof(info_region), val);
        else if (!strcmp(key, "RX2"))   copy_val(info_rx2,    sizeof(info_rx2),    val);
        else if (!strcmp(key, "DR"))    copy_val(info_dr,     sizeof(info_dr),     val);
        else if (!strcmp(key, "JOIN"))  snprintf(info_join, sizeof(info_join), "%s", val[0] == '1' ? "yes" : "no");
        else if (!strcmp(key, "UL"))    copy_val(info_ul,     sizeof(info_ul),     val);
        else if (!strcmp(key, "DL"))    copy_val(info_dl,     sizeof(info_dl),     val);
        else if (!strcmp(key, "EUI"))   copy_val(info_eui,    sizeof(info_eui),    val);
        else if (!strcmp(key, "FW"))    copy_val(info_fw,     sizeof(info_fw),     val);
    }
}

/* ---------- STATUS:<WORD>[:<id>] ---------- */
static void handle_status(const char* st) {
    char word[12];
    const char* colon = strchr(st, ':');
    size_t wl = colon ? (size_t)(colon - st) : strlen(st);
    if (wl >= sizeof(word)) wl = sizeof(word) - 1;
    memcpy(word, st, wl);
    word[wl] = '\0';
    uint16_t id = 0;
    if (colon) { int v = atoi(colon + 1); if (v > 0 && v <= 65535) id = (uint16_t)v; }

    if (!strcmp(word, "QUEUED")) {
        LoraEntry* e = id ? find_tx_by_id(id) : NULL;
        if (e && e->kind == LORA_TX_SENDING) {
            e->t_ms = millis();                         /* the SENT timeout starts now */
            set_tx_state(e, LORA_TX_QUEUED);
        }
    } else if (!strcmp(word, "SENT")) {
        LoraEntry* e = id ? find_tx_by_id(id) : oldest_pending();
        if (e && is_pending(e->kind)) set_tx_state(e, LORA_TX_SENT);
    } else if (!strcmp(word, "FAIL")) {
        LoraEntry* e = id ? find_tx_by_id(id) : oldest_pending();
        if (e && is_pending(e->kind)) set_tx_state(e, LORA_TX_FAILED);
    } else if (!strcmp(word, "ACK")) {
#if LORA_EXPECT_ACKS
        LoraEntry* e = id ? find_tx_by_id(id) : NULL;
        if (e && (is_pending(e->kind) || e->kind == LORA_TX_SENT)) set_tx_state(e, LORA_TX_DELIVERED);
#endif
    } else if (!strcmp(word, "JOINING")) {
        link_state = LS_JOINING; if (radio_state < 0) radio_state = 1;
    } else if (!strcmp(word, "LISTENING") || !strcmp(word, "READY")) {
        link_state = LS_LISTENING; radio_state = 1;
    } else if (!strcmp(word, "BUSY")) {
        link_state = LS_BUSY; radio_state = 1;
    } else if (!strcmp(word, "NORADIO")) {
        radio_state = 0;
    } else if (!strcmp(word, "POLLFAIL")) {
        poll_on = false;
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
            rx_ever = true;
            last_rx_ms = millis();
            hist_add(LORA_RX_MSG, (uint8_t)thread, body, info);
            persist_thread(thread);
#if LORA_SERIAL_LOG
            Serial.printf("[LoRa] RX %s: %s\n", info, body);
#endif
        }
    } else if (strncmp(line, "STATUS:", 7) == 0) {
        handle_status(line + 7);
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
    Serial1.print("CMD:INFO\n");      /* a modem that is already running answers now; an older one ignores it */
}

void lora_request_info(void) { Serial1.print("CMD:INFO\n"); }

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
        LoraEntry& e = hist[i];
        if (e.kind == LORA_TX_SENDING && now - e.t_ms > LORA_QUEUE_TIMEOUT)      set_tx_state(&e, LORA_TX_FAILED);   /* modem never answered */
        else if (e.kind == LORA_TX_QUEUED && now - e.t_ms > LORA_TX_TIMEOUT)     set_tx_state(&e, LORA_TX_FAILED);   /* accepted, never sent */
    }
}

int lora_hist_count(void) { return hist_n; }
LoraEntry* lora_hist_at(int i) { return (i >= 0 && i < hist_n) ? &hist[i] : NULL; }

LoraEntry* lora_find_seq(uint32_t seq) {
    for (int i = hist_n - 1; i >= 0; i--)
        if (hist[i].seq == seq) return &hist[i];
    return NULL;
}

static void write_tx_line(LoraEntry* e) {
    const char* tag = app_tag(e->thread);
    if (modem_proto >= 2) {
        if (tag[0]) Serial1.printf("TX2:%u:%s:%s\n", (unsigned)e->id, tag, e->text);
        else        Serial1.printf("TX2:%u:%s\n", (unsigned)e->id, e->text);
    } else {
        if (tag[0]) Serial1.printf("TX:%s:%s\n", tag, e->text);
        else        Serial1.printf("TX:%s\n", e->text);
    }
}

LoraEntry* lora_send(const char* txt, int thread) {
    char clean[201];
    snprintf(clean, sizeof(clean), "%s", txt);
    for (char* c = clean; *c; c++) if (*c < 32 || *c > 126) *c = ' ';

    LoraEntry* e = hist_add(LORA_TX_SENDING, (uint8_t)thread, clean, "");
    e->id = alloc_id();
    write_tx_line(e);
#if LORA_SERIAL_LOG
    Serial.printf("[LoRa] TX id=%u (thread %d): %s\n", (unsigned)e->id, thread, clean);
#endif
    tx_count++;
    return e;
}

bool lora_resend(LoraEntry* e) {
    if (!e || e->kind != LORA_TX_FAILED) return false;
    e->kind = LORA_TX_SENDING;
    e->id = alloc_id();
    e->t_ms = millis();
    result_rev++;
    write_tx_line(e);
#if LORA_SERIAL_LOG
    Serial.printf("[LoRa] TX retry id=%u (thread %d): %s\n", (unsigned)e->id, (int)e->thread, e->text);
#endif
    tx_count++;
    return true;
}

uint32_t lora_next_seq(void)   { return next_seq; }
uint32_t lora_drop_count(void) { return drop_count; }
uint32_t lora_result_rev(void) { return result_rev; }

const char* lora_modem_status_text(void) {
    if (!modem_seen) return "waiting for modem";
    if (millis() - last_modem_ms > MODEM_SILENT_MS) return "modem silent";
    if (radio_state == 0) return "no radio";
    if (poll_active()) return "checking messages";
    switch (link_state) {
        case LS_JOINING:   return "joining network";
        case LS_BUSY:      return "busy";
        case LS_LISTENING: return modem_class == 'C' ? "listening" : "online";
        default:           return "online";
    }
}

int lora_link_level(void) {
    if (!modem_seen) return 0;
    if (millis() - last_modem_ms > MODEM_SILENT_MS || radio_state == 0) return 3;
    if (poll_active() || link_state == LS_JOINING || link_state == LS_BUSY) return 2;
    if (link_state == LS_LISTENING) return 1;
    return 0;
}

int lora_radio_state(void)      { return radio_state; }
bool lora_class_is_c(void)      { return modem_class == 'C'; }
bool lora_poll_supported(void)  { return modem_class != 'C'; }
uint32_t lora_rx_count(void)    { return rx_count; }
uint32_t lora_tx_count(void)    { return tx_count; }
const char* lora_last_rssi(void){ return last_rssi; }
int32_t lora_last_rx_age_s(void){ return rx_ever ? (int32_t)((millis() - last_rx_ms) / 1000) : -1; }

const char* lora_info_class(void)  { return info_class; }
const char* lora_info_net(void)    { return info_net; }
const char* lora_info_region(void) { return info_region; }
const char* lora_info_rx2(void)    { return info_rx2; }
const char* lora_info_dr(void)     { return info_dr; }
const char* lora_info_join(void)   { return info_join; }
const char* lora_info_ul(void)     { return info_ul; }
const char* lora_info_dl(void)     { return info_dl; }
const char* lora_info_eui(void)    { return info_eui; }
const char* lora_info_fw(void)     { return info_fw; }
int         lora_info_proto(void)  { return modem_proto; }

bool lora_request_poll(void) {
    if (!lora_poll_supported()) return false;   // Class C: nothing to poll
    if (poll_active()) return false;            // ignore taps while a poll is running
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
            LoraKind kind = (buf[k].kind == 0) ? LORA_RX_MSG
                          : (buf[k].kind == 1) ? LORA_TX_SENT
                          : (buf[k].kind == 3) ? LORA_TX_DELIVERED : LORA_TX_FAILED;
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
