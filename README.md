# The Movies for Android

An unofficial ARM64 Android port of *The Movies* (2005), targeting the Russian CD release 1.1.

> **Experimental — the game is not yet confirmed playable.** An older device-tested APK reached the animated intro but did not reach the main menu. The current source revision includes later native-code changes and has not yet been rebuilt or tested on a device, so the older result must not be treated as the current revision's behaviour. A successful APK build or installation does not mean the game works.

## Build

Follow **[Build the Android APK](docs/ANDROID-BUILD.md)**. The guide covers the required tools, dependencies, local game-file preparation and Gradle build command.

You must provide your own legally obtained copy of the original game. This repository does not distribute the game executable, disc images, proprietary game data or generated code derived from the executable.

## Current scope

- Android target: ARM64 (`arm64-v8a`), Android 7.0/API 24 or later, with a Vulkan 1.1-compatible GPU and driver.
- Graphics: the Android D3D9 path can present the game's animated intro.
- Media: a video-only WMAsfReader/RGB24 path using FFmpeg is implemented in source, but has not been built or validated on Android; playback of the intro and menu videos is not yet confirmed.
- Main menu, studio gameplay, media playback, audio, touch controls, save/load and sustained play are not yet verified as working.

The immediate goal is to complete and validate the Russian CD 1.1 port. A Steam-release port may be considered afterwards.

## Legal and privacy

This project is unofficial and is not affiliated with Lionhead Studios, Activision or Microsoft. *The Movies* and related marks belong to their respective owners.

Project source is MIT-licensed. Third-party dependencies retain their own licences; see [LICENSE](LICENSE) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Do not commit original game files, generated lifted code, diagnostic archives, credentials or personal data. See [PRIVACY.md](PRIVACY.md) and [SECURITY.md](SECURITY.md).
