#pragma once
#include "app_types.h"
#include "config.h"

extern MessageItem messages[NUM_MESSAGES];   /* one per slot; slot 0 = "LoRa Radio" */
extern int current_msg_idx;

void app_data_init(void);                              /* FIRST call in setup(): opens flash, loads the contact table */
void app_touch_time(int idx);                          /* "T+mm:ss" on that chat's inbox row */
const char* app_tag(int idx);                          /* contact ID; "" for slot 0 */
int  app_thread_for_tag(const char* tag, size_t len);  /* slot of a used contact (case-insensitive), or -1 */

bool contact_used(int slot);
/* Adds a contact. Returns its slot, or -1 with *err set to a short reason for the UI. */
int  contact_add(const char* name, const char* id, const char** err);
void contact_remove(int slot);                         /* frees the slot and saves; slot 0 cannot be removed */

/* Unread tracking and recency (used by the inbox order, the unread badges and the Home screen) */
void     app_unread_inc(int idx);              /* a message arrived while that chat was not on screen */
void     app_unread_clear(int idx);
int      app_unread_count(int idx);
int      app_unread_total(void);               /* sum over all chats */
uint32_t app_last_activity(int idx);           /* millis()+1 of the last send/receive, 0 = none since boot */
