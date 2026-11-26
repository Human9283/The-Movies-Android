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

int SDL_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    SDL_SetMainReady();

    const char *root = SDL_AndroidGetInternalStoragePath();
    if (!root || !root[0]) {
        fprintf(stderr, "[android] SDL did not provide app-private storage\n");
        return 70;
    }

    char game[PATH_MAX], user[PATH_MAX], system_dir[PATH_MAX], exe[PATH_MAX], data[PATH_MAX];
    if (snprintf(game, sizeof(game), "%s/game", root) >= (int)sizeof(game) ||
        snprintf(user, sizeof(user), "%s/user", root) >= (int)sizeof(user) ||
        snprintf(system_dir, sizeof(system_dir), "%s/system", root) >= (int)sizeof(system_dir) ||
        snprintf(exe, sizeof(exe), "%s/Movies.exe", game) >= (int)sizeof(exe) ||
        snprintf(data, sizeof(data), "%s/Data", game) >= (int)sizeof(data)) {
        fprintf(stderr, "[android] app-private path is too long\n");
        return 70;
    }

    if (!tm_mkdirs(user) || !tm_mkdirs(system_dir)) {
        fprintf(stderr, "[android] cannot create user/system directories under %s\n", root);
        return 73;
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
