/**
 * platform_switch.c — Nintendo Switch platform implementation (libnx + SDL2)
 */

#include <switch.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "platform.h"
#include "pad_bits.h"
#include "gamepad_buttons.h"

SDL_Window *g_sdlWindow = NULL;
SDL_GLContext g_sdlGLContext = NULL;

static int s_fbWidth = 1280;
static int s_fbHeight = 720;
static int s_quitRequested = 0;
unsigned char s_keystate[256];

#define MAX_GAMEPADS 4
#define JOY_BUTTONS_PER_SLOT 80

typedef struct {
    SDL_GameController *ctrl;
    SDL_Joystick       *joy;
    SDL_JoystickID      id;
    int                 nButtons;
} GamepadSlot;

static GamepadSlot s_pads[MAX_GAMEPADS];
static int s_padCount = 0;

#define TRIGGER_THRESHOLD_I 8192
#define STICK_DEADZONE_I    19660

extern unsigned char g_keyPressState[320];
extern short g_joystickConfigWords[];
extern char g_joystickSlots[4][282];
extern char g_joystickDeviceNames[4][260];
extern short g_joystickDeviceFlags[];
extern void SyncJoystickSlots(void);

static void gamepad_close(int slot)
{
    if (slot < 0 || slot >= s_padCount) return;
    if (s_pads[slot].ctrl) {
        SDL_GameControllerClose(s_pads[slot].ctrl);
        s_pads[slot].ctrl = NULL;
    }
    if (s_pads[slot].joy) {
        SDL_JoystickClose(s_pads[slot].joy);
        s_pads[slot].joy = NULL;
    }
    s_pads[slot].id = -1;
    s_pads[slot].nButtons = 0;
}

static int gamepad_open(int deviceIndex)
{
    if (s_padCount >= MAX_GAMEPADS) return 0;

    SDL_JoystickID jid = SDL_JoystickGetDeviceInstanceID(deviceIndex);
    for (int i = 0; i < s_padCount; i++) {
        if (s_pads[i].id == jid) return 0;
    }

    GamepadSlot *pad = &s_pads[s_padCount];
    memset(pad, 0, sizeof(*pad));

    if (SDL_IsGameController(deviceIndex)) {
        pad->ctrl = SDL_GameControllerOpen(deviceIndex);
        if (pad->ctrl) {
            pad->joy = SDL_GameControllerGetJoystick(pad->ctrl);
            pad->id = SDL_JoystickInstanceID(pad->joy);
            pad->nButtons = GC_BUTTON_COUNT;
            const char *name = SDL_GameControllerName(pad->ctrl);
            strncpy(g_joystickDeviceNames[s_padCount], name ? name : "Switch Controller", 259);
            g_joystickDeviceFlags[s_padCount] = (short)pad->nButtons;
            s_padCount++;
            return 1;
        }
    }

    pad->joy = SDL_JoystickOpen(deviceIndex);
    if (pad->joy) {
        pad->id = SDL_JoystickInstanceID(pad->joy);
        pad->nButtons = SDL_JoystickNumButtons(pad->joy);
        const char *name = SDL_JoystickName(pad->joy);
        strncpy(g_joystickDeviceNames[s_padCount], name ? name : "Joystick", 259);
        g_joystickDeviceFlags[s_padCount] = (short)pad->nButtons;
        s_padCount++;
        return 1;
    }

    return 0;
}

static unsigned char SDLScancodeToDIK(SDL_Scancode sc)
{
    switch (sc) {
        case SDL_SCANCODE_ESCAPE: return DIK_ESCAPE;
        case SDL_SCANCODE_RETURN: return DIK_RETURN;
        case SDL_SCANCODE_SPACE:  return DIK_SPACE;
        case SDL_SCANCODE_UP:     return DIK_UP;
        case SDL_SCANCODE_DOWN:   return DIK_DOWN;
        case SDL_SCANCODE_LEFT:   return DIK_LEFT;
        case SDL_SCANCODE_RIGHT:  return DIK_RIGHT;
        case SDL_SCANCODE_A:      return DIK_A;
        case SDL_SCANCODE_B:      return DIK_B;
        case SDL_SCANCODE_C:      return DIK_C;
        case SDL_SCANCODE_D:      return DIK_D;
        case SDL_SCANCODE_E:      return DIK_E;
        case SDL_SCANCODE_F:      return DIK_F;
        case SDL_SCANCODE_Q:      return DIK_Q;
        case SDL_SCANCODE_R:      return DIK_R;
        case SDL_SCANCODE_S:      return DIK_S;
        case SDL_SCANCODE_W:      return DIK_W;
        case SDL_SCANCODE_X:      return DIK_X;
        case SDL_SCANCODE_Z:      return DIK_Z;
        case SDL_SCANCODE_1:      return DIK_1;
        case SDL_SCANCODE_2:      return DIK_2;
        case SDL_SCANCODE_3:      return DIK_3;
        case SDL_SCANCODE_4:      return DIK_4;
        default:                  return 0;
    }
}

static void HandleSDLEvent(SDL_Event *event)
{
    switch (event->type) {
        case SDL_QUIT:
            s_quitRequested = 1;
            break;

        case SDL_KEYDOWN: {
            unsigned char dik = SDLScancodeToDIK(event->key.keysym.scancode);
            if (dik) s_keystate[dik] = 0x80;
            break;
        }

        case SDL_KEYUP: {
            unsigned char dik = SDLScancodeToDIK(event->key.keysym.scancode);
            if (dik) s_keystate[dik] = 0x00;
            break;
        }

        case SDL_JOYDEVICEADDED:
            if (gamepad_open(event->jdevice.which)) {
                SyncJoystickSlots();
            }
            break;

        case SDL_JOYDEVICEREMOVED: {
            SDL_JoystickID jid = event->jdevice.which;
            for (int i = 0; i < s_padCount; i++) {
                if (s_pads[i].id == jid) {
                    gamepad_close(i);
                    memset(&g_keyPressState[i * JOY_BUTTONS_PER_SLOT], 0, JOY_BUTTONS_PER_SLOT);
                    for (int j = i; j < s_padCount - 1; j++) {
                        s_pads[j] = s_pads[j + 1];
                    }
                    s_pads[s_padCount - 1].ctrl = NULL;
                    s_pads[s_padCount - 1].joy = NULL;
                    s_pads[s_padCount - 1].id = -1;
                    s_pads[s_padCount - 1].nButtons = 0;
                    s_padCount--;
                    break;
                }
            }
            break;
        }

        default:
            break;
    }
}

static char s_detectedBasePath[512] = "";

const char *platform_base_path(void)
{
    if (s_detectedBasePath[0] != '\0') {
        return s_detectedBasePath;
    }

    /* Probe candidate directories for Sonic R assets */
    const char *candidates[] = {
        "sdmc:/switch/sonicr",
        "sdmc:/sonicr",
        "romfs:",
        "."
    };

    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        char testPath[512];
        snprintf(testPath, sizeof(testPath), "%s/GENERAL/SONICR.BIT", candidates[i]);
        FILE *f = fopen(testPath, "rb");
        if (f) {
            fclose(f);
            strncpy(s_detectedBasePath, candidates[i], sizeof(s_detectedBasePath) - 1);
            printf("Found Sonic R data at: %s\n", s_detectedBasePath);
            return s_detectedBasePath;
        }
    }

    /* Fall back to standard homebrew location */
    strncpy(s_detectedBasePath, "sdmc:/switch/sonicr", sizeof(s_detectedBasePath) - 1);
    return s_detectedBasePath;
}

int platform_init(int width, int height, int fullscreen, const char *title)
{
    (void)fullscreen;
    (void)title;
    s_fbWidth = 1280;
    s_fbHeight = 720;
    memset(s_keystate, 0, sizeof(s_keystate));
    s_quitRequested = 0;

    romfsInit();
    socketInitializeDefault();

    /* Switch into detected base directory */
    const char *base = platform_base_path();
    if (base && chdir(base) != 0) {
        printf("Notice: chdir(%s) failed, staying in cwd\n", base);
    }

    /* Ensure SAVE and GHOST directories exist on SD card */
    mkdir("SAVE", 0777);
    mkdir("GHOST", 0777);

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        fprintf(stderr, "Mix_OpenAudio failed: %s\n", Mix_GetError());
    }
    Mix_AllocateChannels(64);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    g_sdlWindow = SDL_CreateWindow(
        "Sonic R",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN
    );

    if (!g_sdlWindow) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    g_sdlGLContext = SDL_GL_CreateContext(g_sdlWindow);
    if (!g_sdlGLContext) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_MakeCurrent(g_sdlWindow, g_sdlGLContext);
    SDL_GL_SetSwapInterval(1);

    platform_init_gamepads();
    return 0;
}

void platform_shutdown(void)
{
    for (int i = 0; i < s_padCount; i++) {
        gamepad_close(i);
    }
    s_padCount = 0;

    if (g_sdlGLContext) {
        SDL_GL_DeleteContext(g_sdlGLContext);
        g_sdlGLContext = NULL;
    }
    if (g_sdlWindow) {
        SDL_DestroyWindow(g_sdlWindow);
        g_sdlWindow = NULL;
    }

    Mix_CloseAudio();
    SDL_Quit();

    socketExit();
    romfsExit();
}

int platform_poll_events(unsigned char *keystateOut, int keystateSize)
{
    if (!appletMainLoop()) {
        s_quitRequested = 1;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        HandleSDLEvent(&event);
    }

    int copySize = keystateSize < 256 ? keystateSize : 256;
    memcpy(keystateOut, s_keystate, copySize);
    return s_quitRequested;
}

void platform_pump_events(void)
{
    if (!appletMainLoop()) {
        s_quitRequested = 1;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        HandleSDLEvent(&event);
    }
}

int platform_init_gamepads(void)
{
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n && s_padCount < MAX_GAMEPADS; i++) {
        gamepad_open(i);
    }
    return s_padCount;
}

static int gamepad_button_held(const GamepadSlot *pad, int b)
{
    if (pad->ctrl) {
        if (b == GCBTN_TRIGGER_LEFT) {
            return SDL_GameControllerGetAxis(pad->ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > TRIGGER_THRESHOLD_I;
        }
        if (b == GCBTN_TRIGGER_RIGHT) {
            return SDL_GameControllerGetAxis(pad->ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > TRIGGER_THRESHOLD_I;
        }
        return SDL_GameControllerGetButton(pad->ctrl, (SDL_GameControllerButton)b) != 0;
    }
    return SDL_JoystickGetButton(pad->joy, b) != 0;
}

int platform_poll_gamepads(unsigned short *joySlotState, int maxSlots)
{
    int count = s_padCount;
    if (count > maxSlots) count = maxSlots;

    for (int i = 0; i < count; i++) {
        const GamepadSlot *pad = &s_pads[i];
        unsigned char *pressBase = &g_keyPressState[i * JOY_BUTTONS_PER_SLOT];

        if (!pad->ctrl && !pad->joy) {
            joySlotState[i] = 0;
            memset(pressBase, 0, JOY_BUTTONS_PER_SLOT);
            continue;
        }

        /* Check for Minus + Plus combination to quit back to hbmenu */
        if (pad->ctrl) {
            if (SDL_GameControllerGetButton(pad->ctrl, SDL_CONTROLLER_BUTTON_BACK) &&
                SDL_GameControllerGetButton(pad->ctrl, SDL_CONTROLLER_BUTTON_START)) {
                s_quitRequested = 1;
            }
        }

        unsigned short bits = 0;

        /* D-Pad from Hat if unmapped joystick */
        if (pad->joy && SDL_JoystickNumHats(pad->joy) > 0) {
            Uint8 hat = SDL_JoystickGetHat(pad->joy, 0);
            if (hat & SDL_HAT_LEFT)  bits |= PAD_LEFT;
            if (hat & SDL_HAT_RIGHT) bits |= PAD_RIGHT;
            if (hat & SDL_HAT_UP)    bits |= PAD_UP;
            if (hat & SDL_HAT_DOWN)  bits |= PAD_DOWN;
        }

        /* Analog stick */
        Sint16 lx = 0, ly = 0;
        if (pad->ctrl) {
            lx = SDL_GameControllerGetAxis(pad->ctrl, SDL_CONTROLLER_AXIS_LEFTX);
            ly = SDL_GameControllerGetAxis(pad->ctrl, SDL_CONTROLLER_AXIS_LEFTY);
        } else if (SDL_JoystickNumAxes(pad->joy) >= 2) {
            lx = SDL_JoystickGetAxis(pad->joy, 0);
            ly = SDL_JoystickGetAxis(pad->joy, 1);
        }

        if (lx < -STICK_DEADZONE_I) bits |= PAD_LEFT;
        if (lx >  STICK_DEADZONE_I) bits |= PAD_RIGHT;
        if (ly < -STICK_DEADZONE_I) bits |= PAD_UP;
        if (ly >  STICK_DEADZONE_I) bits |= PAD_DOWN;

        /* Action buttons */
        const short *perSlotWords = (const short *)&g_joystickSlots[i][0x104];
        int numButtons = pad->nButtons;
        if (numButtons > JOY_BUTTONS_PER_SLOT) numButtons = JOY_BUTTONS_PER_SLOT;

        for (int b = 0; b < numButtons; b++) {
            int held = gamepad_button_held(pad, b);
            pressBase[b] = held ? 0x80 : 0x00;
            if (held) {
                short pat = (b < JOY_SLOT_CFG_WORDS) ? perSlotWords[b] : g_joystickConfigWords[b];
                bits |= (unsigned short)pat;
            }
        }

        joySlotState[i] = bits;
    }

    for (int i = count; i < maxSlots; i++) {
        joySlotState[i] = 0;
        memset(&g_keyPressState[i * JOY_BUTTONS_PER_SLOT], 0, JOY_BUTTONS_PER_SLOT);
    }

    return count;
}

uint32_t platform_get_time_ms(void)
{
    return SDL_GetTicks();
}

void platform_sleep_ms(int ms)
{
    SDL_Delay(ms);
}

int platform_audio_init(void)
{
    return 0;
}

void platform_audio_shutdown(void)
{
}

void platform_gl_swap(void)
{
    if (g_sdlWindow) {
        SDL_GL_SwapWindow(g_sdlWindow);
    }
}

void platform_get_drawable_size(int *w, int *h)
{
    if (g_sdlWindow) {
        SDL_GL_GetDrawableSize(g_sdlWindow, w, h);
    } else {
        *w = 1280;
        *h = 720;
    }
}

int platform_net_init(void)
{
    return 0;
}

void platform_net_shutdown(void)
{
}

int platform_net_is_modem(void)
{
    return 0;
}

int platform_get_region(void)
{
    return 0;
}
