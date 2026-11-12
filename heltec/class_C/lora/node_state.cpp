#include "node_state.h"
#include <string.h>

static TxItem q[TX_QUEUE_LEN];
static uint8_t q_head = 0, q_n = 0;

bool joined = false;
bool wasIdle = false;
unsigned long uplinkCount = 0, downlinkCount = 0, uartLines = 0;
int lastRssi = 0;

bool txq_push(uint16_t id, const char* text) {
  if (q_n >= TX_QUEUE_LEN) return false;
  TxItem& it = q[(q_head + q_n) % TX_QUEUE_LEN];
  it.id = id;
  strncpy(it.text, text, MAX_MSG_BYTES);
  it.text[MAX_MSG_BYTES] = '\0';
  q_n++;
  return true;
}

bool txq_pop(TxItem* out) {
  if (q_n == 0) return false;
  *out = q[q_head];
  q_head = (q_head + 1) % TX_QUEUE_LEN;
  q_n--;
  return true;
}

int txq_count(void) { return q_n; }

/* ---------- delivery receipts ---------- */
uint8_t crc8(const char* s) {
  uint8_t crc = 0;
  for (; *s; s++) {
    crc ^= (uint8_t)*s;
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

struct AckRec { uint16_t id; uint8_t crc; bool used; uint32_t order; };
static AckRec ring[ACK_RING];
static uint32_t ring_order = 0;

void ack_record(uint16_t id, const char* payload) {
  int slot = 0;
  for (int i = 0; i < ACK_RING; i++) {                 /* free slot, else overwrite the oldest */
    if (!ring[i].used) { slot = i; break; }
    if (ring[i].order < ring[slot].order) slot = i;
  }
  ring[slot].id = id; ring[slot].crc = crc8(payload); ring[slot].used = true; ring[slot].order = ++ring_order;
}

bool ack_match(uint8_t crc, uint16_t* id) {
  int best = -1;
  for (int i = 0; i < ACK_RING; i++)
    if (ring[i].used && ring[i].crc == crc && (best < 0 || ring[i].order < ring[best].order)) best = i;
  if (best < 0) return false;
  *id = ring[best].id;
  ring[best].used = false;
  return true;
}
