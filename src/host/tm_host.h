#ifndef TM_HOST_H
#define TM_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-neutral inputs for starting the original game runtime.
 * Paths must be absolute on Android; desktop callers may keep their
 * existing relative defaults. Optional diagnostics are NULL when unused. */
typedef struct tm_host_options {
    const char *game_dir;
    const char *user_dir;
    const char *system_dir;
    const char *imports_json;
    const char *import_trace;
    const char *seen_out;
    const char *d3d9_backend;
    int watchdog_seconds;
    int stop_at_winmain;
    int mouse_free;
} tm_host_options;

/* Starts the guest runtime. Returns the guest exit code or a host error. */
int tm_host_run(const tm_host_options *options);

#ifdef __cplusplus
}
#endif
#endif
