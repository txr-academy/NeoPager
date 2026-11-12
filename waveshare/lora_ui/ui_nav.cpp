#include "ui_nav.h"
#include "ui_settings.h"
#include "ui_pin.h"
#include "ui_chat.h"
#include "ui_contacts.h"

lv_obj_t* scr_home = NULL;
lv_obj_t* scr_settings = NULL;
lv_obj_t* scr_pin = NULL;
lv_obj_t* scr_inbox = NULL;
lv_obj_t* scr_detail = NULL;
lv_obj_t* scr_addcontact = NULL;

/* Messages relock implicitly: the inbox is only reachable through go_pin(),
   and every route back to Home leaves it. Inbox <-> chat never asks again. */
void go_home(void)     { settings_close(); lv_scr_load(scr_home); }   /* also called when the screen turns off */
void go_settings(void) { settings_open(); lv_scr_load(scr_settings); }
void go_pin(void)      { pin_reset(); lv_scr_load(scr_pin); }
void go_inbox(void)    { lv_scr_load(scr_inbox); }
void go_chat(int idx)  { chat_open(idx); }      /* loads scr_detail itself */
void go_addcontact(void) { contacts_open(); }   /* loads scr_addcontact itself */
