#include <SDL.h>
#include <SDL_vulkan.h>
#include <SDL_system.h>
#include <d3d9.h>
#include <android/log.h>
#include <jni.h>
#include <dlfcn.h>
#include <cstdlib>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

#define TAG "TM-D3D9-PROBE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

using Direct3DCreate9Fn = IDirect3D9* (WINAPI *)(UINT);

static std::mutex g_report_mutex;
static char g_report[8192] = "The Movies Android DXVK D3D9 probe 0.2\n";

static void update_java_status(const char* status) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    if (!cls) return;
    jmethodID method = env->GetMethodID(cls, "updateProbeStatus", "(Ljava/lang/String;)V");
    if (method) {
        jstring value = env->NewStringUTF(status);
        if (value) {
            env->CallVoidMethod(activity, method, value);
            env->DeleteLocalRef(value);
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
}

static void reportf(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    {
        std::lock_guard<std::mutex> lock(g_report_mutex);
        size_t used = strlen(g_report);
        if (used + strlen(line) + 2 < sizeof(g_report)) {
            memcpy(g_report + used, line, strlen(line));
            used += strlen(line);
            g_report[used++] = '\n';
            g_report[used] = '\0';
        }
    }
    LOGI("%s", line);
    update_java_status(line);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_human9283_d3d9probe_MainActivity_nativeGetProbeReport(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(g_report_mutex);
    return env->NewStringUTF(g_report);
}

struct ProbeVertex {
    float x, y, z, rhw;
    DWORD color;
};

extern "C" int SDL_main(int, char**) {
    setenv("DXVK_WSI_DRIVER", "SDL2", 1);
    setenv("DXVK_LOG_LEVEL", "info", 1);
    setenv("DXVK_STATE_CACHE", "0", 1);

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        reportf("FAIL SDL_Init: %s", SDL_GetError());
        return 10;
    }
    reportf("SDL_Init: PASS");
    if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
        reportf("FAIL SDL_Vulkan_LoadLibrary: %s", SDL_GetError());
        SDL_Quit();
        return 11;
    }

    reportf("SDL_Vulkan_LoadLibrary: PASS");
    SDL_Window* window = SDL_CreateWindow(
        "The Movies — DXVK D3D9 test",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        reportf("FAIL SDL_CreateWindow: %s", SDL_GetError());
        SDL_Vulkan_UnloadLibrary();
        SDL_Quit();
        return 12;
    }

    reportf("SDL_CreateWindow: PASS");
    void* dxvk_library = dlopen("libdxvk_d3d9.so", RTLD_NOW | RTLD_LOCAL);
    if (!dxvk_library) {
        reportf("FAIL dlopen libdxvk_d3d9.so: %s", dlerror());
        SDL_DestroyWindow(window);
        SDL_Vulkan_UnloadLibrary();
        SDL_Quit();
        return 13;
    }
    auto create9 = reinterpret_cast<Direct3DCreate9Fn>(dlsym(dxvk_library, "Direct3DCreate9"));
    if (!create9) {
        reportf("FAIL Direct3DCreate9 export: %s", dlerror());
        dlclose(dxvk_library);
        SDL_DestroyWindow(window);
        SDL_Vulkan_UnloadLibrary();
        SDL_Quit();
        return 13;
    }

    reportf("DXVK library load: PASS");
    reportf("Calling Direct3DCreate9");
    IDirect3D9* d3d = create9(D3D_SDK_VERSION);
    if (!d3d) {
        reportf("FAIL Direct3DCreate9 returned NULL");
        SDL_DestroyWindow(window);
        SDL_Vulkan_UnloadLibrary();
        SDL_Quit();
        return 14;
    }
    reportf("Direct3DCreate9: PASS");

    int width = 0, height = 0;
    SDL_Vulkan_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        SDL_GetWindowSize(window, &width, &height);
    }

    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = static_cast<UINT>(width);
    pp.BackBufferHeight = static_cast<UINT>(height);
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferCount = 2;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = reinterpret_cast<HWND>(window);
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = FALSE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        reinterpret_cast<HWND>(window),
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &pp,
        &device);
    if (FAILED(hr) || !device) {
        reportf("FAIL IDirect3D9::CreateDevice: HRESULT=0x%08x (%d)", static_cast<unsigned>(hr), static_cast<int>(hr));
        d3d->Release();
        dlclose(dxvk_library);
        SDL_DestroyWindow(window);
        SDL_Vulkan_UnloadLibrary();
        SDL_Quit();
        return 15;
    }
    reportf("IDirect3D9::CreateDevice: PASS; backbuffer=%dx%d", width, height);

    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

    ProbeVertex triangle[3] = {
        { width * 0.50f, height * 0.22f, 0.5f, 1.0f, D3DCOLOR_ARGB(255, 245, 245, 255) },
        { width * 0.28f, height * 0.76f, 0.5f, 1.0f, D3DCOLOR_ARGB(255, 40, 220, 120) },
        { width * 0.72f, height * 0.76f, 0.5f, 1.0f, D3DCOLOR_ARGB(255, 40, 220, 120) },
    };

    reportf("D3D9 fixed-function clear/draw/present started");
    bool running = true;
    bool logged_present = false;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                width = event.window.data1;
                height = event.window.data2;
                triangle[0].x = width * 0.50f; triangle[0].y = height * 0.22f;
                triangle[1].x = width * 0.28f; triangle[1].y = height * 0.76f;
                triangle[2].x = width * 0.72f; triangle[2].y = height * 0.76f;
            }
        }

        hr = device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(12, 32, 72), 1.0f, 0);
        if (SUCCEEDED(hr)) hr = device->BeginScene();
        if (SUCCEEDED(hr)) {
            hr = device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle, sizeof(ProbeVertex));
            device->EndScene();
        }
        if (SUCCEEDED(hr)) hr = device->Present(nullptr, nullptr, nullptr, nullptr);
        if (FAILED(hr)) {
            reportf("FAIL D3D9 render/Present: HRESULT=0x%08x (%d)", static_cast<unsigned>(hr), static_cast<int>(hr));
            break;
        }
        if (!logged_present) {
            reportf("PASS D3D9 fixed-function triangle + Present");
            logged_present = true;
        }
        SDL_Delay(16);
    }

    device->Release();
    d3d->Release();
    dlclose(dxvk_library);
    SDL_DestroyWindow(window);
    SDL_Vulkan_UnloadLibrary();
    SDL_Quit();
    reportf("D3D9 probe finished");
    return logged_present ? 0 : 16;
}
