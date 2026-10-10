/* STUB - replaced by unit "Storage". */
#include <string.h>
#include "storage.h"
const char *storage_dir(void) { return "."; }
void storage_set_dir(const char *dir) { (void)dir; }
void settings_default(Settings *s)
{
    memset(s, 0, sizeof *s);
    s->lang = LANG_EN; s->bgm_volume = 0.7f; s->sfx_volume = 0.8f;
    strcpy(s->profile[0].name, "Fireboy"); s->profile[0].avatar = 0;
    strcpy(s->profile[1].name, "Watergirl"); s->profile[1].avatar = 1;
    s->last_mode = MODE_PVE; s->last_board = 10; s->last_diff = DIFF_MEDIUM;
}
bool settings_load(Settings *s) { settings_default(s); return false; }
bool settings_save(const Settings *s) { (void)s; return true; }
bool storage_save_game(int slot, const Game *g) { (void)slot; (void)g; return false; }
bool storage_load_game(int slot, Game *g) { (void)slot; (void)g; return false; }
bool storage_delete_slot(int slot) { (void)slot; return false; }
void storage_list_slots(SlotInfo out[SAVE_SLOTS]) { memset(out, 0, sizeof(SlotInfo) * SAVE_SLOTS); }
int storage_first_free_slot(void) { return 0; }
int storage_autosave_slot(void) { return 0; }
int storage_latest_slot(void) { return -1; }
