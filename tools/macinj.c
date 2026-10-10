/* macinj - post keyboard events to a process on macOS (for tests/e2e_gui_macos.sh).
 * Usage: macinj <pid> <keycode>...   (virtual key codes, e.g. 36 Return, 53 Escape)
 * Keys go through the HID event tap (like a real keyboard) to the frontmost app; if this
 * process lacks Accessibility trust they are posted straight to <pid> instead.
 * (Posting both would deliver every key twice.) */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: macinj <pid> <keycode>...\n"); return 2; }
    pid_t pid = (pid_t)atoi(argv[1]);
    bool trusted = AXIsProcessTrusted();
    for (int i = 2; i < argc; i++) {
        CGKeyCode k = (CGKeyCode)atoi(argv[i]);
        for (int down = 1; down >= 0; down--) {
            CGEventRef e = CGEventCreateKeyboardEvent(NULL, k, down ? true : false);
            if (trusted) CGEventPost(kCGHIDEventTap, e);
            else CGEventPostToPid(pid, e);
            CFRelease(e);
            usleep(60000);
        }
        usleep(150000);
    }
    return 0;
}
