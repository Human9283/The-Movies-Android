# Build the ARM64 Android APK

> **Important: an APK build is not proof that the game works.** This project is still under development. The APK may install but the game may fail to start, stop during the intro, render incorrectly, perform poorly, or lack major functionality. The instructions below explain how to compile the current Android port; they do not promise a playable result. The repository status and instructions will be revised after the CD 1.1 port has been completed and validated.

This guide builds the Android app independently from source on a Linux x86-64 workstation; no prebuilt project artifacts are required.

The supported game executable is the Russian CD release 1.1. You must supply your own original disc images or an installed copy. The repository does not contain game data or generated game code.

## 1. Host tools

Use Ubuntu 24.04 x86-64. Install a JDK 17, Git, Python, CMake, Ninja, Meson, glslang, ccache and the Windows Installer extraction tools:

~~~bash
sudo apt-get update
sudo apt-get install -y git curl unzip zip openjdk-17-jdk \
  build-essential cmake ninja-build glslang-tools ccache patch pkg-config \
  python3-venv msitools cabextract
~~~

Create a Python environment for the lift and build tools:

~~~bash
python3 -m venv "$HOME/.venvs/themovies-android"
source "$HOME/.venvs/themovies-android/bin/activate"
python -m pip install --upgrade pip
python -m pip install pefile capstone pycdlib meson==1.5.2
~~~

Create a workspace used by the dependency downloads and build:

~~~bash
export WORK="$HOME/themovies-android-build"
mkdir -p "$WORK"
~~~

Install the Android command-line tools. This guide pins the published Linux package and verifies its SHA-256 before unpacking it:

~~~bash
export ANDROID_HOME="$HOME/Android/Sdk"
mkdir -p "$ANDROID_HOME/cmdline-tools"
curl -fL -o "$WORK/commandlinetools.zip" \
  "https://dl.google.com/android/repository/commandlinetools-linux-15859902_latest.zip"
echo "4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583  $WORK/commandlinetools.zip" | sha256sum -c -
mkdir -p "$WORK/cmdline-tools-unpack"
unzip -q "$WORK/commandlinetools.zip" -d "$WORK/cmdline-tools-unpack"
rm -rf "$ANDROID_HOME/cmdline-tools/latest"
mv "$WORK/cmdline-tools-unpack/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest"
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"
~~~

Install the exact Android platform and native toolchain versions used by this project:

~~~bash
yes | sdkmanager --licenses
sdkmanager "platforms;android-36" "build-tools;36.0.0" \
  "ndk;28.2.13676358" "cmake;3.22.1" "platform-tools"
~~~

Install the pinned Gradle 8.11.1 binary distribution and verify it against Gradle's published SHA-256:

~~~bash
curl -fL -o "$WORK/gradle-8.11.1-bin.zip" \
  "https://services.gradle.org/distributions/gradle-8.11.1-bin.zip"
echo "f397b287023acdba1e9f6fc5ea72d22dd63669d59ed4a289a29b1a76eee151c6  $WORK/gradle-8.11.1-bin.zip" | sha256sum -c -
mkdir -p "$HOME/.local/gradle"
unzip -q "$WORK/gradle-8.11.1-bin.zip" -d "$HOME/.local/gradle"
export PATH="$HOME/.local/gradle/gradle-8.11.1/bin:$PATH"
java -version
gradle --version
~~~

Use JDK 17 and Gradle 8.11.1. The Android Gradle Plugin declared by the project is 8.10.1; that plugin's minimum supported Gradle version is 8.11.1. Official references: [Android command-line tools and package checksums](https://developer.android.com/studio), [Android SDK/NDK setup](https://developer.android.com/studio/projects/install-ndk), [Gradle 8.11.1 checksums](https://gradle.org/release-checksums/), and [Android Gradle Plugin compatibility](https://developer.android.com/build/releases/gradle-plugin).

## 2. Check out the source and pinned runtime

~~~bash
git clone https://github.com/Human9283/The-Movies-Android "$WORK/port"
git clone https://github.com/sp00nznet/pcrecomp "$WORK/pcrecomp"
git -C "$WORK/pcrecomp" checkout 7c07a0b42059b6dc4313d9b4ebded6f7a72db8f3
git clone https://github.com/sp00nznet/themovies "$WORK/themovies"
git -C "$WORK/themovies" checkout 8e278f6b22d70095d0b1b10803370613b20862f5
~~~

The pcrecomp and themovies revisions are pinned because the generated lift and host shims depend on their runtime interfaces.

## 3. Install your copy of the game and generate the lift

Copy the three original CD ISO images into $WORK/iso/:

~~~bash
mkdir -p "$WORK/iso"
# Copy MOVIES_1.iso, MOVIES_2.iso and MOVIES_3.iso into this directory.
~~~

Extract the installed files:

~~~bash
python "$WORK/port/tools/tm_install.py" --iso "$WORK/iso" --out "$WORK/game"
~~~

The extraction command must exit successfully. It writes `tm_manifest.json`
with the number of expected MSI file rows, per-file size checks and any
extraction problems. A missing cabinet member, size mismatch, collision or
cabinet validation error makes the command fail. Do not continue with a
partial installation. The Android asset packager repeats these checks and
refuses manifests with extraction problems. Do not use `--no-hash` when
preparing Android assets; the packager requires per-file SHA-256 values.

Generate the executable analysis and function catalog:

~~~bash
mkdir -p "$WORK/work"
python "$WORK/pcrecomp/tools/pe/pe_analyze.py" \
  "$WORK/game/Movies.exe" --json "$WORK/work/pe_analysis.json"
python "$WORK/pcrecomp/tools/cpp/rtti.py" \
  "$WORK/game/Movies.exe" -o "$WORK/work/rtti.json" \
  --seeds "$WORK/work/rtti_seeds.json"
python "$WORK/port/tools/crt_init_seeds.py" \
  "$WORK/game/Movies.exe" --merge "$WORK/work/rtti_seeds.json" \
  -o "$WORK/work/seeds.json"
python "$WORK/pcrecomp/tools/disasm/disasm32.py" \
  "$WORK/game/Movies.exe" -o "$WORK/work/functions.json" \
  --seed-functions "$WORK/work/seeds.json"
~~~

Run the static lift using the pinned themovies driver:

~~~bash
ln -sfn "$WORK/work" "$WORK/themovies/work"
cd "$WORK/themovies"
PCRECOMP="$WORK/pcrecomp" python run_lift.py \
  --exe "$WORK/game/Movies.exe" \
  --catalog "$WORK/work/functions.json" \
  --out "$WORK/work/gen" --all
~~~

Inspect the generated code:

~~~bash
python "$WORK/port/tools/lift_health.py" "$WORK/work/gen"
~~~

The lift command must finish without exceptions. The health script summarizes
dispatch entries, generated functions, unresolved indirect-tail targets and
not-lifted stubs; it is an inspection aid, not a proof that every game path is
implemented or that the Android port is playable. Keep the generated directory
local; it contains code derived from the proprietary executable and must not
be committed or distributed.

Convert the dispatch table to AArch64 assembly and split the generated C into bounded translation units:

~~~bash
python "$WORK/port/android-game/tools/dispatch_to_asm.py" \
  "$WORK/work/gen/recomp_dispatch.c" \
  "$WORK/work/gen/recomp_dispatch.S"
python "$WORK/port/android-game/tools/split_lifted_c.py" \
  "$WORK/work/gen" "$WORK/work/gen-android" --max-bytes 3145728
~~~

gen-android/ is the directory passed to Gradle as tmGen.

## 4. Build SDL2 and SDL2_ttf

Set the NDK path used by all Android native dependencies:

~~~bash
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/28.2.13676358"
export SDL_ROOT="$WORK/SDL"
export SDL_PREFIX="$WORK/android-deps"
~~~

Build SDL2 release 2.30.9:

~~~bash
git clone --depth 1 --branch release-2.30.9 \
  https://github.com/libsdl-org/SDL.git "$SDL_ROOT"
cmake -S "$SDL_ROOT" -B "$SDL_ROOT/build-android" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
cmake --build "$SDL_ROOT/build-android" --parallel 2
cmake --install "$SDL_ROOT/build-android" --prefix "$SDL_PREFIX"
~~~

Locate the installed SDL2 CMake package and library:

~~~bash
export SDL2_CMAKE_DIR="$(dirname "$(find "$SDL_PREFIX" -name SDL2Config.cmake -print -quit)")"
export SDL_LIB="$(find "$SDL_ROOT/build-android" -name libSDL2.so -print -quit)"
~~~

Build SDL2_ttf release 2.22.0:

~~~bash
git clone --depth 1 --branch release-2.22.0 \
  https://github.com/libsdl-org/SDL_ttf.git "$WORK/SDL_ttf"
git -C "$WORK/SDL_ttf" submodule update --init --depth 1
cmake -S "$WORK/SDL_ttf" -B "$WORK/SDL_ttf/build-android" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DSDL2_DIR="$SDL2_CMAKE_DIR" -DCMAKE_PREFIX_PATH="$SDL_PREFIX" \
  -DBUILD_SHARED_LIBS=ON -DSDL2TTF_VENDORED=ON \
  -DSDL2TTF_HARFBUZZ=OFF -DSDL2TTF_SAMPLES=OFF \
  -DSDL2TTF_INSTALL=OFF
cmake --build "$WORK/SDL_ttf/build-android" --parallel 2
export SDL2_TTF_LIB="$(find "$WORK/SDL_ttf/build-android" -name libSDL2_ttf.so -print -quit)"
export SDL2_TTF_INCLUDE="$WORK/SDL2TTF-compat"
mkdir -p "$SDL2_TTF_INCLUDE/SDL2" "$SDL_ROOT/include/SDL2"
cp "$WORK/SDL_ttf/SDL_ttf.h" "$SDL2_TTF_INCLUDE/SDL2/SDL_ttf.h"
cp "$WORK/SDL_ttf/SDL_ttf.h" "$SDL_ROOT/include/SDL2/SDL_ttf.h"
~~~

## 5. Build Android DXVK Native

The port pins the Android DXVK Native fork at commit f2c7207b3d8bc848fe3a27cef34ee5e67aeec552:

~~~bash
export DXVK_ROOT="$WORK/dxvk-native-android"
git clone https://github.com/Star1xr/dxvk-native-android.git "$DXVK_ROOT"
git -C "$DXVK_ROOT" checkout f2c7207b3d8bc848fe3a27cef34ee5e67aeec552
python "$WORK/port/android-game/tools/patch_dxvk_android.py" "$DXVK_ROOT"
~~~

The patch adapts the SDL/Vulkan window to Android's existing SDLActivity surface, avoids recreating the swapchain for a successful VK_SUBOPTIMAL_KHR result, and configures the DXVK build to use the same shared C++ runtime as the app.

Build the ARM64 D3D9 library:

~~~bash
export SDL2_INCLUDE="$SDL_ROOT/include"
export SDL2_LIB_DIR="$(dirname "$SDL_LIB")"
"$DXVK_ROOT/android/build-android.sh" arm64-v8a 24 "$WORK/dxvk-build"
export DXVK_LIB="$WORK/dxvk-build/arm64-v8a/lib/libdxvk_d3d9.so"
~~~

## 6. Prepare the pinned pcrecomp runtime

The Android port applies its Win32 input bridge to the pinned pcrecomp runtime. Android touch is currently translated by SDL into ordinary mouse events, which the DirectInput shim consumes; this is single-pointer mouse emulation, not native multi-touch. Apply the runtime patch once to the clean checkout:

~~~bash
python "$WORK/port/android-game/tools/patch_win32hle_input_trace.py" \
  "$WORK/pcrecomp/runtime/win32hle"
~~~

Do not run this patcher a second time on the same checkout. If you need a clean re-application, restore or reclone the pinned pcrecomp revision first.

## 7. Choose whether to include game data in the APK

**Full APK with your own game data:** package the files extracted in step 3:

~~~bash
python "$WORK/port/tools/prepare_android_game_assets.py" \
  "$WORK/game" --assets "$WORK/port/android-game/app/src/main/assets"
~~~

This creates game-data.tar and game-manifest.json locally under the app assets directory. The manifest and each archived file are checked against SHA-256. The APK's first launch extracts the data into app-private storage. Do not commit or share these generated assets.

**Slim APK:** skip this step. On first launch, the app asks you to select an already installed copy of the game containing Movies.exe and Data/. It copies that folder into app-private storage.

## 8. Stage the native libraries and build

Copy the four required ARM64 libraries into the Android application's JNI library directory:

~~~bash
export LIBDIR="$WORK/port/android-game/app/src/main/jniLibs/arm64-v8a"
mkdir -p "$LIBDIR"
cp "$SDL_LIB" "$LIBDIR/libSDL2.so"
cp "$SDL2_TTF_LIB" "$LIBDIR/libSDL2_ttf.so"
cp "$DXVK_LIB" "$LIBDIR/libdxvk_d3d9.so"
cp "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" "$LIBDIR/"
~~~

Build the debug APK:

~~~bash
gradle --no-daemon --max-workers=1 -p "$WORK/port/android-game" \
  -PsdlRoot="$SDL_ROOT" \
  -PdxvkRoot="$DXVK_ROOT" \
  -PpcrecompRoot="$WORK/pcrecomp" \
  -PtmGen="$WORK/work/gen-android" \
  -PsdlLibrary="$SDL_LIB" \
  -Psdl2TtfIncludeDir="$SDL2_TTF_INCLUDE" \
  -Psdl2TtfLibrary="$SDL2_TTF_LIB" \
  -PdxvkLibrary="$DXVK_LIB" \
  :app:assembleDebug
~~~

The output is:

~~~text
$WORK/port/android-game/app/build/outputs/apk/debug/app-debug.apk
~~~

The build targets arm64-v8a, Android API 24+, compile SDK 36 and target SDK 35. The CMake native compilation is intentionally serialized because the generated C source is large.

## 9. Install

Copy the APK to the Android device and install it using Android's package installer. For adb:

~~~bash
adb install app-debug.apk
~~~

The first install creates the app's private data directory. Subsequent updates can preserve that directory only when the APK is signed with the same certificate and keeps the same application ID. A locally generated debug keystore may differ from the signing certificate used by an existing installation; Android will reject an in-place update when the signing certificates differ. Do not uninstall an existing installation merely to work around a signature mismatch if its app-private game data must be preserved.
