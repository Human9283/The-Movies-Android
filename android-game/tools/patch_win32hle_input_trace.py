#!/usr/bin/env python3
"""Enable bounded SDL and Win32 input tracing in the pinned win32hle runtime."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
screen = root / "screen.c"
user32 = root / "user32.c"

s = screen.read_text()
old = """#ifdef __EMSCRIPTEN__
        if (getenv("RA2_INPUT_TRACE") || g_input_trace) {
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) fprintf(stderr, "[input] key %s scancode %d repeat %d\\n", e.type == SDL_KEYDOWN ? "down" : "up", e.key.keysym.scancode, e.key.repeat);
            else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) fprintf(stderr, "[input] button %d %s at %d,%d\\n", e.button.button, e.type == SDL_MOUSEBUTTONDOWN ? "down" : "up", e.button.x, e.button.y);
        }
#endif
"""
new = """        if (getenv("RA2_INPUT_TRACE") || g_input_trace) {
            static unsigned motion_events, touch_motion_events;
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
                fprintf(stderr, "[sdl-input] key %s scancode=%d repeat=%d\\n",
                    e.type == SDL_KEYDOWN ? "down" : "up", e.key.keysym.scancode, e.key.repeat);
            else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP)
                fprintf(stderr, "[sdl-input] mouse-button=%u %s at=%d,%d\\n",
                    e.button.button, e.type == SDL_MOUSEBUTTONDOWN ? "down" : "up", e.button.x, e.button.y);
            else if (e.type == SDL_MOUSEMOTION && (++motion_events <= 12 || motion_events % 120 == 0))
                fprintf(stderr, "[sdl-input] mouse-motion at=%d,%d relative=%d,%d sample=%u\\n",
                    e.motion.x, e.motion.y, e.motion.xrel, e.motion.yrel, motion_events);
            else if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP)
                fprintf(stderr, "[sdl-input] finger %s id=%lld at=%.4f,%.4f\\n",
                    e.type == SDL_FINGERDOWN ? "down" : "up",
                    (long long)e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
            else if (e.type == SDL_FINGERMOTION && (++touch_motion_events <= 12 || touch_motion_events % 120 == 0))
                fprintf(stderr, "[sdl-input] finger-motion id=%lld at=%.4f,%.4f sample=%u\\n",
                    (long long)e.tfinger.fingerId, e.tfinger.x, e.tfinger.y, touch_motion_events);
            else if (e.type == SDL_WINDOWEVENT &&
                     (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED))
                fprintf(stderr, "[sdl-input] window size=%dx%d\\n", e.window.data1, e.window.data2);
        }
"""
if s.count(old) != 1:
    raise SystemExit(f"screen.c trace block expected once, found {s.count(old)}")
screen.write_text(s.replace(old, new))

u = user32.read_text()
old_mouse = """    g_cursor_x = sx, g_cursor_y = sy;
    uint32_t vk = message == WM_LBUTTONDOWN"""
new_mouse = """    g_cursor_x = sx, g_cursor_y = sy;
    if (getenv("RA2_INPUT_TRACE") && message != WM_MOUSEMOVE)
        fprintf(stderr, "[win32-input] mouse message=0x%04X at=%d,%d\\n", message, sx, sy);
    uint32_t vk = message == WM_LBUTTONDOWN"""
if u.count(old_mouse) != 1:
    raise SystemExit(f"user32.c mouse hook expected once, found {u.count(old_mouse)}")
u = u.replace(old_mouse, new_mouse)

old_async = """static void u_GetAsyncKeyState(void) {
    if ((A32(0) & 0xFF) == 1) hle_lbutton_reads++;
    RET(g_keys[A32(0) & 0xFF] & 0x80 ? 0x8000u : 0u, 1);
}"""
new_async = """static void u_GetAsyncKeyState(void) {
    uint32_t vk = A32(0) & 0xFF;
    uint32_t state = g_keys[vk] & 0x80 ? 0x8000u : 0u;
    if (vk == 1) {
        static unsigned reads;
        hle_lbutton_reads++;
        if (getenv("RA2_INPUT_TRACE") && (++reads <= 24 || reads % 120 == 0))
            fprintf(stderr, "[win32-input] GetAsyncKeyState(VK_LBUTTON)=%04X sample=%u\\n", state, reads);
    }
    RET(state, 1);
}"""
if u.count(old_async) != 1:
    raise SystemExit(f"user32.c async-key hook expected once, found {u.count(old_async)}")
user32.write_text(u.replace(old_async, new_async))
print("Patched win32hle: bounded SDL touch/mouse events and translated Win32 input tracing")
