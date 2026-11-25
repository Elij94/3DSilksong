#pragma once
// NPC dialogues: a text box with the game's own lines (text/<lang>/<Sheet>.txt), page by page. Started by the
// FSMs' RunDialogue actions; when the last page is gone the FSM that asked gets CONVO_END.
#include <3ds.h>

void dlg_init(void);                       // picks the language (French on a French console, else English)
bool dlg_open(const char *sheet, const char *key, void (*done)(void *ctx), void *ctx);
bool dlg_active(void);
void dlg_update(u32 keys_down);            // A, B or Y: next page
void dlg_draw(void);                       // on the top screen, over the game
void dlg_close(void);
// the game's text: KEY of text/<lang>/<sheet>.txt into out (the key itself if missing); the language (fr, en)
bool text_line(const char *sheet, const char *key, char *out, size_t cap);
void text_set_lang(const char *lang);
const char *text_lang(void);
