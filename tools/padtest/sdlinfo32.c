/* sdlinfo32.c -- what does SDL think is plugged in?
 *
 * A 32-bit probe with no window and no game: it initialises SDL's gamepad
 * subsystem exactly the way the port's input layer does, then reports, for
 * every device SDL can see:
 *   - whether SDL classifies it as a gamepad (the mapping database's answer)
 *   - SDL's own name and type for it, and the evdev node behind it
 *   - the NAMED axes and NAMED buttons the port reads
 *
 * Usage: sdlinfo32 [seconds]     (default 2; prints a line per update where
 *                                 something changed, plus one summary at the end)
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double mono(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (double)ts.tv_sec+(double)ts.tv_nsec/1e9;}

static const char *type_name(int t)
{
    switch (t) {
    case SDL_GAMEPAD_TYPE_STANDARD:                     return "standard";
    case SDL_GAMEPAD_TYPE_XBOX360:                      return "Xbox360";
    case SDL_GAMEPAD_TYPE_XBOXONE:                      return "XboxOne";
    case SDL_GAMEPAD_TYPE_PS3:                          return "PS3";
    case SDL_GAMEPAD_TYPE_PS4:                          return "PS4";
    case SDL_GAMEPAD_TYPE_PS5:                          return "PS5";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:          return "SwitchPro";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:  return "JoyConL";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return "JoyConR";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:  return "JoyConPair";
    case SDL_GAMEPAD_TYPE_GAMECUBE:                     return "GameCube";
    case SDL_GAMEPAD_TYPE_STEAM:                        return "Steam";
    default:                                            return "unknown";
    }
}

static const char *btn_name(int b)
{
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH:          return "SOUTH";
    case SDL_GAMEPAD_BUTTON_EAST:           return "EAST";
    case SDL_GAMEPAD_BUTTON_WEST:           return "WEST";
    case SDL_GAMEPAD_BUTTON_NORTH:          return "NORTH";
    case SDL_GAMEPAD_BUTTON_BACK:           return "BACK";
    case SDL_GAMEPAD_BUTTON_GUIDE:          return "GUIDE";
    case SDL_GAMEPAD_BUTTON_START:          return "START";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:     return "LSTICK";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:    return "RSTICK";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  return "LSHOULDER";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RSHOULDER";
    case SDL_GAMEPAD_BUTTON_DPAD_UP:        return "DPAD_UP";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:      return "DPAD_DOWN";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:      return "DPAD_LEFT";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:     return "DPAD_RIGHT";
    default:                                return "?";
    }
}

int main(int argc, char **argv)
{
    double secs = argc > 1 ? atof(argv[1]) : 2.0;
    setvbuf(stdout, NULL, _IOLBF, 0);   /* read live, not at exit */
    if (getenv("SDL_VIDEODRIVER") == NULL) { /* nothing to do: no video wanted */ }
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    /* SDL's own account of what it is doing with the devices, on stderr. */
    {
        const char *lg = getenv("SDL_LOGGING");
        SDL_SetHint(SDL_HINT_LOGGING, lg && *lg ? lg : "input=debug");
    }

    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        printf("INIT FAIL: %s\n", SDL_GetError());
        return 1;
    }
    printf("SDL %d.%d.%d -- gamepad subsystem up\n",
           SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
           SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
           SDL_VERSIONNUM_MICRO(SDL_GetVersion()));

    /* EVERY joystick first, gamepad or not: "SDL has no gamepad" and "SDL sees
     * nothing at all" are different answers and need different fixes. */
    {
        int jn = 0;
        SDL_JoystickID *jids = SDL_GetJoysticks(&jn);
        printf("joystick list: %d device(s)\n", jn);
        for (int i = 0; jids && i < jn; i++) {
            const char *nm = SDL_GetJoystickNameForID(jids[i]);
            const char *pt = SDL_GetJoystickPathForID(jids[i]);
            SDL_Joystick *j = SDL_OpenJoystick(jids[i]);
            printf("  [%d] name='%s' is_gamepad=%d path=%s\n", i,
                   nm ? nm : "(null)", (int)SDL_IsGamepad(jids[i]),
                   pt ? pt : "(null)");
            if (j) {
                printf("      vendor=0x%04x product=0x%04x axes=%d buttons=%d hats=%d\n",
                       SDL_GetJoystickVendor(j), SDL_GetJoystickProduct(j),
                       SDL_GetNumJoystickAxes(j), SDL_GetNumJoystickButtons(j),
                       SDL_GetNumJoystickHats(j));
                char *map = SDL_GetGamepadMappingForGUID(SDL_GetJoystickGUID(j));
                printf("      mapping=%s\n", map ? map : "(none)");
                SDL_free(map);
                SDL_CloseJoystick(j);
            }
        }
        if (jids) SDL_free(jids);
    }

    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    printf("device list: %d gamepad(s)\n", n);
    if (ids) {
        for (int i = 0; i < n; i++) {
            SDL_Gamepad *g = SDL_OpenGamepad(ids[i]);
            printf("  [%d] id=%u is_gamepad=%d name='%s' type=%s path=%s\n", i,
                   (unsigned)ids[i], (int)SDL_IsGamepad(ids[i]),
                   SDL_GetGamepadNameForID(ids[i]) ? SDL_GetGamepadNameForID(ids[i]) : "(null)",
                   type_name((int)SDL_GetGamepadTypeForID(ids[i])),
                   SDL_GetJoystickPathForID(ids[i]) ? SDL_GetJoystickPathForID(ids[i]) : "(null)");
            if (!g) {
                printf("      SDL_OpenGamepad FAILED: %s\n", SDL_GetError());
                continue;
            }
            for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++)
                printf("      axis %-14s = %6d%s\n",
                       a == SDL_GAMEPAD_AXIS_LEFTX ? "LEFTX" :
                       a == SDL_GAMEPAD_AXIS_LEFTY ? "LEFTY" :
                       a == SDL_GAMEPAD_AXIS_RIGHTX ? "RIGHTX" :
                       a == SDL_GAMEPAD_AXIS_RIGHTY ? "RIGHTY" :
                       a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? "LEFT_TRIGGER" : "RIGHT_TRIGGER",
                       (int)SDL_GetGamepadAxis(g, (SDL_GamepadAxis)a),
                       SDL_GamepadHasAxis(g, (SDL_GamepadAxis)a) ? "" : "   (absent)");
            SDL_CloseGamepad(g);
        }
        SDL_free(ids);
    }

    /* Then watch: print every change in the named buttons and named axes, with
     * the time since start, so a press can be attributed. */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    unsigned char was[SDL_GAMEPAD_BUTTON_COUNT];
    memset(was, 0, sizeof was);

    /* Opened ONCE, then watched -- which is also what the port does. (Opening
     * and closing it every pass re-synced the device and produced a stream of
     * zeros, which is a measurement artefact and not a device property.) */
    SDL_Gamepad *watch = NULL;
    {
        int n2 = 0;
        SDL_JoystickID *i2 = SDL_GetGamepads(&n2);
        const char *pin = getenv("SDLPIN");
        for (int i = 0; i2 && i < n2; i++) {
            const char *nm = SDL_GetGamepadNameForID(i2[i]);
            if (pin && *pin && !(nm && strstr(nm, pin))) continue;
            watch = SDL_OpenGamepad(i2[i]);
            if (watch) break;
        }
        if (i2) SDL_free(i2);
        if (watch) printf("watching '%s' (type=%s)\n", SDL_GetGamepadName(watch),
                          type_name((int)SDL_GetGamepadType(watch)));
    }

    while (watch) {
        SDL_UpdateJoysticks();
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
        if (el > secs) break;

        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
            int now = SDL_GetGamepadButton(watch, (SDL_GamepadButton)b) ? 1 : 0;
            if (now != was[b])
                printf("T=%.6f t=%.3f button %-11s %s\n", mono(), el, btn_name(b), now ? "DOWN" : "UP");
            was[b] = (unsigned char)now;
        }
        static int last[SDL_GAMEPAD_AXIS_COUNT] = { -1,-1,-1,-1,-1,-1 };
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) {
            int v = (int)SDL_GetGamepadAxis(watch, (SDL_GamepadAxis)a);
            if (v != last[a]) {
                printf("T=%.6f t=%.3f axis    %-14s = %6d\n", mono(), el,
                       a == SDL_GAMEPAD_AXIS_LEFTX ? "LEFTX" :
                       a == SDL_GAMEPAD_AXIS_LEFTY ? "LEFTY" :
                       a == SDL_GAMEPAD_AXIS_RIGHTX ? "RIGHTX" :
                       a == SDL_GAMEPAD_AXIS_RIGHTY ? "RIGHTY" :
                       a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? "LEFT_TRIGGER" : "RIGHT_TRIGGER", v);
                last[a] = v;
            }
        }
        if (!SDL_GamepadConnected(watch)) { printf("disconnected\n"); break; }
    }
    if (watch) SDL_CloseGamepad(watch);

    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    return 0;
}
