/* padbench32.c -- the two pad backends, measured side by side.
 *
 * The question the phone brief asks is whether routing pad input through SDL
 * costs anything on the Deck: latency, trigger resolution, Steam Input. The
 * answer is a number, so this is a number-producing tool and not an argument.
 *
 *   padbench32 sdl   <name-substring> <seconds>   SDL gamepad, named axes
 *   padbench32 evdev <event-node>     <seconds>   the kernel reader this port
 *                                                 shipped on
 *
 * Both modes print one line per change of the trigger axis:
 *     <kind> <CLOCK_MONOTONIC seconds> <raw-or-normalised>
 * CLOCK_MONOTONIC is system-wide, so an injector in another process can stamp
 * the moment it wrote the event and this prints the moment the reader saw it.
 * Same injection, same clock, two readers: the difference between the modes is
 * the hop that was added.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <SDL3/SDL.h>
#include <linux/input.h>

static double now_mono(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int run_sdl(const char *name, double secs)
{
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        printf("INIT FAIL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Gamepad *g = NULL;
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) {
        const char *nm = SDL_GetGamepadNameForID(ids[i]);
        if (name && *name && !(nm && strstr(nm, name))) continue;
        g = SDL_OpenGamepad(ids[i]);
        if (g) break;
    }
    if (ids) SDL_free(ids);
    if (!g) { printf("NO GAMEPAD matching '%s'\n", name ? name : ""); return 1; }

    printf("sdl device '%s'\n", SDL_GetGamepadName(g));
    fflush(stdout);

    int last[SDL_GAMEPAD_AXIS_COUNT];
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) last[a] = -999999;
    double t0 = now_mono();
    while (now_mono() - t0 < secs) {
        SDL_UpdateJoysticks();
        if (!SDL_GamepadConnected(g)) { printf("sdl disconnected\n"); break; }
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) {
            int v = (int)SDL_GetGamepadAxis(g, (SDL_GamepadAxis)a);
            if (v != last[a]) {
                last[a] = v;
                printf("sdl %.6f axis%d %d\n", now_mono(), a, v);
                fflush(stdout);
            }
        }
    }
    SDL_CloseGamepad(g);
    return 0;
}

static int run_evdev(const char *node, double secs)
{
    int fd = open(node, O_RDONLY | O_NONBLOCK);
    if (fd < 0) { printf("OPEN FAIL %s: %s\n", node, strerror(errno)); return 1; }
    printf("evdev device %s\n", node);
    fflush(stdout);

    int last[64];
    for (int i = 0; i < 64; i++) last[i] = -999999;
    double t0 = now_mono();
    while (now_mono() - t0 < secs) {
        struct input_event ev[32];
        ssize_t n;
        int got = 0;
        while ((n = read(fd, ev, sizeof ev)) > 0) {
            int c = (int)(n / (ssize_t)sizeof ev[0]);
            got = 1;
            for (int i = 0; i < c; i++) {
                if (ev[i].type != EV_ABS || ev[i].code >= 64) continue;
                if (ev[i].value == last[ev[i].code]) continue;
                last[ev[i].code] = ev[i].value;
                /* the kernel's own timestamp for the event, and the moment this
                 * reader got it: both are printed, so "the reader saw it late"
                 * and "the event happened late" can be told apart. */
                printf("evdev %.6f abs%d %d  ktime %.6f\n", now_mono(), ev[i].code,
                       ev[i].value,
                       (double)ev[i].input_event_sec + (double)ev[i].input_event_usec / 1e6);
                fflush(stdout);
            }
        }
        (void)got;
        /* no sleep here on purpose: the two modes must poll at the same rate,
         * or the measurement is of this loop and not of the path. */
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s sdl <name-substring> <seconds>\n"
                        "       %s evdev <event-node> <seconds>\n", argv[0], argv[0]);
        return 2;
    }
    double secs = atof(argv[3]);
    if (!strcmp(argv[1], "sdl"))   return run_sdl(argv[2], secs);
    if (!strcmp(argv[1], "evdev")) return run_evdev(argv[2], secs);
    fprintf(stderr, "unknown mode %s\n", argv[1]);
    return 2;
}
