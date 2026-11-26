/*
 * Android SDL entry point for the real The Movies host.
 *
 * Game files are supplied by the user and must be copied to:
 *   <SDL internal files directory>/game/Movies.exe
 *   <SDL internal files directory>/game/Data/
 *
 * User saves and the synthetic Windows system root live beside the game
 * directory, not inside it. No game data are packaged in the APK.
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_system.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "tm_host.h"

static int tm_mkdirs(const char *path) {
    char buffer[PATH_MAX];
    size_t length = strlen(path);
    if (!length || length >= sizeof(buffer)) return 0;
    memcpy(buffer, path, length + 1);
    for (char *p = buffer + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buffer, 0700) != 0 && errno != EEXIST) return 0;
            *p = '/';
        }
    }
    return mkdir(buffer, 0700) == 0 || errno == EEXIST;
}

static int tm_is_directory(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void tm_android_redirect_stdio(const char *root) {
    char log_dir[PATH_MAX], log_file[PATH_MAX];
    if (snprintf(log_dir, sizeof(log_dir), "%s/logs", root) >= (int)sizeof(log_dir) ||
        snprintf(log_file, sizeof(log_file), "%s/native.log", log_dir) >= (int)sizeof(log_file)) {
        return;
    }
    if (!tm_mkdirs(log_dir)) return;
    if (freopen(log_file, "a", stdout)) setvbuf(stdout, NULL, _IOLBF, 0);
    if (freopen(log_file, "a", stderr)) setvbuf(stderr, NULL, _IOLBF, 0);
}

int SDL_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    SDL_SetMainReady();

    /* Make Android taps explicitly available to the Win32 mouse/DirectInput
     * bridge. Keep mouse-to-touch synthesis disabled to avoid feedback loops.
     * The game is a fixed-landscape D3D9 title; do not let SDL request portrait. */
    SDL_SetHint("SDL_TOUCH_MOUSE_EVENTS", "0");
    SDL_SetHint("SDL_MOUSE_TOUCH_EVENTS", "0");
    SDL_SetHint("SDL_IOS_ORIENTATIONS", "LandscapeLeft LandscapeRight");
    setenv("TM_DINPUT_TRACE", "1", 1);
    setenv("RA2_INPUT_TRACE", "1", 1);
    fprintf(stderr, "[android] native finger->mouse bridge enabled; landscape-only; SDL/Win32 input trace enabled\\n");

    const char *root = SDL_AndroidGetInternalStoragePath();
    if (!root || !root[0]) {
        fprintf(stderr, "[android] SDL did not provide app-private storage\n");
        return 70;
    }
    tm_android_redirect_stdio(root);

    char game[PATH_MAX], user[PATH_MAX], system_dir[PATH_MAX], exe[PATH_MAX], data[PATH_MAX];
    if (snprintf(game, sizeof(game), "%s/game", root) >= (int)sizeof(game) ||
        snprintf(user, sizeof(user), "%s/user", root) >= (int)sizeof(user) ||
        snprintf(system_dir, sizeof(system_dir), "%s/system", root) >= (int)sizeof(system_dir) ||
        snprintf(exe, sizeof(exe), "%s/Movies.exe", game) >= (int)sizeof(exe) ||
        snprintf(data, sizeof(data), "%s/Data", game) >= (int)sizeof(data)) {
        fprintf(stderr, "[android] app-private path is too long\n");
        return 70;
    }

    struct stat exe_stat;
    if (!tm_is_directory(game) || stat(exe, &exe_stat) != 0 || !S_ISREG(exe_stat.st_mode) ||
        !tm_is_directory(data)) {
        fprintf(stderr,
                "[android] game data not found. Copy your own installation so that\n"
                "  %s/Movies.exe\n  %s/Data/\n"
                "exist, then restart the app.\n", game, game);
        return 66;
    }

    fprintf(stderr, "[android] game root: %s\n", game);
    fprintf(stderr, "[android] user root: %s\n", user);
    fprintf(stderr, "[android] system root: %s\n", system_dir);
    char frames[PATH_MAX];
    if (snprintf(frames, sizeof(frames), "%s/logs/frames", root) < (int)sizeof(frames) &&
        tm_mkdirs(frames)) {
        if (setenv("TM_D3D9_SHOTS", frames, 1) == 0)
            fprintf(stderr, "[android] D3D9 frame capture enabled: %s (frames 1, 2, 4, ... 512)\n", frames);
        else
            fprintf(stderr, "[android] could not enable D3D9 frame capture\n");
    } else {
        fprintf(stderr, "[android] could not create D3D9 frame capture directory\n");
    }

    fprintf(stderr, "[android] starting shared host with Android DXVK D3D9 backend\n");

    const tm_host_options options = {
        .game_dir = game,
        .user_dir = user,
        .system_dir = system_dir,
        .d3d9_backend = "dxvk",
        .watchdog_seconds = 0
    };
    return tm_host_run(&options);
}
