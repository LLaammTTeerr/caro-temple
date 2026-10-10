/* STUB - replaced by unit "TUI gamepad". */
#include "gamepad.h"
bool pad_init(void) { return false; }
void pad_shutdown(void) {}
int pad_count(void) { return 0; }
int pad_poll(PadEvent *out, int max, double dt) { (void)out; (void)max; (void)dt; return 0; }
