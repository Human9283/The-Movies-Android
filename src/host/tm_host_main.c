/* Linux command-line adapter for the shared The Movies runtime. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_trace.h"
#include "tm_host.h"


#ifndef __ANDROID__
int main(int argc, char **argv) {
    tm_host_options options = {
        .game_dir = NULL,
        .user_dir = "tm_user",
        .system_dir = "tm_system"
    };
    for (int i = 1; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--stop-at-winmain")) options.stop_at_winmain = 1;
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc) options.watchdog_seconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--user") && i + 1 < argc) options.user_dir = argv[++i];
        else if (!strcmp(argv[i], "--system") && i + 1 < argc) options.system_dir = argv[++i];
        else if (!strcmp(argv[i], "--imports-json") && i + 1 < argc) options.imports_json = argv[++i];
        else if (!strcmp(argv[i], "--import-trace") && i + 1 < argc) options.import_trace = argv[++i];
        else if (!strcmp(argv[i], "--seen-out") && i + 1 < argc) options.seen_out = argv[++i];
        else if (!strcmp(argv[i], "--d3d9") && i + 1 < argc) options.d3d9_backend = argv[++i];
        else if (!strcmp(argv[i], "--mouse") && i + 1 < argc && !strcmp(argv[i + 1], "free")) options.mouse_free = 1, i++;
        else if (argv[i][0] != '-' && !options.game_dir) options.game_dir = argv[i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 64; }
    }
    if (!options.game_dir) {
        fprintf(stderr, "usage: %s <game dir> [--stop-at-winmain] [--watchdog S] [--user DIR] [--system DIR] [--imports-json FILE] [--import-trace FILE] [--seen-out FILE] [--d3d9 dxvk]\n", argv[0]);
        recomp_trace_help();
        return 64;
    }
    return tm_host_run(&options);
}
#endif
