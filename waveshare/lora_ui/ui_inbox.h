#pragma once

void inbox_build(void);
void inbox_refresh_row(int idx);   /* preview, time and unread styling from messages[idx] */
void inbox_rebuild(void);          /* recreate the rows after a contact was added or removed */
