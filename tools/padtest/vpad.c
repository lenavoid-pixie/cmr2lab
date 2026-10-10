/* vpad.c -- a uinput gamepad whose IDENTITY is a parameter.
 *
 * Derived from work/DECKPAD/padinject.c (the same uinput stand-in that lane
 * built and validated), with one change: vendor/product/name come from the
 * environment, so the same synthetic device can present itself as a Valve pad,
 * an Xbox 360 pad, a DualSense or a Switch Pro pad and the question "does SDL
 * call them all the same thing" can be asked of a real device node.
 *
 * The thumb is still the only synthetic part: everything from /dev/uinput
 * inward is the kernel's own input stack.
 *
 * protocol: lines on stdin, one action each
 *   key <BTN_NAME> <0|1>    hat <x> <y>    abs <NAME> <value>
 *   syn                     sleep <ms>     quit
 * The node it created is printed on the first line of stdout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/uinput.h>
#include <linux/input.h>

static int ufd = -1;
static char g_name[80] = "Lena Pad Stand-In";

static void emit(int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type; ev.code = code; ev.value = value;
    if (write(ufd, &ev, sizeof ev) < 0)
        fprintf(stderr, "vpad: write: %s\n", strerror(errno));
}

static void syn(void) { emit(EV_SYN, SYN_REPORT, 0); }

static int abs_code(const char *n)
{
    struct { const char *n; int c; } T[] = {
        {"ABS_X",ABS_X},{"ABS_Y",ABS_Y},{"ABS_Z",ABS_Z},
        {"ABS_RX",ABS_RX},{"ABS_RY",ABS_RY},{"ABS_RZ",ABS_RZ},
        {"ABS_HAT0X",ABS_HAT0X},{"ABS_HAT0Y",ABS_HAT0Y},
        {NULL,0}
    };
    for (int i = 0; T[i].n; i++) if (!strcmp(T[i].n, n)) return T[i].c;
    return -1;
}

static int key_code(const char *n)
{
    struct { const char *n; int c; } T[] = {
        {"BTN_SOUTH",BTN_SOUTH},{"BTN_A",BTN_SOUTH},{"BTN_EAST",BTN_EAST},
        {"BTN_B",BTN_EAST},{"BTN_WEST",BTN_WEST},{"BTN_X",BTN_WEST},
        {"BTN_NORTH",BTN_NORTH},{"BTN_Y",BTN_NORTH},{"BTN_TL",BTN_TL},
        {"BTN_TR",BTN_TR},{"BTN_SELECT",BTN_SELECT},{"BTN_START",BTN_START},
        {"BTN_MODE",BTN_MODE},{"BTN_THUMBL",BTN_THUMBL},{"BTN_THUMBR",BTN_THUMBR},
        {"BTN_DPAD_UP",BTN_DPAD_UP},{"BTN_DPAD_DOWN",BTN_DPAD_DOWN},
        {"BTN_DPAD_LEFT",BTN_DPAD_LEFT},{"BTN_DPAD_RIGHT",BTN_DPAD_RIGHT},
        {NULL,0}
    };
    for (int i = 0; T[i].n; i++) if (!strcmp(T[i].n, n)) return T[i].c;
    return -1;
}

static void absinfo(int code, int lo, int hi, int fuzz, int flat)
{
    struct uinput_abs_setup a;
    memset(&a, 0, sizeof a);
    a.code = code;
    a.absinfo.minimum = lo;
    a.absinfo.maximum = hi;
    a.absinfo.fuzz = fuzz;
    a.absinfo.flat = flat;
    if (ioctl(ufd, UI_ABS_SETUP, &a) < 0)
        fprintf(stderr, "vpad: UI_ABS_SETUP %d: %s\n", code, strerror(errno));
}

static int find_node(char *out, size_t n)
{
    for (int try = 0; try < 60; try++) {
        DIR *d = opendir("/dev/input");
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (strncmp(de->d_name, "event", 5) != 0) continue;
                char path[160];
                snprintf(path, sizeof path, "/dev/input/%s", de->d_name);
                int fd = open(path, O_RDONLY | O_NONBLOCK);
                if (fd < 0) continue;
                char nm[80] = "";
                int ok = ioctl(fd, EVIOCGNAME(sizeof nm - 1), nm) >= 0 &&
                         strcmp(nm, g_name) == 0;
                close(fd);
                if (ok) { snprintf(out, n, "%s", path); closedir(d); return 1; }
            }
            closedir(d);
        }
        usleep(50000);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *nodepath = argc > 1 ? argv[1] : "/tmp/vpad.dev";
    const char *env;

    if ((env = getenv("VPAD_NAME")) && *env) snprintf(g_name, sizeof g_name, "%s", env);

    unsigned long vid = 0x28de, pid = 0x1205;
    if ((env = getenv("VPAD_VID")) && *env) vid = strtoul(env, NULL, 0);
    if ((env = getenv("VPAD_PID")) && *env) pid = strtoul(env, NULL, 0);

    ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0) { fprintf(stderr, "vpad: /dev/uinput: %s\n", strerror(errno)); return 1; }

    ioctl(ufd, UI_SET_EVBIT, EV_KEY);
    ioctl(ufd, UI_SET_EVBIT, EV_ABS);
    ioctl(ufd, UI_SET_EVBIT, EV_SYN);
    ioctl(ufd, UI_SET_EVBIT, EV_MSC);

    const int KEYS[] = { BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH, BTN_TL, BTN_TR,
                         BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR,
                         BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT };
    for (unsigned i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
        ioctl(ufd, UI_SET_KEYBIT, KEYS[i]);

    /* The axis set the Deck's own virtual node advertises: sticks bipolar at
     * -32768..32767, triggers 0..255 resting at 0, hat a switch. Whether that
     * is also what an Xbox pad reports is exactly what the test is for. */
    ioctl(ufd, UI_SET_ABSBIT, ABS_X);      absinfo(ABS_X, -32768, 32767, 16, 128);
    ioctl(ufd, UI_SET_ABSBIT, ABS_Y);      absinfo(ABS_Y, -32768, 32767, 16, 128);
    ioctl(ufd, UI_SET_ABSBIT, ABS_RX);     absinfo(ABS_RX, -32768, 32767, 16, 128);
    ioctl(ufd, UI_SET_ABSBIT, ABS_RY);     absinfo(ABS_RY, -32768, 32767, 16, 128);
    ioctl(ufd, UI_SET_ABSBIT, ABS_Z);      absinfo(ABS_Z, 0, 255, 0, 0);
    ioctl(ufd, UI_SET_ABSBIT, ABS_RZ);     absinfo(ABS_RZ, 0, 255, 0, 0);
    ioctl(ufd, UI_SET_ABSBIT, ABS_HAT0X);  absinfo(ABS_HAT0X, -1, 1, 0, 0);
    ioctl(ufd, UI_SET_ABSBIT, ABS_HAT0Y);  absinfo(ABS_HAT0Y, -1, 1, 0, 0);

    struct uinput_setup us;
    memset(&us, 0, sizeof us);
    us.id.bustype = BUS_USB;
    us.id.vendor  = (unsigned short)vid;
    us.id.product = (unsigned short)pid;
    us.id.version = 0x0111;
    snprintf(us.name, sizeof us.name, "%s", g_name);
    if (ioctl(ufd, UI_DEV_SETUP, &us) < 0) {
        fprintf(stderr, "vpad: UI_DEV_SETUP: %s\n", strerror(errno)); return 1;
    }
    if (ioctl(ufd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "vpad: UI_DEV_CREATE: %s\n", strerror(errno)); return 1;
    }

    char node[160];
    if (!find_node(node, sizeof node)) {
        fprintf(stderr, "vpad: device created but no /dev/input/event* node found\n");
        return 1;
    }
    printf("%s\n", node);
    fflush(stdout);
    FILE *nf = fopen(nodepath, "w");
    if (nf) { fprintf(nf, "%s\n", node); fclose(nf); }

    emit(EV_ABS, ABS_X, 0); emit(EV_ABS, ABS_Y, 0);
    emit(EV_ABS, ABS_RX, 0); emit(EV_ABS, ABS_RY, 0);
    emit(EV_ABS, ABS_Z, 0); emit(EV_ABS, ABS_RZ, 0);
    emit(EV_ABS, ABS_HAT0X, 0); emit(EV_ABS, ABS_HAT0Y, 0);
    syn();

    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        char cmd[32], a[32];
        int v1, v2;
        if (sscanf(line, "%31s", cmd) != 1) continue;

        if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit")) break;
        else if (!strcmp(cmd, "syn")) syn();
        else if (!strcmp(cmd, "sleep")) {
            if (sscanf(line, "%*s %d", &v1) == 1) usleep(v1 * 1000);
        } else if (!strcmp(cmd, "hat")) {
            if (sscanf(line, "%*s %d %d", &v1, &v2) == 2) {
                emit(EV_ABS, ABS_HAT0X, v1); emit(EV_ABS, ABS_HAT0Y, v2); syn();
            }
        } else if (!strcmp(cmd, "abs")) {
            if (sscanf(line, "%*s %31s %d", a, &v1) == 2) {
                int c = abs_code(a);
                if (c < 0) fprintf(stderr, "vpad: unknown abs %s\n", a);
                else { emit(EV_ABS, c, v1); syn(); }
            }
        } else if (!strcmp(cmd, "key")) {
            if (sscanf(line, "%*s %31s %d", a, &v1) == 2) {
                int c = key_code(a);
                if (c < 0) fprintf(stderr, "vpad: unknown key %s\n", a);
                else { emit(EV_KEY, c, v1); syn(); }
            }
        } else {
            fprintf(stderr, "vpad: ? %s\n", cmd);
        }
    }
    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    return 0;
}
