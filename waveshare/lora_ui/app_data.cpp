#include "app_data.h"
#include "store.h"
#include <string.h>
#include <strings.h>
#include <ctype.h>

struct ContactRec { uint8_t used; char name[MAX_NAME_LEN + 1]; char tag[MAX_TAG_LEN + 1]; uint32_t color; };
struct ContactStore { uint8_t version; ContactRec rec[MAX_CONTACTS]; };   /* slots 1..MAX_CONTACTS */
#define CONTACT_STORE_VER 1

static ContactRec contacts[NUM_MESSAGES];
static char time_buf[NUM_MESSAGES][16];
static char body_buf[NUM_MESSAGES][112];
MessageItem messages[NUM_MESSAGES];
int current_msg_idx = 0;

static const uint32_t palette[] = { 0x8E24AA, 0xE65100, 0x1565C0, 0x2E7D32, 0xC2185B, 0x5D4037, 0x00838F, 0x6A1B9A };

static void init_message(int i) {
    MessageItem& m = messages[i];
    m.sender = contacts[i].name;
    snprintf(m.preview, sizeof(m.preview), "%s", i == 0 ? "No LoRa messages yet" : "No messages yet");
    if (i == 0) {
        snprintf(body_buf[i], sizeof(body_buf[i]),
                 "LoRa link. Messages here are sent without an ID. Received messages from unknown IDs also land here.");
    } else {
        snprintf(body_buf[i], sizeof(body_buf[i]),
                 "Contact ID: %s. Messages you type here are sent to this contact over LoRa.", contacts[i].tag);
    }
    m.full_body = body_buf[i];
    snprintf(time_buf[i], sizeof(time_buf[i]), "--");
    m.time = time_buf[i];
    m.unread = false;
    m.avatar_color = contacts[i].color;
    m.reply[0] = '\0';
    m.has_reply = false;
}

static void save_contacts(void) {
    ContactStore s;
    memset(&s, 0, sizeof(s));
    s.version = CONTACT_STORE_VER;
    for (int i = 1; i < NUM_MESSAGES; i++) s.rec[i - 1] = contacts[i];
    store_save_contacts(&s, sizeof(s));
}

void app_data_init(void) {
    store_init();

    memset(contacts, 0, sizeof(contacts));
    contacts[0].used = 1;
    snprintf(contacts[0].name, sizeof(contacts[0].name), "LoRa Radio");
    contacts[0].color = 0x00897B;

    ContactStore s;
    memset(&s, 0, sizeof(s));
    if (store_load_contacts(&s, sizeof(s)) && s.version == CONTACT_STORE_VER) {
        for (int i = 1; i < NUM_MESSAGES; i++) {
            contacts[i] = s.rec[i - 1];
            contacts[i].name[MAX_NAME_LEN] = '\0';
            contacts[i].tag[MAX_TAG_LEN] = '\0';
            if (contacts[i].tag[0] == '\0') contacts[i].used = 0;      /* a contact without an ID is unusable */
        }
    }
    for (int i = 0; i < NUM_MESSAGES; i++) init_message(i);
}

void app_touch_time(int idx) {
    uint32_t sec = millis() / 1000;
    snprintf(time_buf[idx], sizeof(time_buf[idx]), "T+%02lu:%02lu",
             (unsigned long)(sec / 60), (unsigned long)(sec % 60));
}

const char* app_tag(int idx) { return contacts[idx].tag; }
bool contact_used(int slot)  { return slot >= 0 && slot < NUM_MESSAGES && contacts[slot].used; }

int app_thread_for_tag(const char* tag, size_t len) {
    if (len == 0 || len > MAX_TAG_LEN) return -1;
    for (int i = 1; i < NUM_MESSAGES; i++)
        if (contacts[i].used && strlen(contacts[i].tag) == len && strncasecmp(contacts[i].tag, tag, len) == 0) return i;
    return -1;
}

int contact_add(const char* name, const char* id, const char** err) {
    static const char* none = "";
    if (!err) err = &none;

    /* name: trimmed, 1..MAX_NAME_LEN */
    char nm[MAX_NAME_LEN + 1];
    while (*name == ' ') name++;
    snprintf(nm, sizeof(nm), "%s", name);
    for (int n = (int)strlen(nm); n > 0 && nm[n - 1] == ' '; ) nm[--n] = '\0';
    if (nm[0] == '\0') { *err = "Enter a name"; return -1; }

    /* ID: 1..MAX_TAG_LEN letters / digits, stored upper case */
    char tg[MAX_TAG_LEN + 1];
    size_t idlen = id ? strlen(id) : 0;
    if (idlen == 0 || idlen > MAX_TAG_LEN) { *err = "ID must be 1 to 4 letters or digits"; return -1; }
    for (size_t i = 0; i < idlen; i++) {
        if (!isalnum((unsigned char)id[i])) { *err = "ID can only use letters and digits"; return -1; }
        tg[i] = (char)toupper((unsigned char)id[i]);
    }
    tg[idlen] = '\0';

    for (int i = 1; i < NUM_MESSAGES; i++)
        if (contacts[i].used && strcmp(contacts[i].tag, tg) == 0) { *err = "That ID is already a contact"; return -1; }

    int slot = -1;
    for (int i = 1; i < NUM_MESSAGES; i++) if (!contacts[i].used) { slot = i; break; }
    if (slot < 0) { *err = "Contact list is full"; return -1; }

    ContactRec& c = contacts[slot];
    memset(&c, 0, sizeof(c));
    c.used = 1;
    snprintf(c.name, sizeof(c.name), "%s", nm);
    snprintf(c.tag, sizeof(c.tag), "%s", tg);
    c.color = palette[(slot - 1) % (sizeof(palette) / sizeof(palette[0]))];
    init_message(slot);
    save_contacts();
    return slot;
}

void contact_remove(int slot) {
    if (slot < 1 || slot >= NUM_MESSAGES || !contacts[slot].used) return;
    memset(&contacts[slot], 0, sizeof(contacts[slot]));
    init_message(slot);
    save_contacts();
}
