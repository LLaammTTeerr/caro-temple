/* vpad - virtual Xbox 360 controller via /dev/uinput, for tests/e2e_gamepad.sh (needs root).
 * stdin commands: a | b | start  (press+release), up|down|left|right (D-pad tap), sleep <ms>, cmd <shell> */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fd;
static void emit(int type, int code, int val)
{
    struct input_event ie;
    memset(&ie, 0, sizeof ie);
    ie.type = type; ie.code = code; ie.value = val;
    if (write(fd, &ie, sizeof ie) < 0) perror("write");
}
static void syn(void) { emit(EV_SYN, SYN_REPORT, 0); }
static void tap_key(int code) { emit(EV_KEY, code, 1); syn(); usleep(80000); emit(EV_KEY, code, 0); syn(); usleep(150000); }
static void tap_hat(int axis, int v) { emit(EV_ABS, axis, v); syn(); usleep(80000); emit(EV_ABS, axis, 0); syn(); usleep(150000); }
static void absinfo(int code, int min, int max)
{
    struct uinput_abs_setup a;
    memset(&a, 0, sizeof a);
    a.code = code; a.absinfo.minimum = min; a.absinfo.maximum = max;
    if (code == ABS_X || code == ABS_Y || code == ABS_RX || code == ABS_RY) { a.absinfo.fuzz = 16; a.absinfo.flat = 128; }
    ioctl(fd, UI_ABS_SETUP, &a);
}
int main(void)
{
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("/dev/uinput"); return 1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    int keys[] = { BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR };
    for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    int axes[] = { ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_HAT0X, ABS_HAT0Y };
    for (unsigned i = 0; i < sizeof axes / sizeof *axes; i++) ioctl(fd, UI_SET_ABSBIT, axes[i]);
    absinfo(ABS_X, -32768, 32767); absinfo(ABS_Y, -32768, 32767);
    absinfo(ABS_RX, -32768, 32767); absinfo(ABS_RY, -32768, 32767);
    absinfo(ABS_Z, 0, 255); absinfo(ABS_RZ, 0, 255);
    absinfo(ABS_HAT0X, -1, 1); absinfo(ABS_HAT0Y, -1, 1);
    struct uinput_setup us;
    memset(&us, 0, sizeof us);
    us.id.bustype = BUS_USB; us.id.vendor = 0x045e; us.id.product = 0x028e; us.id.version = 0x0114; /* matches the "X360 Controller" Linux SDL mapping */
    strcpy(us.name, "Microsoft X-Box 360 pad");
    ioctl(fd, UI_DEV_SETUP, &us);
    if (ioctl(fd, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); return 1; }
    fprintf(stderr, "vpad: created\n");
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (!strcmp(line, "a")) tap_key(BTN_SOUTH);
        else if (!strcmp(line, "b")) tap_key(BTN_EAST);
        else if (!strcmp(line, "start")) tap_key(BTN_START);
        else if (!strcmp(line, "up")) tap_hat(ABS_HAT0Y, -1);
        else if (!strcmp(line, "down")) tap_hat(ABS_HAT0Y, 1);
        else if (!strcmp(line, "left")) tap_hat(ABS_HAT0X, -1);
        else if (!strcmp(line, "right")) tap_hat(ABS_HAT0X, 1);
        else if (!strncmp(line, "sleep ", 6)) usleep(atoi(line + 6) * 1000);
        else if (!strncmp(line, "cmd ", 4)) { if (system(line + 4) != 0) fprintf(stderr, "cmd failed: %s\n", line + 4); }
    }
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
