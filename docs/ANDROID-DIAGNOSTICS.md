# Android Diagnostics

The Android app keeps diagnostic data in its private app storage. It does not require ADB, root access, or a computer to export a report.

## Exporting a report

1. Open The Movies.
2. On the setup screen, select **Export diagnostics**.
3. Choose a destination in Android's document picker.
4. Attach `the-movies-diagnostics.zip` to the bug report.

The setup screen remains available when game files have already been imported. If the game process stalls or crashes, reopen the app and export the report before clearing app data or reinstalling. Exporting does not require ADB or a desktop computer. The archive is created locally; it is not automatically uploaded to GitHub or made available to the developer.

## Archive contents

- `device.txt`: app version, Android release/API level, device manufacturer/model, and supported CPU ABIs.
- `logs/app.log`: app lifecycle, game import/export operations, and Java uncaught-exception stack traces.
- `logs/native.log`: native host stdout/stderr, including game loader, filesystem, Direct3D 9, and DXVK diagnostics.
- `logs/logcat.txt`: the most recent available Android logcat buffer at export time.
- `*.1`: the previous rotated application/native log when a log exceeded 2 MiB.

The application log and native log are stored under the app-private `files/logs/` directory. Logs are rotated on app startup to limit storage use.

## Privacy

The archive is created locally and is not uploaded automatically. It can contain local filesystem paths, device model information, crash traces, and game/runtime diagnostics. Review the archive before sharing it. The archive does not include the user's game installation, save files, or account credentials.

## Reports used during development

For a useful report of a splash-screen stall, leave the game running long
enough to reproduce the issue, then return to the setup screen and export
the archive. Include whether the app was manually closed or whether it
terminated by itself. A native shutdown trace after a manual exit must not
be interpreted as the cause of the preceding stall.

## Current limitations

- A fatal native process crash may terminate the app before buffered output is flushed. Native stdout/stderr are line-buffered to reduce this risk.
- Android may restrict which system log entries an ordinary app can read. The logcat file is best-effort; the app's own app/native log files are the primary diagnostic sources.
- Kernel-level tombstones and vendor GPU crash dumps are not collected by this in-app exporter. Those may require a separate user-provided Android bug report.
