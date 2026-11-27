/*
 * tm_user32.c - user32 imports of Movies.exe 1.1 that win32hle does not
 * answer.
 *
 * MessageBoxW: there is no display yet, so the box is printed and answered
 * at once. The answer follows the reference port's rule (themovies
 * docs/bringup.md, 15): No when the box has one (Yes could start something,
 * like opening the Readme), else the first button. Each answer is printed, so
 * a run never makes a choice nobody can see.
 */
#include <stdint.h>
#include <stdio.h>
#include "win32hle.h"
#include "tm_user32.h"

#define IDOK     1
#define IDCANCEL 2
#define IDABORT  3
#define IDNO     7

/* A guest UTF-16 string as UTF-8, cut at n bytes. */
static void guest_wstr(uint32_t va, char *out, size_t n) {
    size_t o = 0;
    if (!va) { out[0] = 0; return; }
    for (uint32_t i = 0; o + 4 < n; i++) {
        uint32_t c = MEM16(va + 2u * i);
        if (!c) break;
        if (c >= 0xD800 && c < 0xDC00) {                 /* surrogate pair */
            uint32_t lo = MEM16(va + 2u * (i + 1));
            if (lo >= 0xDC00 && lo < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i++; }
        }
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = (char)(0xC0 | c >> 6); out[o++] = (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out[o++] = (char)(0xE0 | c >> 12); out[o++] = (char)(0x80 | (c >> 6 & 0x3F));
                                out[o++] = (char)(0x80 | (c & 0x3F)); }
        else { out[o++] = (char)(0xF0 | c >> 18); out[o++] = (char)(0x80 | (c >> 12 & 0x3F));
               out[o++] = (char)(0x80 | (c >> 6 & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[o] = 0;
}

/* int MessageBoxW(HWND, LPCWSTR text, LPCWSTR caption, UINT type) */
static void u_MessageBoxW(void) {
    char text[2048], caption[512];
    guest_wstr(A32(1), text, sizeof text);
    guest_wstr(A32(2), caption, sizeof caption);
    uint32_t buttons = A32(3) & 0xF, answer;
    const char *name;
    switch (buttons) {
    case 3: case 4: answer = IDNO;     name = "No";     break;   /* YESNOCANCEL, YESNO */
    case 2:         answer = IDABORT;  name = "Abort";  break;   /* ABORTRETRYIGNORE */
    case 5:         answer = IDCANCEL; name = "Cancel"; break;   /* RETRYCANCEL */
    default:        answer = IDOK;     name = "OK";     break;   /* OK, OKCANCEL */
    }
    fprintf(stderr, "[MessageBoxW] type 0x%X caption \"%s\"\n  text \"%s\"\n  answered %s\n",
            A32(3), caption, text, name);
    RET(answer, 4);
}

const win32hle_shim tm_user32_shims[] = {
    { "MessageBoxW", u_MessageBoxW },
    { NULL, NULL }
};
