#pragma once
void settings_build(void);
void settings_open(void);      /* refresh now and resume the 1 s timer (call before loading the screen) */
void settings_refresh(void);   /* rewrites only labels whose text changed */
