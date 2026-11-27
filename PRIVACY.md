# Privacy

This repository is a source-code project. It does not ask contributors to publish personal information, device identifiers, game files or diagnostic archives.

## Project data and telemetry

The project documentation describes local build and diagnostic workflows. Diagnostic export is user-initiated: the app creates an archive on the device and the user chooses whether to share it. The current game-app manifest does not request Android's `INTERNET` permission. Do not assume that a diagnostic archive is anonymous.

The diagnostic archive may contain:
- device manufacturer and model, Android version/API level and app version;
- supported CPU ABI information;
- application and native logs, including stack traces;
- up to 2,000 recent Android logcat entries filtered to the app's process; this reduces unrelated log capture but does not make the archive anonymous;
- local filesystem paths, game/runtime filenames and operation details.

The archive is not intended to contain the original game installation, save files or account credentials. Review the actual archive before sharing; logs can contain unexpected data.

## Before publishing a report

1. Open the archive locally and inspect every file.
2. Remove personal names, usernames, home-directory paths, device/build identifiers, tokens and unrelated log entries.
3. Do not upload original game files, disc images, save files, generated lifted code or proprietary assets.
4. Prefer a short, redacted excerpt that reproduces the technical issue.
5. Do not paste secrets into public issues, pull requests or discussions.

## Repository visibility

This repository is public. Commits, source files, workflow definitions, workflow logs, issues and pull-request discussions may be visible to anyone. Do not place private build artifacts, credentials, personal contact details or unredacted diagnostic output here.

Removing a file from the latest commit does not necessarily remove it from Git history, cached views, forks or existing clones. If sensitive data was published, revoke exposed credentials first and follow [GitHub's sensitive-data removal guidance](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository). History rewriting should be treated as a coordinated recovery operation, not routine cleanup.
