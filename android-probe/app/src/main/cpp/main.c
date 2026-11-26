#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <stdarg.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TAG "TM-VK-PROBE"
static char g_report[49152];
static size_t g_report_len;

static void append_report(const char *level, const char *fmt, ...) {
    if (g_report_len >= sizeof(g_report) - 1) return;
    int n = snprintf(g_report + g_report_len, sizeof(g_report) - g_report_len,
                     "%s", level);
    if (n < 0) return;
    g_report_len += (size_t)n;
    if (g_report_len >= sizeof(g_report) - 1) return;
    va_list args;
    va_start(args, fmt);
    n = vsnprintf(g_report + g_report_len, sizeof(g_report) - g_report_len, fmt, args);
    va_end(args);
    if (n > 0) {
        size_t added = (size_t)n;
        g_report_len += added < sizeof(g_report) - g_report_len
            ? added : sizeof(g_report) - g_report_len - 1;
    }
    if (g_report_len < sizeof(g_report) - 1) {
        g_report[g_report_len++] = '\n';
        g_report[g_report_len] = '\0';
    }
}
#define LOGI(...) do { __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__); append_report("INFO: ", __VA_ARGS__); } while (0)
#define LOGE(...) do { __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__); append_report("ERROR: ", __VA_ARGS__); } while (0)

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
    uint32_t graphics_family = UINT32_MAX, present_family = UINT32_MAX;
    for (uint32_t i = 0; i < qn; i++) {
        VkBool32 supports_present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supports_present);
        if ((q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && q[i].queueCount) { graphics = true; if (graphics_family == UINT32_MAX) graphics_family = i; }
        if (supports_present && q[i].queueCount) { present = true; if (present_family == UINT32_MAX) present_family = i; }
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
    PFN_vkGetPhysicalDeviceFeatures2 get_features2 =
        (PFN_vkGetPhysicalDeviceFeatures2)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2");
    if (!get_features2) {
        get_features2 = (PFN_vkGetPhysicalDeviceFeatures2)
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2KHR");
    }
    if (!get_features2) {
        LOGE("FAIL: vkGetPhysicalDeviceFeatures2 entry point unavailable");
        return;
    }
    get_features2(gpu, &f2);
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
    /* DXVK 2.4.1 baseline smoke test. In upstream v2.4.1, swapchain and
       robustness2 are required; maintenance5, depth-clip and transform
       feedback are optional. Test device creation with the core features
       DXVK enables unconditionally, then try creating an Android swapchain. */
    bool base_ext_ok = has_device_extension(gpu, VK_KHR_SWAPCHAIN_EXTENSION_NAME)
                    && has_device_extension(gpu, "VK_EXT_robustness2");
    bool base_features_ok = f2.features.robustBufferAccess
                         && f12.timelineSemaphore && f12.hostQueryReset
                         && f12.vulkanMemoryModel
                         && f13.synchronization2 && f13.dynamicRendering
                         && f13.maintenance4
                         && robust.robustBufferAccess2 && robust.nullDescriptor;
    LOGI("DXVK 2.4.1 baseline extensions: %s", base_ext_ok ? "PASS" : "FAIL");
    LOGI("DXVK 2.4.1 baseline features: %s", base_features_ok ? "PASS" : "FAIL");
    if (props.apiVersion >= VK_API_VERSION_1_3 && graphics && present
        && graphics_family != UINT32_MAX && present_family != UINT32_MAX
        && base_ext_ok && base_features_ok && cr == VK_SUCCESS) {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queues[2] = {
            { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = graphics_family, .queueCount = 1, .pQueuePriorities = &priority },
            { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = present_family, .queueCount = 1, .pQueuePriorities = &priority }
        };
        uint32_t queue_count = graphics_family == present_family ? 1 : 2;
        VkPhysicalDeviceRobustness2FeaturesEXT er = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, .robustBufferAccess2 = VK_TRUE, .nullDescriptor = VK_TRUE };
        VkPhysicalDeviceVulkan13Features e13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE, .maintenance4 = VK_TRUE };
        VkPhysicalDeviceVulkan12Features e12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .timelineSemaphore = VK_TRUE, .hostQueryReset = VK_TRUE, .vulkanMemoryModel = VK_TRUE };
        er.pNext = &e13; e13.pNext = &e12;
        VkPhysicalDeviceFeatures2 e2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &er, .features = { .robustBufferAccess = VK_TRUE } };
        const char *device_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_EXT_robustness2" };
        VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &e2, .queueCreateInfoCount = queue_count, .pQueueCreateInfos = queues, .enabledExtensionCount = 2, .ppEnabledExtensionNames = device_exts };
        VkDevice device = VK_NULL_HANDLE;
        VkResult dvr = vkCreateDevice(gpu, &dci, NULL, &device);
        LOGI("DXVK 2.4.1 baseline vkCreateDevice: %s (%d)", dvr == VK_SUCCESS ? "PASS" : "FAIL", dvr);
        if (dvr == VK_SUCCESS) {
            PFN_vkCreateSwapchainKHR create_sc = (PFN_vkCreateSwapchainKHR)vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
            PFN_vkDestroySwapchainKHR destroy_sc = (PFN_vkDestroySwapchainKHR)vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR");
            PFN_vkGetSwapchainImagesKHR get_sc_images = (PFN_vkGetSwapchainImagesKHR)vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR");
            VkSurfaceFormatKHR formats[32]; uint32_t format_count = 0;
            VkResult fr = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, NULL);
            if (fr == VK_SUCCESS && format_count) { if (format_count > 32) format_count = 32; fr = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, formats); }
            VkSwapchainKHR sc = VK_NULL_HANDLE; VkResult sr = VK_ERROR_INITIALIZATION_FAILED;
            bool can_transfer = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0;
            LOGI("surface supportedUsageFlags=0x%x transfer-dst=%s", caps.supportedUsageFlags, can_transfer ? "YES" : "NO");
            if (create_sc && destroy_sc && get_sc_images && fr == VK_SUCCESS && format_count && can_transfer) {
                VkExtent2D extent = caps.currentExtent;
                if (extent.width == UINT32_MAX) { extent.width = caps.minImageExtent.width; extent.height = caps.minImageExtent.height; }
                VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
                if (!(caps.supportedCompositeAlpha & alpha)) { const VkCompositeAlphaFlagBitsKHR opts[] = { VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR }; for (unsigned k=0;k<3;k++) if (caps.supportedCompositeAlpha & opts[k]) { alpha=opts[k]; break; } }
                uint32_t image_count = caps.minImageCount; if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
                uint32_t family_indices[] = { graphics_family, present_family };
                VkSwapchainCreateInfoKHR sci = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface, .minImageCount = image_count, .imageFormat = formats[0].format, .imageColorSpace = formats[0].colorSpace, .imageExtent = extent, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT, .imageSharingMode = graphics_family == present_family ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT, .queueFamilyIndexCount = graphics_family == present_family ? 0 : 2, .pQueueFamilyIndices = graphics_family == present_family ? NULL : family_indices, .preTransform = caps.currentTransform, .compositeAlpha = alpha, .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE };
                sr = create_sc(device, &sci, NULL, &sc);
                LOGI("DXVK 2.4.1 baseline vkCreateSwapchainKHR: %s (%d), extent=%ux%u", sr == VK_SUCCESS ? "PASS" : "FAIL", sr, extent.width, extent.height);
            } else {
                LOGI("DXVK 2.4.1 present test: SKIPPED (missing transfer-dst surface usage or swapchain entry points)");
            }
            if (sr == VK_SUCCESS && sc) {
                uint32_t nimages=0;
                VkResult ir=get_sc_images(device, sc, &nimages, NULL);
                LOGI("swapchain image enumeration: %s (%d), images=%u", ir==VK_SUCCESS && nimages ? "PASS" : "FAIL", ir, nimages);
                if (ir == VK_SUCCESS && nimages > 0 && nimages <= 16) {
                    VkImage images[16];
                    uint32_t actual=nimages;
                    ir=get_sc_images(device, sc, &actual, images);
                    if (ir == VK_SUCCESS && actual > 0) {
                        VkQueue graphics_queue=VK_NULL_HANDLE, present_queue=VK_NULL_HANDLE;
                        vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
                        vkGetDeviceQueue(device, present_family, 0, &present_queue);
                        VkSemaphoreCreateInfo sem_info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                        VkSemaphore acquired=VK_NULL_HANDLE, rendered=VK_NULL_HANDLE;
                        VkResult pr=vkCreateSemaphore(device,&sem_info,NULL,&acquired);
                        if(pr==VK_SUCCESS) pr=vkCreateSemaphore(device,&sem_info,NULL,&rendered);
                        uint32_t image_index=0;
                        if(pr==VK_SUCCESS) {
                            pr=vkAcquireNextImageKHR(device,sc,UINT64_MAX,acquired,VK_NULL_HANDLE,&image_index);
                            if(pr==VK_SUBOPTIMAL_KHR) pr=VK_SUCCESS;
                        }
                        VkCommandPool pool=VK_NULL_HANDLE;
                        VkCommandBuffer cmd=VK_NULL_HANDLE;
                        if(pr==VK_SUCCESS) {
                            VkCommandPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,.queueFamilyIndex=graphics_family};
                            pr=vkCreateCommandPool(device,&pci,NULL,&pool);
                        }
                        if(pr==VK_SUCCESS) {
                            VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
                            pr=vkAllocateCommandBuffers(device,&cai,&cmd);
                        }
                        if(pr==VK_SUCCESS) {
                            VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
                            pr=vkBeginCommandBuffer(cmd,&bi);
                        }
                        VkImageSubresourceRange range={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.baseMipLevel=0,.levelCount=1,.baseArrayLayer=0,.layerCount=1};
                        if(pr==VK_SUCCESS) {
                            VkImageMemoryBarrier to_clear={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=0,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[image_index],.subresourceRange=range};
                            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&to_clear);
                            VkClearColorValue color={{0.06f,0.22f,0.48f,1.0f}};
                            vkCmdClearColorImage(cmd,images[image_index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
                            VkImageMemoryBarrier to_present={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=0,.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[image_index],.subresourceRange=range};
                            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&to_present);
                            pr=vkEndCommandBuffer(cmd);
                        }
                        if(pr==VK_SUCCESS) {
                            VkPipelineStageFlags wait_stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
                            VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&acquired,.pWaitDstStageMask=&wait_stage,.commandBufferCount=1,.pCommandBuffers=&cmd,.signalSemaphoreCount=1,.pSignalSemaphores=&rendered};
                            pr=vkQueueSubmit(graphics_queue,1,&submit,VK_NULL_HANDLE);
                        }
                        if(pr==VK_SUCCESS) {
                            VkPresentInfoKHR present={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.waitSemaphoreCount=1,.pWaitSemaphores=&rendered,.swapchainCount=1,.pSwapchains=&sc,.pImageIndices=&image_index};
                            pr=vkQueuePresentKHR(present_queue,&present);
                            if(pr==VK_SUBOPTIMAL_KHR) pr=VK_SUCCESS;
                        }
                        if(present_queue) vkQueueWaitIdle(present_queue);
                        LOGI("Android Vulkan clear + vkQueuePresentKHR: %s (%d)",pr==VK_SUCCESS?"PASS":"FAIL",pr);
                        if(pool) vkDestroyCommandPool(device,pool,NULL);
                        if(acquired) vkDestroySemaphore(device,acquired,NULL);
                        if(rendered) vkDestroySemaphore(device,rendered,NULL);
                    } else LOGI("present test: FAIL (could not fetch swapchain images, VkResult %d)",ir);
                } else LOGI("present test: FAIL (invalid swapchain image count, VkResult %d)",ir);
                destroy_sc(device, sc, NULL);
            }
            vkDeviceWaitIdle(device); vkDestroyDevice(device, NULL);
        }
    } else LOGI("DXVK 2.4.1 baseline device/swapchain smoke test: SKIPPED (prerequisite missing)");
    LOGI("NOTE: Vulkan device/swapchain smoke test only; DXVK itself is not linked or run.");
    (void)instance;
}

static void probe(ANativeWindow *window) {
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
        .window = window };
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


JNIEXPORT jstring JNICALL
Java_com_human9283_themoviesprobe_MainActivity_runProbe(JNIEnv *env, jobject thiz, jobject surface) {
    (void)thiz;
    g_report_len = 0;
    g_report[0] = '\0';
    LOGI("The Movies Vulkan Probe 0.3");
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        LOGE("Could not obtain Android surface window.");
        return (*env)->NewStringUTF(env, g_report);
    }
    probe(window);
    ANativeWindow_release(window);
    return (*env)->NewStringUTF(env, g_report);
}
