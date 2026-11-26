#include <android/log.h>
#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TAG "TM-VK-PROBE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static bool has_instance_extension(const char *name) {
    uint32_t n = 0;
    if (vkEnumerateInstanceExtensionProperties(NULL, &n, NULL) != VK_SUCCESS) return false;
    VkExtensionProperties p[256];
    if (n > 256) n = 256;
    if (vkEnumerateInstanceExtensionProperties(NULL, &n, p) != VK_SUCCESS) return false;
    for (uint32_t i = 0; i < n; i++) if (!strcmp(p[i].extensionName, name)) return true;
    return false;
}

static bool has_device_extension(VkPhysicalDevice gpu, const char *name) {
    uint32_t n = 0;
    if (vkEnumerateDeviceExtensionProperties(gpu, NULL, &n, NULL) != VK_SUCCESS) return false;
    VkExtensionProperties p[512];
    if (n > 512) n = 512;
    if (vkEnumerateDeviceExtensionProperties(gpu, NULL, &n, p) != VK_SUCCESS) return false;
    for (uint32_t i = 0; i < n; i++) if (!strcmp(p[i].extensionName, name)) return true;
    return false;
}

static void report_gpu(VkInstance instance, VkSurfaceKHR surface, VkPhysicalDevice gpu) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu, &props);
    LOGI("GPU: %s", props.deviceName);
    LOGI("Vulkan API: %u.%u.%u; driverVersion=%u; vendor=0x%04x device=0x%04x",
         VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
         VK_VERSION_PATCH(props.apiVersion), props.driverVersion, props.vendorID, props.deviceID);
    LOGI("maxPushConstantsSize=%u", props.limits.maxPushConstantsSize);
    if (props.apiVersion < VK_API_VERSION_1_3) {
        LOGE("DXVK v3.1.1 hardware gate: FAIL (physical device API below Vulkan 1.3)");
        return;
    }

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, NULL);
    VkQueueFamilyProperties q[32];
    if (qn > 32) qn = 32;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, q);
    bool graphics = false, present = false;
    for (uint32_t i = 0; i < qn; i++) {
        VkBool32 supports_present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supports_present);
        if ((q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && q[i].queueCount) graphics = true;
        if (supports_present && q[i].queueCount) present = true;
        LOGI("queue[%u]: flags=0x%x count=%u present=%u", i, q[i].queueFlags, q[i].queueCount, supports_present);
    }
    LOGI("queue graphics=%d present=%d", graphics, present);

    const char *required_exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        "VK_EXT_depth_clip_enable",
        "VK_EXT_robustness2",
        "VK_KHR_maintenance5",
        "VK_KHR_maintenance6",
        "VK_KHR_load_store_op_none"
    };
    bool ext_ok = true;
    for (unsigned i = 0; i < sizeof(required_exts)/sizeof(required_exts[0]); i++) {
        bool ok = has_device_extension(gpu, required_exts[i]);
        LOGI("DXVK required extension %-38s %s", required_exts[i], ok ? "YES" : "NO");
        if (!ok) ext_ok = false;
    }

    VkPhysicalDeviceVulkan11Features f11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceVulkan12Features f12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceDepthClipEnableFeaturesEXT depth = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT };
    VkPhysicalDeviceRobustness2FeaturesEXT robust = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
    VkPhysicalDeviceMaintenance5FeaturesKHR m5 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR };
    VkPhysicalDeviceMaintenance6FeaturesKHR m6 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR };
    void *chain = NULL;
#define PUSH_FEATURE(x) do { (x).pNext = chain; chain = &(x); } while (0)
    if (has_device_extension(gpu, "VK_EXT_depth_clip_enable")) PUSH_FEATURE(depth);
    if (has_device_extension(gpu, "VK_EXT_robustness2")) PUSH_FEATURE(robust);
    if (has_device_extension(gpu, "VK_KHR_maintenance5")) PUSH_FEATURE(m5);
    if (has_device_extension(gpu, "VK_KHR_maintenance6")) PUSH_FEATURE(m6);
    PUSH_FEATURE(f13); PUSH_FEATURE(f12); PUSH_FEATURE(f11);
    VkPhysicalDeviceFeatures2 f2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = chain };
    vkGetPhysicalDeviceFeatures2(gpu, &f2);
    bool required_features_ok = true;
#define REQUIRED_CORE(name) do { bool ok = f2.features.name != 0; LOGI("required %-39s %s", #name, ok ? "PASS" : "FAIL"); if (!ok) required_features_ok = false; } while (0)
    REQUIRED_CORE(depthBiasClamp); REQUIRED_CORE(depthClamp); REQUIRED_CORE(dualSrcBlend); REQUIRED_CORE(fillModeNonSolid);
    REQUIRED_CORE(fragmentStoresAndAtomics); REQUIRED_CORE(fullDrawIndexUint32); REQUIRED_CORE(geometryShader);
    REQUIRED_CORE(imageCubeArray); REQUIRED_CORE(independentBlend); REQUIRED_CORE(multiDrawIndirect); REQUIRED_CORE(multiViewport);
    REQUIRED_CORE(occlusionQueryPrecise); REQUIRED_CORE(robustBufferAccess); REQUIRED_CORE(sampleRateShading);
    REQUIRED_CORE(samplerAnisotropy); REQUIRED_CORE(shaderClipDistance); REQUIRED_CORE(shaderCullDistance);
    REQUIRED_CORE(shaderImageGatherExtended); REQUIRED_CORE(shaderInt16); REQUIRED_CORE(shaderInt64);
    REQUIRED_CORE(shaderSampledImageArrayDynamicIndexing); REQUIRED_CORE(textureCompressionBC);
#undef REQUIRED_CORE
#define REQUIRED_FEAT(obj, name) do { bool ok = (obj).name != 0; LOGI("required %-39s %s", #name, ok ? "PASS" : "FAIL"); if (!ok) required_features_ok = false; } while (0)
    REQUIRED_FEAT(f11, shaderDrawParameters); REQUIRED_FEAT(f11, storageBuffer16BitAccess);
    REQUIRED_FEAT(f12, bufferDeviceAddress); REQUIRED_FEAT(f12, descriptorIndexing); REQUIRED_FEAT(f12, storageBuffer8BitAccess);
    REQUIRED_FEAT(f12, descriptorBindingSampledImageUpdateAfterBind);
    REQUIRED_FEAT(f12, descriptorBindingUpdateUnusedWhilePending); REQUIRED_FEAT(f12, descriptorBindingPartiallyBound);
    REQUIRED_FEAT(f12, hostQueryReset); REQUIRED_FEAT(f12, runtimeDescriptorArray); REQUIRED_FEAT(f12, samplerMirrorClampToEdge);
    REQUIRED_FEAT(f12, scalarBlockLayout); REQUIRED_FEAT(f12, shaderInt8); REQUIRED_FEAT(f12, timelineSemaphore);
    REQUIRED_FEAT(f12, uniformBufferStandardLayout); REQUIRED_FEAT(f12, vulkanMemoryModel);
    REQUIRED_FEAT(f13, inlineUniformBlock); REQUIRED_FEAT(f13, computeFullSubgroups); REQUIRED_FEAT(f13, dynamicRendering);
    REQUIRED_FEAT(f13, maintenance4); REQUIRED_FEAT(f13, shaderDemoteToHelperInvocation);
    REQUIRED_FEAT(f13, shaderZeroInitializeWorkgroupMemory); REQUIRED_FEAT(f13, subgroupSizeControl); REQUIRED_FEAT(f13, synchronization2);
    if (has_device_extension(gpu, "VK_EXT_depth_clip_enable")) REQUIRED_FEAT(depth, depthClipEnable);
    else required_features_ok = false;
    if (has_device_extension(gpu, "VK_EXT_robustness2")) {
        REQUIRED_FEAT(robust, robustBufferAccess2); REQUIRED_FEAT(robust, nullDescriptor);
    } else required_features_ok = false;
    if (has_device_extension(gpu, "VK_KHR_maintenance5")) REQUIRED_FEAT(m5, maintenance5);
    else required_features_ok = false;
    if (has_device_extension(gpu, "VK_KHR_maintenance6")) REQUIRED_FEAT(m6, maintenance6);
    else required_features_ok = false;
#undef REQUIRED_FEAT
    if (props.limits.maxPushConstantsSize < 256) {
        LOGI("required maxPushConstantsSize >= 256 FAIL (reported %u)", props.limits.maxPushConstantsSize);
        required_features_ok = false;
    } else LOGI("required maxPushConstantsSize >= 256 PASS");
    VkSurfaceCapabilitiesKHR caps;
    VkResult cr = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
    LOGI("surface capabilities result=%d minImages=%u maxImages=%u currentExtent=%ux%u",
         cr, cr == VK_SUCCESS ? caps.minImageCount : 0, cr == VK_SUCCESS ? caps.maxImageCount : 0,
         cr == VK_SUCCESS ? caps.currentExtent.width : 0, cr == VK_SUCCESS ? caps.currentExtent.height : 0);
    bool ready = props.apiVersion >= VK_API_VERSION_1_3 && graphics && present && ext_ok &&
                 required_features_ok && cr == VK_SUCCESS;
    LOGI("DXVK v3.1.1 hardware gate: %s (does not prove Android linking/runtime compatibility)",
         ready ? "PASS" : "FAIL");
    LOGI("NOTE: no Vulkan device is created; this is capability enumeration only.");
    (void)instance;
}

static void probe(struct android_app *app) {
    uint32_t loader_version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion enum_version =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (enum_version) enum_version(&loader_version);
    LOGI("Vulkan loader API: %u.%u.%u", VK_VERSION_MAJOR(loader_version),
         VK_VERSION_MINOR(loader_version), VK_VERSION_PATCH(loader_version));
    if (loader_version < VK_API_VERSION_1_3) {
        LOGE("FAIL: Vulkan 1.3 loader unavailable (DXVK v3.1.1 requires Vulkan 1.3)");
        return;
    }
    if (!has_instance_extension(VK_KHR_SURFACE_EXTENSION_NAME) ||
        !has_instance_extension(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) {
        LOGE("FAIL: VK_KHR_surface or VK_KHR_android_surface unavailable");
        return;
    }
    const char *exts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME };
    VkApplicationInfo ai = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "The Movies Vulkan Probe", .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &ai, .enabledExtensionCount = 2, .ppEnabledExtensionNames = exts };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult vr = vkCreateInstance(&ici, NULL, &instance);
    if (vr != VK_SUCCESS) { LOGE("vkCreateInstance failed: %d", vr); return; }
    PFN_vkCreateAndroidSurfaceKHR create_surface =
        (PFN_vkCreateAndroidSurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR");
    VkAndroidSurfaceCreateInfoKHR sci = { .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = app->window };
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vr = create_surface ? create_surface(instance, &sci, NULL, &surface) : VK_ERROR_EXTENSION_NOT_PRESENT;
    if (vr != VK_SUCCESS) { LOGE("vkCreateAndroidSurfaceKHR failed: %d", vr); vkDestroyInstance(instance, NULL); return; }

    uint32_t count = 0;
    vr = vkEnumeratePhysicalDevices(instance, &count, NULL);
    LOGI("physical device enumeration: result=%d count=%u", vr, count);
    if (vr == VK_SUCCESS && count) {
        VkPhysicalDevice devices[16]; if (count > 16) count = 16;
        vr = vkEnumeratePhysicalDevices(instance, &count, devices);
        for (uint32_t i = 0; vr == VK_SUCCESS && i < count; i++) report_gpu(instance, surface, devices[i]);
    } else LOGE("FAIL: no Vulkan physical device found");
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
}

static void handle_cmd(struct android_app *app, int32_t cmd) {
    if (cmd == APP_CMD_INIT_WINDOW && app->window) probe(app);
}

void android_main(struct android_app *app) {
    app_dummy();
    app->onAppCmd = handle_cmd;
    LOGI("Probe started. Waiting for Android native window.");
    while (!app->destroyRequested) {
        int events = 0; struct android_poll_source *source = NULL;
        int ident = ALooper_pollOnce(-1, NULL, &events, (void **)&source);
        if (ident >= 0 && source) source->process(app, source);
    }
    LOGI("Probe stopped.");
}
