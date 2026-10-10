/* gamepad.c - Xbox-style gamepad input for the terminal version.
 *
 * Device layers (OS specific, no libraries):
 *   Linux   - joystick API /dev/input/js0..js7 (<linux/joystick.h>), non-blocking reads.
 *   Windows - XInput loaded at runtime with LoadLibraryA (no import library needed).
 *   other   - pad_init() returns false, pad_poll() returns 0.
 *
 * Everything above the device layers is one platform-independent state machine:
 * a per-pad PadInput snapshot (buttons, D-pad, left stick) is turned into PadEvents
 * by pad_step(), which does rising-edge detection for buttons and held-direction
 * auto-repeat (0.35 s first delay, then every 0.08 s) driven by dt. */
#include "gamepad.h"

#if defined(_WIN32) || defined(__linux__)

#define PAD_DEADZONE       16000
#define PAD_DPAD_MAG       32767
#define PAD_REPEAT_FIRST   0.35
#define PAD_REPEAT_EVERY   0.08
#define PAD_REPEAT_BURST   8      /* max repeats emitted per poll (guards against huge dt) */
#define PAD_PROBE_EVERY    1.0    /* seconds between attempts to open missing devices */

/* One controller's logical input for one frame. Stick: sx right-positive, sy down-positive. */
typedef struct {
    bool up, down, left, right;
    bool a, b, start;
    int  sx, sy;
} PadInput;

/* Per-pad state machine memory. */
typedef struct {
    bool      active;  /* a controller is connected in this slot */
    PadInput  prev;    /* previous frame's snapshot (for edges) */
    ActionType dir;    /* currently held direction, ACT_NONE if none */
    double    rep;     /* seconds until the next auto-repeat of dir */
} PadSlot;

static PadSlot g_slots[PAD_MAX];

static int pad_abs(int v) { return v < 0 ? -v : v; }

static int pad_axis_sign(int v)
{
    if (v > PAD_DEADZONE) return 1;
    if (v < -PAD_DEADZONE) return -1;
    return 0;
}

/* Pick the single direction for this frame. D-pad beats the stick on each axis; if both
 * axes are active, the larger magnitude wins; on a tie the previously held axis is kept
 * (horizontal if there was none). */
static ActionType pad_dir(const PadInput *in, ActionType prev)
{
    int dx = (in->right ? 1 : 0) - (in->left ? 1 : 0);
    int dy = (in->down ? 1 : 0) - (in->up ? 1 : 0);
    int mx = PAD_DPAD_MAG, my = PAD_DPAD_MAG;
    if (dx == 0) {
        dx = pad_axis_sign(in->sx);
        mx = dx ? pad_abs(in->sx) : 0;
    }
    if (dy == 0) {
        dy = pad_axis_sign(in->sy);
        my = dy ? pad_abs(in->sy) : 0;
    }
    bool useX = dx != 0, useY = dy != 0;
    if (useX && useY) {
        if (mx > my) useY = false;
        else if (my > mx) useX = false;
        else if (prev == ACT_UP || prev == ACT_DOWN) useX = false;
        else useY = false;
    }
    if (useX) return dx > 0 ? ACT_RIGHT : ACT_LEFT;
    if (useY) return dy > 0 ? ACT_DOWN : ACT_UP;
    return ACT_NONE;
}

static int pad_push(PadEvent *out, int max, int n, int pad, ActionType a)
{
    if (n < max) {
        out[n].pad = pad;
        out[n].act = a;
        n++;
    }
    return n;
}

/* Advance one pad by one frame; appends its events to out[n..max) and returns the new n.
 * The first sample after a controller appears only primes the state (no events), so a
 * button already held at connect time does not fire. */
static int pad_step(PadSlot *s, int pad, const PadInput *in, double dt,
                    PadEvent *out, int max, int n)
{
    if (!s->active) {
        s->active = true;
        s->prev = *in;
        s->dir = ACT_NONE;
        s->rep = 0.0;
        return n;
    }
    if (!(dt > 0.0)) dt = 0.0; /* also rejects NaN */

    ActionType d = pad_dir(in, s->dir);
    if (d != s->dir) {
        s->dir = d;
        if (d != ACT_NONE) {
            s->rep = PAD_REPEAT_FIRST;
            n = pad_push(out, max, n, pad, d);
        }
    } else if (d != ACT_NONE) {
        s->rep -= dt;
        for (int k = 0; s->rep <= 0.0 && k < PAD_REPEAT_BURST; k++) {
            s->rep += PAD_REPEAT_EVERY;
            n = pad_push(out, max, n, pad, d);
        }
    }

    if (in->a && !s->prev.a) n = pad_push(out, max, n, pad, ACT_CONFIRM);
    if (in->b && !s->prev.b) n = pad_push(out, max, n, pad, ACT_BACK);
    if (in->start && !s->prev.start) n = pad_push(out, max, n, pad, ACT_HOME);

    s->prev = *in;
    return n;
}

static void pad_drop(PadSlot *s)
{
    s->active = false;
    s->dir = ACT_NONE;
    s->rep = 0.0;
}

int pad_count(void)
{
    int c = 0;
    for (int p = 0; p < PAD_MAX; p++)
        if (g_slots[p].active) c++;
    return c;
}

#endif /* _WIN32 || __linux__ */

/* ------------------------------------------------------------------------------------ */
#if defined(__linux__) && !defined(_WIN32)

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/joystick.h>

#define JS_DIR      "/dev/input/js"
#define JS_MAX_DEV  8
#define JS_NBTN     16
#define JS_NAX      8

/* Xbox pads under the Linux xpad driver: buttons 0=A 1=B 7=Start; D-pad as axes 6/7
 * (X: -left/+right, Y: -up/+down) or, on some pads, as buttons 11..14 (up, down, left,
 * right). Left stick: axes 0 (X) and 1 (Y, positive = down). */
typedef struct {
    bool open;          /* js device is open in this slot */
    int  fd;
    int  num;           /* N of /dev/input/jsN */
    bool btn[JS_NBTN];
    int  axis[JS_NAX];
} JsSlot;

static JsSlot g_js[PAD_MAX];
static double g_js_probe_in = 0.0;

static void js_apply(JsSlot *s, const struct js_event *e)
{
    unsigned t = (unsigned)e->type & ~(unsigned)JS_EVENT_INIT;
    if (t == JS_EVENT_BUTTON && e->number < JS_NBTN)
        s->btn[e->number] = e->value != 0;
    else if (t == JS_EVENT_AXIS && e->number < JS_NAX)
        s->axis[e->number] = e->value;
}

static PadInput js_input(const JsSlot *s)
{
    PadInput in = {0};
    in.a = s->btn[0];
    in.b = s->btn[1];
    in.start = s->btn[7];
    in.up = s->btn[11] || s->axis[7] < -PAD_DEADZONE;
    in.down = s->btn[12] || s->axis[7] > PAD_DEADZONE;
    in.left = s->btn[13] || s->axis[6] < -PAD_DEADZONE;
    in.right = s->btn[14] || s->axis[6] > PAD_DEADZONE;
    in.sx = s->axis[0];
    in.sy = s->axis[1];
    return in;
}

static void js_close(int p)
{
    JsSlot *s = &g_js[p];
    if (s->open) close(s->fd);
    s->open = false;
    s->fd = -1;
    for (int i = 0; i < JS_NBTN; i++) s->btn[i] = false;
    for (int i = 0; i < JS_NAX; i++) s->axis[i] = 0;
    pad_drop(&g_slots[p]);
}

static bool js_in_use(int num)
{
    for (int p = 0; p < PAD_MAX; p++)
        if (g_js[p].open && g_js[p].num == num) return true;
    return false;
}

/* Open missing devices into free slots (first PAD_MAX that open win). */
static void js_probe(void)
{
    for (int j = 0; j < JS_MAX_DEV; j++) {
        if (js_in_use(j)) continue;
        int p = 0;
        while (p < PAD_MAX && g_js[p].open) p++;
        if (p >= PAD_MAX) break;
        char path[32];
        snprintf(path, sizeof path, "%s%d", JS_DIR, j);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        JsSlot *s = &g_js[p];
        s->open = true;
        s->fd = fd;
        s->num = j;
        for (int i = 0; i < JS_NBTN; i++) s->btn[i] = false;
        for (int i = 0; i < JS_NAX; i++) s->axis[i] = 0;
    }
}

/* Drain pending events. Returns false when the device is gone (fatal read error). */
static bool js_read(JsSlot *s)
{
    struct js_event ev;
    for (int k = 0; k < 256; k++) {
        ssize_t r = read(s->fd, &ev, sizeof ev);
        if (r == (ssize_t)sizeof ev) {
            js_apply(s, &ev);
        } else if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return true;
            return false; /* ENODEV on unplug, or any other fatal error */
        } else if (r == 0) {
            return false;
        } else {
            return true; /* short read: wait for the next poll */
        }
    }
    return true;
}

bool pad_init(void)
{
    for (int p = 0; p < PAD_MAX; p++) {
        js_close(p);
    }
    g_js_probe_in = 0.0;
    return true;
}

void pad_shutdown(void)
{
    for (int p = 0; p < PAD_MAX; p++) js_close(p);
}

int pad_poll(PadEvent *out, int max, double dt)
{
    int n = 0;
    if (!out || max < 0) max = 0;
    if (!(dt > 0.0)) dt = 0.0;

    g_js_probe_in -= dt;
    if (g_js_probe_in <= 0.0) {
        js_probe();
        g_js_probe_in = PAD_PROBE_EVERY;
    }

    for (int p = 0; p < PAD_MAX; p++) {
        JsSlot *s = &g_js[p];
        if (!s->open) continue;
        if (!js_read(s)) {
            js_close(p);
            continue;
        }
        PadInput in = js_input(s);
        n = pad_step(&g_slots[p], p, &in, dt, out, max, n);
    }
    return n;
}

#endif /* __linux__ */

/* ------------------------------------------------------------------------------------ */
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* XInput structs, declared here so <xinput.h> (and its import library) is not needed. */
typedef struct {
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} PadXGamepad;

typedef struct {
    DWORD       dwPacketNumber;
    PadXGamepad Gamepad;
} PadXState;

typedef DWORD (WINAPI *PadXGetStateFn)(DWORD, PadXState *);

#define PAD_XI_DPAD_UP        0x0001
#define PAD_XI_DPAD_DOWN      0x0002
#define PAD_XI_DPAD_LEFT      0x0004
#define PAD_XI_DPAD_RIGHT     0x0008
#define PAD_XI_START          0x0010
#define PAD_XI_A              0x1000
#define PAD_XI_B              0x2000
#define PAD_XI_USER_COUNT     4

static HMODULE        g_xi_mod = NULL;
static PadXGetStateFn g_xi_get = NULL;
static int            g_xi_user[PAD_MAX]; /* XInput user index per pad, -1 if none */
static double         g_xi_probe_in = 0.0;

static PadInput xi_input(const PadXGamepad *g)
{
    PadInput in = {0};
    in.up = (g->wButtons & PAD_XI_DPAD_UP) != 0;
    in.down = (g->wButtons & PAD_XI_DPAD_DOWN) != 0;
    in.left = (g->wButtons & PAD_XI_DPAD_LEFT) != 0;
    in.right = (g->wButtons & PAD_XI_DPAD_RIGHT) != 0;
    in.start = (g->wButtons & PAD_XI_START) != 0;
    in.a = (g->wButtons & PAD_XI_A) != 0;
    in.b = (g->wButtons & PAD_XI_B) != 0;
    in.sx = g->sThumbLX;
    in.sy = -(int)g->sThumbLY; /* XInput Y is up-positive; ours is down-positive */
    return in;
}

static void xi_unassign(int p)
{
    g_xi_user[p] = -1;
    pad_drop(&g_slots[p]);
}

bool pad_init(void)
{
    static const char *const names[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    if (g_xi_get) return true;
    for (int i = 0; i < 3; i++) {
        HMODULE m = LoadLibraryA(names[i]);
        if (!m) continue;
        FARPROC f = GetProcAddress(m, "XInputGetState");
        if (f) {
            g_xi_mod = m;
            g_xi_get = (PadXGetStateFn)f;
            for (int p = 0; p < PAD_MAX; p++) xi_unassign(p);
            g_xi_probe_in = 0.0;
            return true;
        }
        FreeLibrary(m);
    }
    return false;
}

void pad_shutdown(void)
{
    for (int p = 0; p < PAD_MAX; p++) xi_unassign(p);
    if (g_xi_mod) FreeLibrary(g_xi_mod);
    g_xi_mod = NULL;
    g_xi_get = NULL;
}

int pad_poll(PadEvent *out, int max, double dt)
{
    int n = 0;
    if (!g_xi_get) return 0;
    if (!out || max < 0) max = 0;
    if (!(dt > 0.0)) dt = 0.0;

    g_xi_probe_in -= dt;
    if (g_xi_probe_in <= 0.0) {
        g_xi_probe_in = PAD_PROBE_EVERY;
        for (DWORD u = 0; u < PAD_XI_USER_COUNT; u++) {
            PadXState st;
            int assigned = 0;
            for (int p = 0; p < PAD_MAX; p++)
                if (g_xi_user[p] == (int)u) assigned = 1;
            if (assigned) continue;
            int p = 0;
            while (p < PAD_MAX && g_xi_user[p] >= 0) p++;
            if (p >= PAD_MAX) break;
            if (g_xi_get(u, &st) == 0) g_xi_user[p] = (int)u;
        }
    }

    for (int p = 0; p < PAD_MAX; p++) {
        if (g_xi_user[p] < 0) {
            pad_drop(&g_slots[p]);
            continue;
        }
        PadXState st;
        if (g_xi_get((DWORD)g_xi_user[p], &st) != 0) { /* not connected any more */
            xi_unassign(p);
            continue;
        }
        PadInput in = xi_input(&st.Gamepad);
        n = pad_step(&g_slots[p], p, &in, dt, out, max, n);
    }
    return n;
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------------------------ */
#if !defined(_WIN32) && !defined(__linux__)

bool pad_init(void) { return false; }
void pad_shutdown(void) {}
int pad_count(void) { return 0; }
int pad_poll(PadEvent *out, int max, double dt) { (void)out; (void)max; (void)dt; return 0; }

#endif
