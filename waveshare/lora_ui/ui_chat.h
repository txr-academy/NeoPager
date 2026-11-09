#pragma once

void chat_build(void);          /* builds scr_detail (chat + keyboard) */
void chat_open(int idx);        /* rebuilds the bubbles only if needed, then loads the screen */
void chat_poll(void);
void chat_forget_thread(int slot);   /* a contact was deleted: drop its cached bubbles */           /* call every loop(): pulls new modem data into inbox row + chat */
