/* Part of the GUI end-to-end test (tests/e2e_gui.sh). Linux/X11 only. */
/* xinj - tiny XTest input injector (throwaway test tool).
 * Reads commands from stdin, one per line:
 *   key <KeysymName>        press+release (e.g. key space, key Return, key w, key Escape, key Up)
 *   down <Keysym> / up <Keysym>
 *   type <text>             types ASCII text
 *   move <x> <y>            pointer to root coords
 *   click <x> <y>           move + left click
 *   sleep <ms>
 *   shot <file.png>         runs ImageMagick import on the root window
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
extern int XTestFakeKeyEvent(Display *, unsigned int, Bool, unsigned long);
extern int XTestFakeButtonEvent(Display *, unsigned int, Bool, unsigned long);
extern int XTestFakeMotionEvent(Display *, int, int, int, unsigned long);

static void key(Display *d, const char *name, int press, int release)
{
    KeySym ks = XStringToKeysym(name);
    if (ks == NoSymbol) { fprintf(stderr, "bad keysym %s\n", name); return; }
    KeyCode kc = XKeysymToKeycode(d, ks);
    if (press) { XTestFakeKeyEvent(d, kc, True, 0); XFlush(d); usleep(40000); }
    if (release) { XTestFakeKeyEvent(d, kc, False, 0); XFlush(d); usleep(40000); }
}

int main(void)
{
    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "no display\n"); return 1; }
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        char cmd[32] = "", a[256] = "";
        int x, y;
        line[strcspn(line, "\n")] = 0;
        if (sscanf(line, "%31s", cmd) != 1 || cmd[0] == '#') continue;
        const char *rest = line + strlen(cmd);
        while (*rest == ' ') rest++;
        if (!strcmp(cmd, "key")) key(d, rest, 1, 1);
        else if (!strcmp(cmd, "down")) key(d, rest, 1, 0);
        else if (!strcmp(cmd, "up")) key(d, rest, 0, 1);
        else if (!strcmp(cmd, "type")) {
            for (const char *p = rest; *p; p++) {
                char k[16];
                if (*p == ' ') strcpy(k, "space"); else { k[0] = *p; k[1] = 0; }
                key(d, k, 1, 1);
            }
        }
        else if (!strcmp(cmd, "move") && sscanf(rest, "%d %d", &x, &y) == 2) { XTestFakeMotionEvent(d, -1, x, y, 0); XFlush(d); usleep(40000); }
        else if (!strcmp(cmd, "click") && sscanf(rest, "%d %d", &x, &y) == 2) {
            XTestFakeMotionEvent(d, -1, x, y, 0); XFlush(d); usleep(60000);
            XTestFakeButtonEvent(d, 1, True, 0); XFlush(d); usleep(60000);
            XTestFakeButtonEvent(d, 1, False, 0); XFlush(d); usleep(60000);
        }
        else if (!strcmp(cmd, "sleep")) {
            /* XINJ_SCALE stretches every wait (slow CI machines) */
            const char *sc = getenv("XINJ_SCALE");
            double k = sc ? atof(sc) : 1.0;
            if (k < 1.0) k = 1.0;
            usleep((useconds_t)(atoi(rest) * 1000 * k));
        }
        else if (!strcmp(cmd, "shot") && sscanf(rest, "%255s", a) == 1) {
            char c[400]; snprintf(c, sizeof c, "import -window root %s", a);
            if (system(c) != 0) fprintf(stderr, "shot failed\n");
        }
        else fprintf(stderr, "?? %s\n", line);
    }
    XCloseDisplay(d);
    return 0;
}
