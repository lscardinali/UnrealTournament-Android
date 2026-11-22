/*
  UTAndroid additions to SDL3: a gamepad and sound from the Android app, for Unreal Tournament 469 (SDLDrv).

  The game runs as a glibc process that can't load Android's libraries, so input and sound arrive over Unix
  sockets from the app. Derived from Pocket Foundry's SDL_pocketfoundry.c (zlib licence, (C) 2026 Pocket Foundry
  contributors); released under the zlib licence, like SDL.

  Input socket ($UTANDROID_INPUT): 4-byte messages, little-endian:
      app -> game  'B' <button 0..15> <int16 down>      'A' <axis 0..3> <int16 value>
      game -> app  'R' 0 <uint16 rumble strength>       'M' 0 <int16 1 = menu cursor mode, 0 = game mode>
  The app shapes the pad (radial dead zone, triggers as buttons) before sending.

  Joystick layout seen by SDLDrv (raw SDL_Joystick API, button i -> Joy(i+1), axes -> JoyX/JoyY/JoyZ/JoyR):
      axes:    0 LX, 1 LY, 2 RX, 3 RY
      buttons: 0 A, 1 B, 2 X, 3 Y, 4 L1, 5 R1, 6 L2, 7 R2, 8 Select, 9 Start, 10 L3, 11 R3,
               12 DUp, 13 DDown, 14 DLeft, 15 DRight
  No hats: SDLDrv would turn a hat into Joy13..16 and clamp buttons to 12.

  Start is the Escape key (in game it opens the menu, in menus it closes it, in the intro it skips to the menu), not a
  joystick button.

  Menus: while the game has relative mouse mode off (menus, console) the pad drives UWindow:
    sticks = mouse cursor, Y = left click, X = right click, B = Escape,
    D-pad = arrow keys (auto-repeat), A = Enter, R1 = Tab, L1 = Space, Select = Alt+G (menu bar "Game").
  On entering the menus the "Game" pulldown is opened once (Alt+G after UWindow had time to build its root), so
  the D-pad has something focused right away.
  UWindow already supports keys: arrows/Enter in the menu bar and pulldowns, Tab/Space/arrows in dialogs.
  The Joy buttons still reach UWindow in menus (PadUI uses them for key binding and swallows them otherwise).

  Native-pad mode ($UTANDROID_NATIVE_PAD, the in-process Android build): no socket. The virtual pad is attached
  at joystick init and each poll reads the real pad through SDL's gamepad API, shaped here (radial dead zone,
  triggers as buttons). The menu cursor moves with SDL mouse motion (Android has no pointer to warp); its position,
  the menu state and the frame count live in a struct the loader shares ($UTANDROID_SHARED, an address).

  Audio socket ($UTANDROID_AUDIO): 12-byte header ("UTA1", rate, channels as LE int32), then F32LE frames.
*/
#include "SDL_internal.h"

#ifdef SDL_UTANDROID

#include "SDL_utandroid.h"
#include "../audio/SDL_sysaudio.h"
#include "../joystick/SDL_joystick_c.h"
#include "../events/SDL_mouse_c.h"
#include "../events/SDL_keyboard_c.h"

#include <dlfcn.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define AUDIO_MAGIC "UTA1"
#define RECONNECT_MS 1000

#define UTA_NBUTTONS 16
#define UTA_NAXES 6 /* 0,1 left stick, 2,3 right stick, 4,5 gyro (JoyU turn, JoyV look up) */
#define UTA_AXIS_GYRO_FIRST 4
#define UTA_BUTTON_A 0
#define UTA_BUTTON_B 1
#define UTA_BUTTON_X 2
#define UTA_BUTTON_START 9

/*
 * UT adds a joystick axis to the view rotation once per frame without scaling by the frame time (it treats the
 * stick like mouse deltas), so look speed would grow with the frame rate. We scale the look axes by
 * frame time / REFERENCE_FRAME, making the Speed values in User.ini mean "at 60 fps" at any frame rate.
 */
#define REFERENCE_FRAME_S (1.0f / 60.0f)
#define LOOK_SCALE_MIN 0.25f
#define LOOK_SCALE_MAX 2.0f
#define UTA_AXIS_LOOK_FIRST 2 /* axes 2,3 = right stick (JoyZ turn, JoyR look up) */
/* Look axes are sent at half range (JoyZ/JoyR Speed doubled in GameConfig.kt) so the turn boost has room above
   full deflection. */
#define LOOK_AXIS_RANGE 0.5f
#define TURN_BOOST_DELAY_S 0.25f /* full yaw held this long before the boost starts */
#define TURN_BOOST_RAMP_S 0.35f  /* then it ramps to 1 + turn_boost over this long */

/*
 * Gyro axes carry the angle turned during one game frame. UT turns the view by 32 * Speed * axis per frame
 * (axis -0.5..+0.5 from SDLDrv, 65536 units per turn), so with JoyU/JoyV bound at Speed=GYRO_AXIS_SPEED a full axis
 * is 16 * GYRO_AXIS_SPEED units (~88 degrees) per frame. Must match GameConfig.kt.
 */
#define GYRO_AXIS_SPEED 1000.0f
#define GYRO_UNITS_PER_URAD (65536.0f / (2.0f * SDL_PI_F) / 1e6f)
#define GYRO_AXIS_PER_URAD (GYRO_UNITS_PER_URAD / (16.0f * GYRO_AXIS_SPEED) * 32767.0f)

#define CURSOR_SPEED 1400.0f /* pixels per second at full deflection, for a 1080-line window */
#define CURSOR_DEADZONE 0.15f

static int UTANDROID_Connect(const char *path)
{
    struct sockaddr_un addr;
    int fd;

    if (!path || SDL_strlen(path) >= sizeof(addr.sun_path)) {
        return -1;
    }
    SDL_zero(addr);
    addr.sun_family = AF_UNIX;
    SDL_strlcpy(addr.sun_path, path, sizeof(addr.sun_path));

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool UTANDROID_SendAll(int fd, const void *data, size_t len)
{
    const Uint8 *p = (const Uint8 *)data;
    while (len > 0) {
        const ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        p += n;
        len -= (size_t)n;
    }
    return true;
}

static bool UTANDROID_RecvAll(int fd, void *data, size_t len)
{
    Uint8 *p = (Uint8 *)data;
    while (len > 0) {
        const ssize_t n = recv(fd, p, len, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        p += n;
        len -= (size_t)n;
    }
    return true;
}

/* ---------------------------------------------------------------------------------------------- audio */

struct SDL_PrivateAudioData
{
    int fd;
    Uint8 *mixbuf;
};

static bool UTANDROIDAUDIO_WaitDevice(SDL_AudioDevice *device)
{
    return true; /* PlayDevice blocks until the app's AudioTrack has room: that is the playback clock */
}

static bool UTANDROIDAUDIO_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buffer_size)
{
    return UTANDROID_SendAll(device->hidden->fd, buffer, (size_t)buffer_size);
}

static Uint8 *UTANDROIDAUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    return device->hidden->mixbuf;
}

static void UTANDROIDAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    if (device->hidden) {
        if (device->hidden->fd >= 0) {
            close(device->hidden->fd);
        }
        SDL_free(device->hidden->mixbuf);
        SDL_free(device->hidden);
        device->hidden = NULL;
    }
}

static bool UTANDROIDAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    const char *path = SDL_getenv("UTANDROID_AUDIO");
    Uint8 header[12];
    int sndbuf;

    device->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) {
        return false;
    }
    device->hidden->fd = UTANDROID_Connect(path);
    if (device->hidden->fd < 0) {
        return SDL_SetError("UTAndroid audio: can't connect to %s", path ? path : "(UTANDROID_AUDIO unset)");
    }

    /* Android's native mix format; SDL converts whatever the game plays. */
    device->spec.format = SDL_AUDIO_F32LE;
    device->spec.channels = 2;
    device->spec.freq = 48000;
    SDL_UpdatedAudioDeviceFormat(device);

    sndbuf = device->buffer_size * 2; /* small: keeps sound in sync with the game */
    setsockopt(device->hidden->fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    SDL_memcpy(header, AUDIO_MAGIC, 4);
    SDL_memcpy(header + 4, &device->spec.freq, 4);
    SDL_memcpy(header + 8, &device->spec.channels, 4);
    if (!UTANDROID_SendAll(device->hidden->fd, header, sizeof(header))) {
        return SDL_SetError("UTAndroid audio: the app closed the connection");
    }

    device->hidden->mixbuf = (Uint8 *)SDL_malloc(device->buffer_size);
    if (!device->hidden->mixbuf) {
        return false;
    }
    SDL_memset(device->hidden->mixbuf, device->silence_value, device->buffer_size);
    return true;
}

static void UTANDROIDAUDIO_DetectDevices(SDL_AudioDevice **default_playback, SDL_AudioDevice **default_recording)
{
    *default_playback = SDL_AddAudioDevice(false, DEFAULT_PLAYBACK_DEVNAME, NULL, (void *)0x1);
}

static bool UTANDROIDAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = UTANDROIDAUDIO_OpenDevice;
    impl->WaitDevice = UTANDROIDAUDIO_WaitDevice;
    impl->PlayDevice = UTANDROIDAUDIO_PlayDevice;
    impl->GetDeviceBuf = UTANDROIDAUDIO_GetDeviceBuf;
    impl->CloseDevice = UTANDROIDAUDIO_CloseDevice;
    impl->DetectDevices = UTANDROIDAUDIO_DetectDevices;
    return true;
}

AudioBootStrap UTANDROIDAUDIO_bootstrap = {
    .name = "utandroid",
    .desc = "UTAndroid (Android app)",
    .init = UTANDROIDAUDIO_Init,
    .demand_only = true, /* the app sets SDL_AUDIO_DRIVER=utandroid */
};

/* ---------------------------------------------------------------------------------------------- gamepad */

static SDL_Thread *input_thread;
static SDL_Semaphore *input_wakeup;
static SDL_AtomicInt input_quitting;
static SDL_AtomicInt input_fd; /* -1 while unplugged; changed under the joystick lock */

/* Latest pad state from the socket thread; applied on the main thread in UTANDROID_Update. */
static SDL_AtomicInt pad_buttons;          /* bit i = button i */
static SDL_AtomicInt pad_received;         /* messages received from the app (diagnostics) */
static SDL_AtomicInt pad_pressed;          /* presses not yet seen by a frame: a tap shorter than a frame still counts */
static SDL_AtomicInt pad_axes[UTA_NAXES];  /* Sint16 values */
static SDL_Joystick *pad_joystick;         /* main-thread/joystick-lock only */

/* Native-pad mode (Android, in-process): shared with libutloader, which counts frames and lets the app draw the
   cursor. Layout must match native/utloader/main_android.c. */
typedef struct {
    volatile Uint32 frames;
    volatile Sint32 menu;
    volatile float cursor_x, cursor_y;
    volatile Sint32 window_w, window_h;
    volatile Uint32 inject_buttons; /* debug: buttons injected by the app (adb), OR-ed into the real pad */
    volatile Uint32 inject_pressed;
    volatile Sint32 touch_dx, touch_dy; /* touchpad cursor movement from the app, window pixels */
    volatile Uint32 touch_taps;         /* touchpad taps = left clicks */
    volatile Sint32 gyro_yaw, gyro_pitch; /* gyro angle from the app, micro-radians, drained once per game frame */
    volatile Sint32 perf_overlay;         /* (app) */
    volatile float stick_curve;           /* look stick response exponent, 1 = linear */
    volatile float turn_boost;            /* extra yaw speed after holding full deflection, 0 = off, 1 = 2x */
    volatile Sint32 gyro_on, fps_limit;   /* (app) */
} UTANDROID_Shared;
static UTANDROID_Shared *shared;
static bool native_pad;
static float gyro_axis[2]; /* this frame's gyro axis values (main thread) */
static float turn_boost_factor = 1.0f; /* main thread */
static SDL_JoystickID virtual_id;
static SDL_Gamepad *real_pad;

#define STICK_DEADZONE 0.18f
#define TRIGGER_ON 0.45f
#define TRIGGER_OFF 0.30f

/* Menu cursor state (main thread only). */
static bool menu_mode;
static Uint32 menu_prev_buttons;
static Uint64 menu_last_ns;
static float cursor_x = -1.0f, cursor_y = -1.0f;

static void UTANDROID_ResetPad(void);

static void UTANDROID_SendToApp(Uint8 type, Sint16 value)
{
    const int fd = SDL_GetAtomicInt(&input_fd);
    const Uint8 message[4] = { type, 0, (Uint8)(value & 0xFF), (Uint8)((Uint16)value >> 8) };
    if (fd >= 0) {
        send(fd, message, sizeof(message), MSG_NOSIGNAL | MSG_DONTWAIT);
    }
}

static bool SDLCALL UTANDROID_Rumble(void *userdata, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble)
{
    const Uint16 strength = SDL_max(low_frequency_rumble, high_frequency_rumble) >> 1;
    if (native_pad) {
        return real_pad ? SDL_RumbleGamepad(real_pad, low_frequency_rumble, high_frequency_rumble, 2000) : true;
    }
    if (SDL_GetAtomicInt(&input_fd) < 0) {
        return SDL_SetError("UTAndroid gamepad is unplugged");
    }
    UTANDROID_SendToApp('R', (Sint16)strength);
    return true;
}

static SDL_Window *UTANDROID_GameWindow(void)
{
    SDL_Window *window = SDL_GetKeyboardFocus();
    if (!window) {
        window = SDL_GetMouseFocus();
    }
    if (!window) {
        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        if (windows) {
            if (count > 0) {
                window = windows[0];
            }
            SDL_free(windows);
        }
    }
    return window;
}

static float UTANDROID_Stick(int axis)
{
    float v = (float)(Sint16)SDL_GetAtomicInt(&pad_axes[axis]) / 32767.0f;
    return SDL_clamp(v, -1.0f, 1.0f);
}

static void UTANDROID_Key(SDL_Scancode scancode, bool down)
{
    SDL_SendKeyboardKey(0, SDL_DEFAULT_KEYBOARD_ID, 0, scancode, down);
}

/* X11: warp the real X pointer (the X server draws UT's system cursor there and reports the motion back).
   Android: there is no pointer to warp: move SDL's mouse; the app draws the cursor from the shared state. */
static void UTANDROID_MoveCursor(SDL_Window *window, float x, float y)
{
#ifdef SDL_PLATFORM_ANDROID
    SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, false, x, y);
#else
    SDL_WarpMouseInWindow(window, x, y);
#endif
    if (shared) {
        shared->cursor_x = x;
        shared->cursor_y = y;
    }
}

static Sint16 UTANDROID_Shape(float x, float y, Sint16 *out_y)
{
    const float mag = SDL_sqrtf(x * x + y * y);
    float sx = 0.0f, sy = 0.0f;
    if (mag > STICK_DEADZONE) {
        const float scaled = SDL_min((mag - STICK_DEADZONE) / (1.0f - STICK_DEADZONE), 1.0f);
        sx = x / mag * scaled;
        sy = y / mag * scaled;
    }
    *out_y = (Sint16)(SDL_clamp(sy, -1.0f, 1.0f) * 32767.0f);
    return (Sint16)(SDL_clamp(sx, -1.0f, 1.0f) * 32767.0f);
}

/* Native-pad mode: read the real pad (first gamepad that isn't ours) into the same state the socket filled. */
static void UTANDROID_PollRealPad(void)
{
    static const SDL_GamepadButton map[UTA_NBUTTONS] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_INVALID, /* L2/R2: from the trigger axes */
        SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    };
    static bool trigger_l, trigger_r;
    Uint32 buttons = 0;
    Sint16 x, y;
    int i;

    /* Debug injection (adb input gamepad via NativeGameActivity): a tap shorter than a poll still counts. */
    if (shared) {
        const Uint32 tapped = __atomic_exchange_n(&shared->inject_pressed, 0u, __ATOMIC_SEQ_CST);
        if (tapped) {
            Uint32 old;
            do {
                old = (Uint32)SDL_GetAtomicInt(&pad_pressed);
            } while (!SDL_CompareAndSwapAtomicInt(&pad_pressed, (int)old, (int)(old | tapped)));
        }
    }
    if (real_pad && !SDL_GamepadConnected(real_pad)) {
        SDL_CloseGamepad(real_pad);
        real_pad = NULL;
    }
    if (!real_pad) {
        int n = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&n);
        for (i = 0; ids && i < n && !real_pad; i++) {
            if (ids[i] != virtual_id) {
                real_pad = SDL_OpenGamepad(ids[i]);
                if (real_pad) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "UTAndroid: using pad \"%s\"", SDL_GetGamepadName(real_pad));
                }
            }
        }
        SDL_free(ids);
        if (!real_pad) {
            UTANDROID_ResetPad();
            return;
        }
    }

    for (i = 0; i < UTA_NBUTTONS; i++) {
        if (map[i] != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(real_pad, map[i])) {
            buttons |= 1u << i;
        }
    }
    {
        const float lt = SDL_GetGamepadAxis(real_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
        const float rt = SDL_GetGamepadAxis(real_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f;
        trigger_l = trigger_l ? lt > TRIGGER_OFF : lt > TRIGGER_ON;
        trigger_r = trigger_r ? rt > TRIGGER_OFF : rt > TRIGGER_ON;
        if (trigger_l) buttons |= 1u << 6;
        if (trigger_r) buttons |= 1u << 7;
    }
    if (shared) {
        buttons |= shared->inject_buttons;
    }
    SDL_SetAtomicInt(&pad_buttons, (int)buttons);

    x = UTANDROID_Shape(SDL_GetGamepadAxis(real_pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f,
                        SDL_GetGamepadAxis(real_pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f, &y);
    SDL_SetAtomicInt(&pad_axes[0], x);
    SDL_SetAtomicInt(&pad_axes[1], y);
    x = UTANDROID_Shape(SDL_GetGamepadAxis(real_pad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f,
                        SDL_GetGamepadAxis(real_pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f, &y);
    /* Look stick response curve: magnitude^curve keeps full speed at full deflection, finer control near the centre. */
    if (shared && shared->stick_curve > 1.01f) {
        const float fx = x / 32767.0f, fy = y / 32767.0f;
        const float mag = SDL_sqrtf(fx * fx + fy * fy);
        if (mag > 0.0f) {
            const float k = SDL_powf(SDL_min(mag, 1.0f), shared->stick_curve) / mag;
            x = (Sint16)(fx * k * 32767.0f);
            y = (Sint16)(fy * k * 32767.0f);
        }
    }
    SDL_SetAtomicInt(&pad_axes[2], x);
    SDL_SetAtomicInt(&pad_axes[3], y);
}

/* Menu keys: D-pad arrows (with auto-repeat), Y Enter, R1 Tab, L1 Space, Select Alt+G. */
#define KEY_REPEAT_DELAY_NS 400000000ull
#define KEY_REPEAT_RATE_NS 90000000ull

#define MIN_PRESS_NS 50000000ull
#define MENU_FOCUS_DELAY_NS 250000000ull
static Uint64 menu_focus_at; /* when to open the menu bar after entering the menus (0 = done) */

static void UTANDROID_OpenMenuBar(void)
{
    UTANDROID_Key(SDL_SCANCODE_LALT, true);
    UTANDROID_Key(SDL_SCANCODE_G, true);
    UTANDROID_Key(SDL_SCANCODE_G, false);
    UTANDROID_Key(SDL_SCANCODE_LALT, false);
}

static void UTANDROID_MenuKeys(Uint32 buttons, Uint32 pressed, Uint32 released)
{
    static const struct { int button; SDL_Scancode key; bool repeat; } map[] = {
        { 12, SDL_SCANCODE_UP, true }, { 13, SDL_SCANCODE_DOWN, true },
        { 14, SDL_SCANCODE_LEFT, true }, { 15, SDL_SCANCODE_RIGHT, true },
        { UTA_BUTTON_A, SDL_SCANCODE_RETURN, false }, { 5, SDL_SCANCODE_TAB, false }, { 4, SDL_SCANCODE_SPACE, false },
    };
    static Uint64 next_repeat[16];
    const Uint64 now = SDL_GetTicksNS();
    size_t i;

    for (i = 0; i < SDL_arraysize(map); i++) {
        const Uint32 bit = 1u << map[i].button;
        if (pressed & bit) {
            UTANDROID_Key(map[i].key, true);
            next_repeat[map[i].button] = now + KEY_REPEAT_DELAY_NS;
        } else if (released & bit) {
            UTANDROID_Key(map[i].key, false);
        } else if (map[i].repeat && (buttons & bit) && now >= next_repeat[map[i].button]) {
            UTANDROID_Key(map[i].key, false); /* a fresh press for UWindow, which acts on key down */
            UTANDROID_Key(map[i].key, true);
            next_repeat[map[i].button] = now + KEY_REPEAT_RATE_NS;
        }
    }
    /* Select: Alt+G opens the menu bar's Game menu (then the D-pad walks the bar and its pulldowns). */
    if (pressed & (1u << 8)) {
        UTANDROID_OpenMenuBar();
        menu_focus_at = 0;
    }
    /* Just entered the menus: focus the menu bar once, after UWindow had a moment to create its root window. */
    if (menu_focus_at && now >= menu_focus_at) {
        menu_focus_at = 0;
        UTANDROID_OpenMenuBar();
    }
}

static Uint64 click_up_at; /* touchpad tap: when to release the left button */

static void UTANDROID_MenuCursor(SDL_Window *window, Uint32 buttons)
{
    const Uint64 now = SDL_GetTicksNS();
    const float dt = menu_last_ns ? SDL_min((float)(now - menu_last_ns) / 1e9f, 0.1f) : 0.0f;
    const Uint32 pressed = buttons & ~menu_prev_buttons;
    const Uint32 released = ~buttons & menu_prev_buttons;
    int w = 0, h = 0;
    float dx, dy, mag;

    menu_last_ns = now;
    SDL_GetWindowSize(window, &w, &h);
    if (shared) {
        shared->window_w = w;
        shared->window_h = h;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    if (cursor_x < 0.0f) {
        SDL_GetMouseState(&cursor_x, &cursor_y);
        if (cursor_x <= 0.0f && cursor_y <= 0.0f) {
            cursor_x = w * 0.5f;
            cursor_y = h * 0.5f;
        }
        /* Show the cursor where it is as soon as the menu opens, not only after the stick moves. */
        UTANDROID_MoveCursor(window, cursor_x, cursor_y);
    }

    /* Native build: the cursor is a touchpad fallback driven by the app (relative finger moves, taps click). The pad
       drives the menus through keys and PadUI, so the sticks and face buttons no longer move or click it. */
    if (native_pad) {
        if (shared) {
            const int tdx = __atomic_exchange_n(&shared->touch_dx, 0, __ATOMIC_SEQ_CST);
            const int tdy = __atomic_exchange_n(&shared->touch_dy, 0, __ATOMIC_SEQ_CST);
            Uint32 taps = __atomic_exchange_n(&shared->touch_taps, 0u, __ATOMIC_SEQ_CST);
            if (tdx || tdy) {
                cursor_x = SDL_clamp(cursor_x + (float)tdx, 0.0f, (float)(w - 1));
                cursor_y = SDL_clamp(cursor_y + (float)tdy, 0.0f, (float)(h - 1));
                UTANDROID_MoveCursor(window, cursor_x, cursor_y);
            }
            /* A tap is a click held for MIN_PRESS_NS: down and up in the same frame don't open menus. */
            if (click_up_at && now >= click_up_at) {
                SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, SDL_BUTTON_LEFT, false);
                click_up_at = 0;
            }
            if (taps && !click_up_at) {
                SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, SDL_BUTTON_LEFT, true);
                click_up_at = now + MIN_PRESS_NS;
            }
        }
        if (pressed & (1u << UTA_BUTTON_B)) {
            UTANDROID_Key(SDL_SCANCODE_ESCAPE, true);
        }
        if (released & (1u << UTA_BUTTON_B)) {
            UTANDROID_Key(SDL_SCANCODE_ESCAPE, false);
        }
        UTANDROID_MenuKeys(buttons, pressed, released);
        return;
    }

    /* X11 build: either stick moves the cursor; the stronger one wins. Quadratic response for precision. */
    dx = UTANDROID_Stick(2);
    dy = UTANDROID_Stick(3);
    if (SDL_fabsf(UTANDROID_Stick(0)) + SDL_fabsf(UTANDROID_Stick(1)) > SDL_fabsf(dx) + SDL_fabsf(dy)) {
        dx = UTANDROID_Stick(0);
        dy = UTANDROID_Stick(1);
    }
    mag = SDL_sqrtf(dx * dx + dy * dy);
    if (mag > CURSOR_DEADZONE && dt > 0.0f) {
        const float scale = CURSOR_SPEED * (h / 1080.0f) * dt * mag; /* mag * unit vector * mag = quadratic */
        cursor_x = SDL_clamp(cursor_x + dx * scale, 0.0f, (float)(w - 1));
        cursor_y = SDL_clamp(cursor_y + dy * scale, 0.0f, (float)(h - 1));
        UTANDROID_MoveCursor(window, cursor_x, cursor_y);
    }

    if (pressed & (1u << 3 /* Y */)) {
        SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, SDL_BUTTON_LEFT, true);
    }
    if (released & (1u << 3 /* Y */)) {
        SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, SDL_BUTTON_LEFT, false);
    }
    if (pressed & (1u << UTA_BUTTON_X)) {
        SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, SDL_BUTTON_RIGHT, true);
    }
    if (released & (1u << UTA_BUTTON_X)) {
        SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, SDL_BUTTON_RIGHT, false);
    }
    if (pressed & (1u << UTA_BUTTON_B)) {
        UTANDROID_Key(SDL_SCANCODE_ESCAPE, true);
    }
    if (released & (1u << UTA_BUTTON_B)) {
        UTANDROID_Key(SDL_SCANCODE_ESCAPE, false);
    }
    UTANDROID_MenuKeys(buttons, pressed, released);
}

/* Main thread, joystick lock held, right before SDL applies the virtual joystick's pending changes. */
static void SDLCALL UTANDROID_Update(void *userdata)
{
    /*
     * Real frame time, from utshim's count of presented frames (SDLDrv polls the joystick twice per frame, so the
     * time between polls is not a frame). Smoothed so one hitch doesn't jerk the view.
     */
    static Uint64 last_poll_ns;
    static int polls_this_second, polls_per_second;
    static Uint32 (*frame_count)(void);
    static bool frame_count_looked_up;
    static Uint32 last_frames;
    static Uint64 last_frames_ns;
    static float frame_s = REFERENCE_FRAME_S;
    const Uint64 poll_ns = SDL_GetTicksNS();
    float look_scale;
    if (!frame_count_looked_up) {
        frame_count_looked_up = true;
        frame_count = (Uint32 (*)(void))dlsym(RTLD_DEFAULT, "utshim_frame_count");
        (void)dlerror(); /* don't leave "undefined symbol" for the game's next dlerror() */
    }
    if (frame_count || shared) {
        const Uint32 frames = shared ? shared->frames : frame_count();
        if (!last_frames_ns) {
            last_frames = frames;
            last_frames_ns = poll_ns;
        } else if (frames != last_frames && poll_ns - last_frames_ns >= 100000000ull) { /* every >= 100 ms */
            const float measured = (float)(poll_ns - last_frames_ns) / 1e9f / (float)(Uint32)(frames - last_frames);
            frame_s += (measured - frame_s) * 0.5f;
            last_frames = frames;
            last_frames_ns = poll_ns;
        }
    }
    look_scale = SDL_clamp(frame_s / REFERENCE_FRAME_S, LOOK_SCALE_MIN, LOOK_SCALE_MAX);
    if (native_pad) {
        UTANDROID_PollRealPad();
    }
    /* Held buttons, plus any pressed and already released since the last frame (seen as down for this frame). */
    const Uint32 buttons = (Uint32)SDL_GetAtomicInt(&pad_buttons) | (Uint32)SDL_SetAtomicInt(&pad_pressed, 0);
    last_poll_ns = poll_ns;
    polls_this_second++;
    SDL_Window *window = UTANDROID_GameWindow();
    const bool menu = window && !SDL_GetWindowRelativeMouseMode(window) && !SDL_getenv("UTANDROID_NO_MENU_CURSOR");
    int i;

    {
        static Uint64 last_beat;
        const Uint64 now_ms = SDL_GetTicks();
        if (now_ms - last_beat > 1000) {
            last_beat = now_ms;
            polls_per_second = polls_this_second;
            polls_this_second = 0;
            {
                static void (*stats)(Uint64 *);
                static bool stats_looked_up;
                Uint64 st[4] = { 0 };
                if (!stats_looked_up) {
                    stats_looked_up = true;
                    stats = (void (*)(Uint64 *))dlsym(RTLD_DEFAULT, "utshim_stats");
                    (void)dlerror();
                }
                if (stats) {
                    stats(st);
                    SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                                "UTAndroid frames: %llu fps, avg %.2f ms, worst %.2f ms, put_image %.2f ms/frame",
                                (unsigned long long)st[0], st[1] / 1e6, st[2] / 1e6, st[3] / 1e6);
                }
            }
            float mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            SDL_LogDebug(SDL_LOG_CATEGORY_INPUT,
                         "UTAndroid update t=%llu: polls/s=%d look_scale=%.2f rx=%d fd=%d buttons=0x%x menu=%d axes=%d,%d,%d,%d mouse=%.0f,%.0f cursor=%d",
                         (unsigned long long)now_ms, polls_per_second, look_scale, SDL_GetAtomicInt(&pad_received), SDL_GetAtomicInt(&input_fd), buttons, menu_mode, SDL_GetAtomicInt(&pad_axes[0]), SDL_GetAtomicInt(&pad_axes[1]),
                         SDL_GetAtomicInt(&pad_axes[2]), SDL_GetAtomicInt(&pad_axes[3]), mx, my, SDL_CursorVisible());
        }
    }

    /*
     * The game window must have SDL's keyboard focus: SDLDrv ignores joystick input for an unfocused viewport, and
     * on Termux:X11 nothing reliably gives the window X input focus (no real WM, no Android key events reach X).
     */
    if (window && !SDL_GetKeyboardFocus()) {
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "UTAndroid: giving the game window keyboard focus");
        SDL_SetKeyboardFocus(window);
    }

    /* Start -> Escape, in every mode. */
    {
        static bool start_down;
        const bool start = (buttons >> UTA_BUTTON_START) & 1;
        if (start != start_down) {
            start_down = start;
            SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "UTAndroid Start %s -> Escape (focus window %p)",
                        start ? "down" : "up", (void *)SDL_GetKeyboardFocus());
            UTANDROID_Key(SDL_SCANCODE_ESCAPE, start);
        }
    }

    if (menu != menu_mode) {
        if (menu_mode) { /* leaving a menu: let go of anything still held */
            UTANDROID_MenuKeys(0, 0, menu_prev_buttons);
        }
        menu_mode = menu;
        menu_focus_at = menu ? SDL_GetTicksNS() + MENU_FOCUS_DELAY_NS : 0;
        menu_prev_buttons = buttons; /* a button held across the switch neither clicks nor releases */
        menu_last_ns = 0;
        cursor_x = cursor_y = -1.0f;
        UTANDROID_SendToApp('M', menu ? 1 : 0);
        if (shared) {
            shared->menu = menu ? 1 : 0;
        }
    }

    if (menu_mode) {
        UTANDROID_MenuCursor(window, buttons);
        menu_prev_buttons = buttons;
    } else if (shared) { /* touchpad input outside the menus does nothing */
        __atomic_exchange_n(&shared->touch_dx, 0, __ATOMIC_SEQ_CST);
        __atomic_exchange_n(&shared->touch_dy, 0, __ATOMIC_SEQ_CST);
        __atomic_exchange_n(&shared->touch_taps, 0u, __ATOMIC_SEQ_CST);
    }

    if (!pad_joystick) {
        return;
    }
    for (i = 0; i < UTA_NBUTTONS; i++) {
        /* Keep every press down for at least MIN_PRESS_NS: the engine samples the virtual pad once per frame, so a tap
           that goes down and up between two samples would never be seen. */
        static Uint64 held_until[UTA_NBUTTONS];
        const Uint64 now_ns = SDL_GetTicksNS();
        bool down = (buttons >> i) & 1;
        if (down && !held_until[i]) {
            held_until[i] = now_ns + MIN_PRESS_NS;
        } else if (!down && held_until[i]) {
            if (now_ns < held_until[i]) {
                down = true;
            } else {
                held_until[i] = 0;
            }
        }
        if (i == UTA_BUTTON_START) {
            down = false; /* sent as Escape above */
        }
        /* In menus the buttons also arrive as keys/clicks (above); the Joy buttons still reach UWindow, where PadUI
           swallows them except while a key-binding button waits for its key. */
        SDL_SetJoystickVirtualButton(pad_joystick, i, down);
    }
    /* Turn boost: holding the look stick fully sideways speeds the turn up after a moment (fast 180s). */
    {
        static Uint64 full_since;
        const float yaw = SDL_fabsf((float)(Sint16)SDL_GetAtomicInt(&pad_axes[UTA_AXIS_LOOK_FIRST]) / 32767.0f);
        const float boost = shared ? shared->turn_boost : 0.0f;
        turn_boost_factor = 1.0f;
        if (yaw > 0.92f && boost > 0.0f && !menu_mode) {
            float held;
            if (!full_since) full_since = poll_ns;
            held = (float)(poll_ns - full_since) / 1e9f - TURN_BOOST_DELAY_S;
            if (held > 0.0f) {
                turn_boost_factor = 1.0f + boost * SDL_min(held / TURN_BOOST_RAMP_S, 1.0f);
            }
        } else {
            full_since = 0;
        }
    }
    /* Gyro: take the angle accumulated since the last game frame, once per frame (SDLDrv polls twice per frame),
       and hold it on the gyro axes for that frame. Discarded in menus. */
    if (shared) {
        static Uint32 gyro_frame;
        const Uint32 f = shared->frames;
        if (f != gyro_frame) {
            const int yaw = __atomic_exchange_n(&shared->gyro_yaw, 0, __ATOMIC_SEQ_CST);
            const int pitch = __atomic_exchange_n(&shared->gyro_pitch, 0, __ATOMIC_SEQ_CST);
            gyro_frame = f;
            gyro_axis[0] = menu_mode ? 0.0f : (float)yaw * GYRO_AXIS_PER_URAD;
            gyro_axis[1] = menu_mode ? 0.0f : (float)pitch * GYRO_AXIS_PER_URAD;
        }
    }
    for (i = 0; i < UTA_NAXES; i++) {
        float v = (float)(Sint16)SDL_GetAtomicInt(&pad_axes[i]);
        if (i >= UTA_AXIS_GYRO_FIRST) {
            v = gyro_axis[i - UTA_AXIS_GYRO_FIRST]; /* already an angle per frame: no look_scale */
        } else if (i >= UTA_AXIS_LOOK_FIRST) {
            v *= look_scale * LOOK_AXIS_RANGE; /* frame-rate independent look (see REFERENCE_FRAME_S) */
            if (i == UTA_AXIS_LOOK_FIRST) {
                v *= turn_boost_factor;
            }
        }
        /* In menus, centre the sticks so the game doesn't turn/move behind the menu. */
        SDL_SetJoystickVirtualAxis(pad_joystick, i, menu_mode ? 0 : (Sint16)SDL_clamp(v, -32767.0f, 32767.0f));
    }
}

static void UTANDROID_ResetPad(void)
{
    int i;
    SDL_SetAtomicInt(&pad_buttons, 0);
    SDL_SetAtomicInt(&pad_pressed, 0);
    for (i = 0; i < UTA_NAXES; i++) {
        SDL_SetAtomicInt(&pad_axes[i], 0);
    }
}

/* Attaches and opens the virtual pad for a connected socket. Returns its id (0 on failure). */
static SDL_JoystickID UTANDROID_AttachPad(int fd)
{
    SDL_VirtualJoystickDesc desc;
    SDL_JoystickID id;
    SDL_Joystick *joystick;

    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = 0x045e; /* Xbox Series controller */
    desc.product_id = 0x0b12;
    desc.naxes = UTA_NAXES;
    desc.nbuttons = UTA_NBUTTONS;
    desc.nhats = 0;
    desc.name = "UTAndroid controller";
    desc.Update = UTANDROID_Update;
    desc.Rumble = UTANDROID_Rumble;

    UTANDROID_ResetPad();
    id = SDL_AttachVirtualJoystick(&desc);
    if (!id) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "UTAndroid gamepad: attach failed: %s", SDL_GetError());
        return 0;
    }
    joystick = SDL_OpenJoystick(id);
    if (!joystick) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "UTAndroid gamepad: open failed: %s", SDL_GetError());
        SDL_DetachVirtualJoystick(id);
        return 0;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "UTAndroid gamepad plugged in");
    SDL_LockJoysticks();
    pad_joystick = joystick;
    SDL_SetAtomicInt(&input_fd, fd);
    SDL_UnlockJoysticks();
    return id;
}

/* Socket thread: feeds the attached pad until the app or SDL hangs up, then detaches it. */
static void UTANDROID_PadLoop(int fd, SDL_JoystickID id)
{
    Uint8 message[4];
    SDL_Joystick *joystick = pad_joystick;

    while (!SDL_GetAtomicInt(&input_quitting) && UTANDROID_RecvAll(fd, message, sizeof(message))) {
        const Sint16 value = (Sint16)(message[2] | (message[3] << 8));
        SDL_AddAtomicInt(&pad_received, 1);
        if (message[0] == 'B' && message[1] < UTA_NBUTTONS) {
            const Uint32 bit = 1u << message[1];
            Uint32 old, now;
            do {
                old = (Uint32)SDL_GetAtomicInt(&pad_buttons);
                now = value ? (old | bit) : (old & ~bit);
            } while (!SDL_CompareAndSwapAtomicInt(&pad_buttons, (int)old, (int)now));
            if (value) {
                do {
                    old = (Uint32)SDL_GetAtomicInt(&pad_pressed);
                } while (!SDL_CompareAndSwapAtomicInt(&pad_pressed, (int)old, (int)(old | bit)));
            }
        } else if (message[0] == 'A' && message[1] < UTA_NAXES) {
            SDL_SetAtomicInt(&pad_axes[message[1]], value);
        }
    }

    SDL_LockJoysticks();
    SDL_SetAtomicInt(&input_fd, -1);
    pad_joystick = NULL;
    SDL_UnlockJoysticks();
    UTANDROID_ResetPad();
    SDL_CloseJoystick(joystick);
    SDL_DetachVirtualJoystick(id);
    SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "UTAndroid gamepad unplugged (%s)",
                SDL_GetAtomicInt(&input_quitting) ? "SDL quit joysticks" : "socket closed");
}

struct UTANDROID_ThreadStart
{
    char *path;
    int fd;             /* already connected and attached by StartInput, or -1 */
    SDL_JoystickID id;
};

static int SDLCALL UTANDROID_InputThread(void *data)
{
    struct UTANDROID_ThreadStart *start = (struct UTANDROID_ThreadStart *)data;

    if (start->fd >= 0) {
        UTANDROID_PadLoop(start->fd, start->id);
        close(start->fd);
    }
    /* Reconnects later are hot-plugs: SDLDrv won't see them, but SDL-aware code would. */
    while (!SDL_GetAtomicInt(&input_quitting)) {
        const int fd = UTANDROID_Connect(start->path);
        const SDL_JoystickID id = fd >= 0 ? UTANDROID_AttachPad(fd) : 0;
        if (id) {
            UTANDROID_PadLoop(fd, id);
        }
        if (fd >= 0) {
            close(fd);
        }
        if (!id) {
            SDL_WaitSemaphoreTimeout(input_wakeup, RECONNECT_MS);
        }
    }
    SDL_free(start->path);
    SDL_free(start);
    return 0;
}

/*
 * Called right after SDL_InitJoysticks (joystick lock released). UT's SDLDrv enumerates joysticks once at startup
 * and never handles hot-plug, so the pad must exist before SDL_Init returns: connect and attach synchronously,
 * waiting briefly for the app's socket.
 */
void SDL_UTANDROID_StartInput(void)
{
    const char *path = SDL_getenv("UTANDROID_INPUT");
    struct UTANDROID_ThreadStart *start;
    int tries;

    if (SDL_getenv("UTANDROID_NATIVE_PAD") && !native_pad) {
        const char *addr = SDL_getenv("UTANDROID_SHARED");
        native_pad = true;
        shared = addr ? (UTANDROID_Shared *)(uintptr_t)SDL_strtoull(addr, NULL, 0) : NULL;
        SDL_SetAtomicInt(&input_fd, -1);
        virtual_id = UTANDROID_AttachPad(-1);
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "UTAndroid native pad mode: virtual pad %u, shared %p", virtual_id, (void *)shared);
        return;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "UTAndroid StartInput (path %s, thread %p)", path ? path : "-", (void *)input_thread);
    if (!path || input_thread) {
        return;
    }
    SDL_SetAtomicInt(&input_quitting, 0);
    SDL_SetAtomicInt(&input_fd, -1);
    input_wakeup = SDL_CreateSemaphore(0);

    start = (struct UTANDROID_ThreadStart *)SDL_calloc(1, sizeof(*start));
    if (!start) {
        return;
    }
    start->path = SDL_strdup(path);
    start->fd = -1;
    for (tries = 0; tries < 20 && start->fd < 0; tries++) { /* up to ~1 s */
        start->fd = UTANDROID_Connect(path);
        if (start->fd < 0) {
            SDL_Delay(50);
        }
    }
    if (start->fd >= 0) {
        start->id = UTANDROID_AttachPad(start->fd);
        if (!start->id) {
            close(start->fd);
            start->fd = -1;
        }
    }
    input_thread = SDL_CreateThread(UTANDROID_InputThread, "UTAndroidInput", start);
    {
        int count = 0;
        SDL_free(SDL_GetJoysticks(&count));
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "UTAndroid StartInput done: %d joystick(s)", count);
    }
}

void SDL_UTANDROID_StopInput(void)
{
    int fd;

    SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "UTAndroid StopInput");
    if (native_pad) {
        SDL_LockJoysticks();
        if (pad_joystick) {
            SDL_CloseJoystick(pad_joystick);
            pad_joystick = NULL;
        }
        if (virtual_id) {
            SDL_DetachVirtualJoystick(virtual_id);
            virtual_id = 0;
        }
        if (real_pad) {
            SDL_CloseGamepad(real_pad);
            real_pad = NULL;
        }
        native_pad = false;
        SDL_UnlockJoysticks();
        return;
    }
    if (!input_thread) {
        return;
    }
    SDL_SetAtomicInt(&input_quitting, 1);
    SDL_SignalSemaphore(input_wakeup);
    SDL_LockJoysticks();
    fd = SDL_GetAtomicInt(&input_fd);
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR); /* wakes the thread's recv() */
    }
    SDL_UnlockJoysticks();
    SDL_WaitThread(input_thread, NULL);
    input_thread = NULL;
    SDL_DestroySemaphore(input_wakeup);
    input_wakeup = NULL;
}

#endif /* SDL_UTANDROID */
