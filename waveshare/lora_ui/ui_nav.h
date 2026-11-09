#pragma once
#include <lvgl.h>

/* Screen pointers live here so screen modules never include each other. */
extern lv_obj_t* scr_home;
extern lv_obj_t* scr_settings;
extern lv_obj_t* scr_pin;
extern lv_obj_t* scr_inbox;
extern lv_obj_t* scr_detail;     /* chat */
extern lv_obj_t* scr_addcontact; /* add-contact form */

void go_home(void);
void go_settings(void);
void go_pin(void);               /* the only way into Messages */
void go_inbox(void);
void go_chat(int idx);
void go_addcontact(void);
