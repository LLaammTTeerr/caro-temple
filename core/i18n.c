/* STUB - replaced by unit "i18n". */
#include "i18n.h"
static Lang cur = LANG_EN;
void i18n_set_lang(Lang l) { cur = (l >= 0 && l < LANG_COUNT) ? l : LANG_EN; }
Lang i18n_lang(void) { return cur; }
const char *tr(StrId id) { (void)id; return "?"; }
