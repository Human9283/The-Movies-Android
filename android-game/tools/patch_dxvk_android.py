#!/usr/bin/env python3
"""Apply pinned Android WSI adaptations to the DXVK Native source tree."""

from pathlib import Path
import sys


def replace_once(source: str, old: str, new: str, label: str) -> str:
    old_count = source.count(old)
    new_count = source.count(new)

    # The original SDL window block intentionally remains inside #else.
    if new_count == 1:
        return source
    if old_count == 1 and new_count == 0:
        return source.replace(old, new, 1)

    raise SystemExit(
        f"{label}: expected one original anchor or one patched block "
        f"(old={old_count}, new={new_count})"
    )


def replace_build_once(source: str, old: str, new: str, label: str) -> str:
    old_count = source.count(old)
    new_count = source.count(new)
    if old_count == 1 and new_count == 0:
        return source.replace(old, new, 1)
    if old_count == 0 and new_count == 1:
        return source
    raise SystemExit(
        f"{label}: expected one original or one patched setting "
        f"(old={old_count}, new={new_count})"
    )


def patch_sources(root: Path) -> tuple[str, str, str, str]:
    wsi_path = root / "src/dxvk/platform/dxvk_sdl2_exts.cpp"
    swapchain_path = root / "src/d3d9/d3d9_swapchain.cpp"
    presenter_path = root / "src/vulkan/vulkan_presenter.cpp"
    build_path = root / "android/build-android.sh"

    for path in (wsi_path, swapchain_path, presenter_path, build_path):
        if not path.is_file():
            raise SystemExit(f"required pinned DXVK source file not found: {path}")

    # Read and validate every file before writing any of them. A source drift
    # must not leave a partially patched DXVK checkout.
    wsi = wsi_path.read_text(encoding="utf-8")
    swapchain = swapchain_path.read_text(encoding="utf-8")
    presenter = presenter_path.read_text(encoding="utf-8")
    build = build_path.read_text(encoding="utf-8")

    old_window = '''    SDL_Window* window = SDL_CreateWindow(
      "Dummy Window",
      SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED,
      1, 1,
      SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);'''
    new_window = '''#ifdef __ANDROID__
    SDL_Window* window = SDL_GetWindowFromID(1);
#else
''' + old_window + '''
#endif'''
    wsi = replace_once(wsi, old_window, new_window, "Android SDL window lookup")

    old_destroy = "    SDL_DestroyWindow(window);"
    new_destroy = "#ifndef __ANDROID__\n" + old_destroy + "\n#endif"
    wsi = replace_once(wsi, old_destroy, new_destroy, "Android SDL window ownership")

    # VK_SUBOPTIMAL_KHR still permits presentation, but Android may use it to
    # signal that the surface transform changed. This app fixes the Activity
    # to one landscape orientation. Keep successful suboptimal presents
    # usable instead of entering SynchronizePresent's waitForIdle/recreate
    # path on every subsequent frame. This remains a constrained workaround:
    # full pre-rotation must align currentTransform, identity extent and the
    # actual blit transform. It is not implemented by this patch.
    old_acquire_status = '''      VkResult status = m_presenter->acquireNextImage(sync, imageIndex);
'''
    new_acquire_status = '''      VkResult status = m_presenter->acquireNextImage(sync, imageIndex);
      if (status == VK_SUBOPTIMAL_KHR) {
        static bool loggedAcquireSuboptimal = false;
        if (!loggedAcquireSuboptimal) {
          Logger::warn("D3D9SwapChainEx: vkAcquireNextImageKHR returned VK_SUBOPTIMAL_KHR");
          loggedAcquireSuboptimal = true;
        }
      }
'''
    swapchain = replace_once(
        swapchain, old_acquire_status, new_acquire_status,
        "Android suboptimal acquire diagnostics"
    )

    old_acquire_error = '''      while (status != VK_SUCCESS && status != VK_SUBOPTIMAL_KHR) {
        RecreateSwapChain(m_vsync);'''
    new_acquire_error = '''      while (status != VK_SUCCESS && status != VK_SUBOPTIMAL_KHR) {
        static uint32_t androidAcquireErrorLogs = 0;
        if (androidAcquireErrorLogs < 32) {
          androidAcquireErrorLogs++;
          Logger::warn(str::format(
            "D3D9SwapChainEx: vkAcquireNextImageKHR failed #",
            androidAcquireErrorLogs, " with VkResult ", status,
            "; recreating swapchain"));
        }
        RecreateSwapChain(m_vsync);'''
    swapchain = replace_once(
        swapchain, old_acquire_error, new_acquire_error,
        "Android acquire failure diagnostics"
    )

    old_recreate_flag = "    bool recreate = false;"
    new_recreate_flag = """    bool presenterMissing = m_presenter == nullptr;
    bool windowChanged = window != m_window;
    bool dialogChanged = m_dialog != m_lastDialog;
    bool recreate = presenterMissing || windowChanged || dialogChanged;"""
    swapchain = replace_once(
        swapchain, old_recreate_flag, new_recreate_flag,
        "Android swapchain change reason flags"
    )

    old_vsync_dirty = "    m_dirty    |= vsync != m_vsync;"
    new_vsync_dirty = """    bool vsyncChanged = vsync != m_vsync;
    m_dirty    |= vsyncChanged;"""
    swapchain = replace_once(
        swapchain, old_vsync_dirty, new_vsync_dirty,
        "Android swapchain vsync reason flag"
    )

    old_region_dirty = "    m_dirty    |= UpdatePresentRegion(pSourceRect, pDestRect);"
    new_region_dirty = """    bool presentRegionChanged = UpdatePresentRegion(pSourceRect, pDestRect);
    m_dirty    |= presentRegionChanged;"""
    swapchain = replace_once(
        swapchain, old_region_dirty, new_region_dirty,
        "Android swapchain present-region reason flag"
    )

    old_missing_swapchain = '''    m_dirty    |= m_presenter != nullptr &&
                 !m_presenter->hasSwapChain();'''
    new_missing_swapchain = '''    bool swapChainMissing = m_presenter != nullptr &&
                            !m_presenter->hasSwapChain();
    m_dirty    |= swapChainMissing;'''
    swapchain = replace_once(
        swapchain, old_missing_swapchain, new_missing_swapchain,
        "Android swapchain missing-surface reason flag"
    )

    old_log_anchor = "    m_lastDialog = m_dialog;"
    new_log_anchor = """    m_lastDialog = m_dialog;

    static uint32_t androidSwapchainChangeLogs = 0;
    if ((recreate || vsyncChanged || presentRegionChanged || swapChainMissing)
        && androidSwapchainChangeLogs < 32) {
      androidSwapchainChangeLogs++;
      Logger::warn(str::format(
        "D3D9SwapChainEx: swapchain change #", androidSwapchainChangeLogs,
        " presenterMissing=", presenterMissing ? 1u : 0u,
        " windowChanged=", windowChanged ? 1u : 0u,
        " dialogChanged=", dialogChanged ? 1u : 0u,
        " vsyncChanged=", vsyncChanged ? 1u : 0u,
        " presentRegionChanged=", presentRegionChanged ? 1u : 0u,
        " swapChainMissing=", swapChainMissing ? 1u : 0u,
        " src=", m_srcRect.right - m_srcRect.left, "x", m_srcRect.bottom - m_srcRect.top,
        " dst=", m_dstRect.right - m_dstRect.left, "x", m_dstRect.bottom - m_dstRect.top));
    }"""
    swapchain = replace_once(
        swapchain, old_log_anchor, new_log_anchor,
        "Android swapchain reason log"
    )

    old_acquire_status = "      VkResult status = m_presenter->acquireNextImage(sync, imageIndex);"
    new_acquire_status = """      VkResult status = m_presenter->acquireNextImage(sync, imageIndex);
      if (status == VK_SUBOPTIMAL_KHR) {
        static bool loggedAcquireSuboptimal = false;
        if (!loggedAcquireSuboptimal) {
          Logger::warn("D3D9SwapChainEx: vkAcquireNextImageKHR returned VK_SUBOPTIMAL_KHR");
          loggedAcquireSuboptimal = true;
        }
      }"""
    swapchain = replace_once(
        swapchain, old_acquire_status, new_acquire_status,
        "Android suboptimal acquire diagnostics"
    )

    old_acquire_error = """      while (status != VK_SUCCESS && status != VK_SUBOPTIMAL_KHR) {
        RecreateSwapChain(m_vsync);"""
    new_acquire_error = """      while (status != VK_SUCCESS && status != VK_SUBOPTIMAL_KHR) {
        static uint32_t androidAcquireErrorLogs = 0;
        if (androidAcquireErrorLogs < 32) {
          androidAcquireErrorLogs++;
          Logger::warn(str::format(
            "D3D9SwapChainEx: vkAcquireNextImageKHR failed #",
            androidAcquireErrorLogs, " with VkResult ", status,
            "; recreating swapchain"));
        }
        RecreateSwapChain(m_vsync);"""
    swapchain = replace_once(
        swapchain, old_acquire_error, new_acquire_error,
        "Android acquire failure diagnostics"
    )

    old_present = '''    if (status != VK_SUCCESS)
      RecreateSwapChain(m_vsync);'''
    new_present = '''    if (status == VK_SUBOPTIMAL_KHR) {
      static bool loggedSuboptimal = false;
      if (!loggedSuboptimal) {
        Logger::warn("D3D9SwapChainEx: VK_SUBOPTIMAL_KHR present; continuing with the fixed-landscape workaround");
        loggedSuboptimal = true;
      }
    } else if (status != VK_SUCCESS) {
      static bool loggedPresentError = false;
      if (!loggedPresentError) {
        Logger::warn(str::format("D3D9SwapChainEx: present failed with VkResult ", status, "; recreating swapchain"));
        loggedPresentError = true;
      }
      RecreateSwapChain(m_vsync);
    }'''
    swapchain = replace_once(
        swapchain, old_present, new_present,
        "VK_SUBOPTIMAL_KHR presentation handling"
    )

    old_surface = "    // Select actual swap chain properties and create swap chain"
    new_surface = '''    Logger::info(str::format(
      "Presenter: Surface capabilities:"
      "\\n  Current extent: ", caps.currentExtent.width, "x", caps.currentExtent.height,
      "\\n  Min extent: ", caps.minImageExtent.width, "x", caps.minImageExtent.height,
      "\\n  Max extent: ", caps.maxImageExtent.width, "x", caps.maxImageExtent.height,
      "\\n  Current transform: ", caps.currentTransform,
      "\\n  Supported transforms: ", caps.supportedTransforms,
      "\\n  Desired extent: ", desc.imageExtent.width, "x", desc.imageExtent.height));

''' + old_surface
    presenter = replace_once(
        presenter, old_surface, new_surface,
        "Android Vulkan surface capability diagnostics"
    )

    old_presenter_log = '''      "\\n  Buffer size:  ", m_info.imageExtent.width, "x", m_info.imageExtent.height,
      "\\n  Image count:  ", m_info.imageCount,'''
    new_presenter_log = '''      "\\n  Buffer size:  ", m_info.imageExtent.width, "x", m_info.imageExtent.height,
      "\\n  Pre-transform: ", swapInfo.preTransform,
      "\\n  Image count:  ", m_info.imageCount,'''
    presenter = replace_once(
        presenter, old_presenter_log, new_presenter_log,
        "Android Vulkan selected pre-transform diagnostic"
    )

    # The Gradle app loads DXVK and the native host in one process and packages
    # the NDK's libc++_shared.so. Both libraries must use that same C++ runtime.
    build = replace_build_once(build, "c = '$CC'", "c = ['ccache', '$CC']", "C compiler launcher")
    build = replace_build_once(build, "cpp = '$CXX'", "cpp = ['ccache', '$CXX']", "C++ compiler launcher")

    static_runtime = "'-static-libstdc++', "
    static_count = build.count(static_runtime)
    if static_count == 2:
        build = build.replace(static_runtime, "")
    elif static_count != 0:
        raise SystemExit(
            "unexpected number of static C++ runtime flags in pinned DXVK build script "
            f"(count={static_count})"
        )
    if "'-static-libstdc++'" in build:
        raise SystemExit("DXVK still links its own static C++ runtime")

    return wsi, swapchain, presenter, build


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} DXVK_SOURCE_ROOT")

    root = Path(sys.argv[1])
    wsi_path = root / "src/dxvk/platform/dxvk_sdl2_exts.cpp"
    swapchain_path = root / "src/d3d9/d3d9_swapchain.cpp"
    presenter_path = root / "src/vulkan/vulkan_presenter.cpp"
    build_path = root / "android/build-android.sh"

    wsi, swapchain, presenter, build = patch_sources(root)

    # All source anchors and build settings have been validated. Only now
    # modify the checkout, so source drift fails without partial mutation.
    for path, content in (
        (wsi_path, wsi),
        (swapchain_path, swapchain),
        (presenter_path, presenter),
        (build_path, build),
    ):
        if path.read_text(encoding="utf-8") != content:
            path.write_text(content, encoding="utf-8")

    print("Applied Android SDL WSI and suboptimal-present patches")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
