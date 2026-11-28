/*
 * tm_dshow_reader.c - the WMV video source filter used by The Movies.
 *
 * This is a deliberately narrow DirectShow source: one video output pin,
 * decoded to bottom-up RGB24 by tm_asf_decoder (FFmpeg). Audio streams are
 * ignored. The filter implements the COM surface required by the game's
 * TEXTURERENDERER path rather than pretending that RenderFile succeeded.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

#include "win32hle.h"
#include "tm_asf_decoder.h"
#include "tm_dshow_reader.h"

#define S_OK                    0x00000000u
#define S_FALSE                 0x00000001u
#define E_NOINTERFACE           0x80004002u
#define E_POINTER               0x80004003u
#define E_FAIL                  0x80004005u
#define E_NOTIMPL               0x80004001u
#define E_INVALIDARG            0x80070057u
#define E_OUTOFMEMORY           0x8007000Eu
#define E_UNEXPECTED            0x8000FFFFu
#define VFW_E_CANNOT_RENDER     0x80040218u
#define VFW_E_NOT_CONNECTED     0x80040209u
#define VFW_E_TYPE_NOT_ACCEPTED  0x8004022Au
#define VFW_E_INVALID_DIRECTION  0x80040208u
#define VFW_E_ALREADY_CONNECTED  0x80040204u
#define VFW_E_ENUM_OUT_OF_SYNC   0x80040203u
#define VFW_E_NOT_STOPPED        0x80040224u
#define EC_COMPLETE              0x00000001u
#define AM_GBF_NOWAIT            0x00000004u
#define PINDIR_OUTPUT            1
#define FILTER_STATE_STOPPED     0u
#define FILTER_STATE_PAUSED      1u
#define FILTER_STATE_RUNNING     2u
#define REFERENCE_TIME_PER_SEC   10000000ll
#define MAX_READER_OBJECTS       16
#define MAX_PIN_ENUMS            32
#define MAX_TYPE_ENUMS           32
#define MAX_VIDEO_DIMENSION      4096

typedef uint8_t guid_t;
#define GUID(d1,d2,d3,b0,b1,b2,b3,b4,b5,b6,b7) \
    { (uint8_t)((d1) & 0xff), (uint8_t)(((d1) >> 8) & 0xff), \
      (uint8_t)(((d1) >> 16) & 0xff), (uint8_t)(((d1) >> 24) & 0xff), \
      (uint8_t)((d2) & 0xff), (uint8_t)(((d2) >> 8) & 0xff), \
      (uint8_t)((d3) & 0xff), (uint8_t)(((d3) >> 8) & 0xff), \
      b0,b1,b2,b3,b4,b5,b6,b7 }

static const uint8_t CLSID_WMAsfReader[16] =
    GUID(0x187463A0,0x5BB7,0x11D3,0xAC,0xBE,0x00,0x80,0xC7,0x5E,0x24,0x6E);
static const uint8_t IID_IUnknown[16] =
    GUID(0,0,0,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46);
static const uint8_t IID_IPersist[16] =
    GUID(0x0000010C,0,0,0xC0,0,0,0,0,0,0,0x46);
static const uint8_t IID_IMediaFilter[16] =
    GUID(0x56A86899,0x0AD4,0x11CE,0xB0,0x3A,0,0x20,0xAF,0x0B,0xA7,0x70);
static const uint8_t IID_IBaseFilter[16] =
    GUID(0x56A86895,0x0AD4,0x11CE,0xB0,0x3A,0,0x20,0xAF,0x0B,0xA7,0x70);
static const uint8_t IID_IFileSourceFilter[16] =
    GUID(0x56A868A6,0x0AD4,0x11CE,0xB0,0x3A,0,0x20,0xAF,0x0B,0xA7,0x70);
static const uint8_t IID_IPin[16] =
    GUID(0x56A86891,0x0AD4,0x11CE,0xB0,0x3A,0,0x20,0xAF,0x0B,0xA7,0x70);
static const uint8_t IID_IEnumPins[16] =
    GUID(0x56A86892,0x0AD4,0x11CE,0xB0,0x3A,0,0x20,0xAF,0x0B,0xA7,0x70);
static const uint8_t IID_IEnumMediaTypes[16] =
    GUID(0x89C31040,0x0AD4,0x11CE,0x97,0xD3,0x00,0xAA,0x00,0x55,0x59,0x5A);
static const uint8_t IID_IMemInputPin[16] =
    GUID(0x56A8689D,0x0AD4,0x11CE,0xB0,0x3A,0,0x20,0xAF,0x0B,0xA7,0x70);
static const uint8_t MEDIATYPE_Video[16] =
    GUID(0x73646976,0,0x0010,0x80,0,0,0xAA,0,0x38,0x9B,0x71);
static const uint8_t MEDIASUBTYPE_RGB24[16] =
    GUID(0xE436EB7D,0x524F,0x11CE,0x9F,0x53,0,0x20,0xAF,0x0B,0xA7,0x70);
static const uint8_t FORMAT_VideoInfo[16] =
    GUID(0x05589F80,0xC356,0x11CE,0xBF,0x01,0,0xAA,0,0x55,0x59,0x5A);

typedef struct reader reader_t;
typedef struct pin_enum pin_enum_t;
typedef struct type_enum type_enum_t;

struct reader {
    uint32_t filter_va;
    uint32_t file_va;
    uint32_t pin_va;
    uint32_t graph_va;
    uint32_t clock_va;
    int refs;
    int width, height, stride;
    uint32_t sample_size;
    double duration_seconds;
    double avg_frame_seconds;
    uint32_t state;
    uint64_t run_start_100ns;
    atomic_int worker_done;
    uint16_t filter_name[128];
    uint16_t current_path[PATH_MAX];
    char host_path[PATH_MAX];
    tm_asf_decoder *decoder;
    tm_asf_video_info video;
    uint32_t media_type_va;
    uint32_t format_va;
    uint32_t peer_pin;
    uint32_t peer_mem_input;
    uint32_t allocator;
    int allocator_committed;
    pthread_t worker;
    uint32_t worker_tid;
    int worker_started;
    atomic_int stop_requested;
    atomic_int paused;
    atomic_int running;
    int looping;
    int pin_flushing;
};

struct pin_enum {
    uint32_t va;
    int refs;
    uint32_t cursor;
    reader_t *reader;
};
struct type_enum {
    uint32_t va;
    int refs;
    uint32_t cursor;
    reader_t *reader;
};

static reader_t *g_readers[MAX_READER_OBJECTS];
static pin_enum_t g_pin_enums[MAX_PIN_ENUMS];
static type_enum_t g_type_enums[MAX_TYPE_ENUMS];
static uint32_t g_filter_vt, g_file_vt, g_pin_vt, g_pin_enum_vt, g_type_enum_vt;

static uint32_t guest_alloc_locked(uint32_t size) {
    mach_enter();
    void *p = hle_alloc(size);
    uint32_t va = p ? hle_va(p) : 0;
    mach_leave();
    return va;
}
static void guest_free_locked(uint32_t va) {
    if (!va) return;
    mach_enter();
    hle_free(hle_ptr(va));
    mach_leave();
}
static uint32_t guest_read32_locked(uint32_t va) {
    mach_enter();
    uint32_t value = MEM32(va);
    mach_leave();
    return value;
}
static void guest_write32_locked(uint32_t va, uint32_t value) {
    mach_enter();
    MEM32(va) = value;
    mach_leave();
}
static void guest_copy_locked(uint32_t dst, const void *src, size_t size) {
    mach_enter();
    memcpy(hle_ptr(dst), src, size);
    mach_leave();
}
static uint32_t guest_call(uint32_t object, uint32_t slot, int nargs, const uint32_t *args) {
    uint32_t call_args[8] = { object };
    if (nargs < 0 || nargs > 7 || !object) return E_INVALIDARG;
    for (int i = 0; i < nargs; i++) call_args[i + 1] = args[i];

    /* The decoder worker calls into guest COM independently of the main
     * SDL thread. Resolve the guest vtable entry while holding the HLE
     * machine lock; hle_call_guest uses the same re-entrant lock itself. */
    mach_enter();
    uint32_t vt = MEM32(object);
    uint32_t method = vt ? MEM32(vt + slot * 4u) : 0;
    mach_leave();
    if (!method) return E_UNEXPECTED;
    return hle_call_guest(method, nargs + 1, call_args);
}

static reader_t *reader_by_interface(uint32_t va) {
    if (!va) return NULL;
    for (int i = 0; i < MAX_READER_OBJECTS; i++) {
        reader_t *r = g_readers[i];
        if (r && (r->filter_va == va || r->file_va == va || r->pin_va == va))
            return r;
    }
    return NULL;
}

static reader_t *reader_from_this(const char *method) {
    uint32_t va = A32(0);
    reader_t *r = reader_by_interface(va);
    if (!r) hle_fatal("[dshow-reader] %s on non-live interface 0x%08X", method, va);
    return r;
}

static void reader_addref(reader_t *r) {
    __atomic_add_fetch(&r->refs, 1, __ATOMIC_RELAXED);
}

static void reader_stop_worker(reader_t *r);

static void reader_disconnect(reader_t *r) {
    reader_stop_worker(r);
    if (r->allocator) {
        if (r->allocator_committed) {
            (void)guest_call(r->allocator, 6, 0, NULL);  /* IMemAllocator::Decommit */
            r->allocator_committed = 0;
        }
        guest_call(r->allocator, 2, 0, NULL);
        r->allocator = 0;
    }
    if (r->peer_mem_input) {
        guest_call(r->peer_mem_input, 2, 0, NULL);
        r->peer_mem_input = 0;
    }
    if (r->peer_pin) {
        guest_call(r->peer_pin, 2, 0, NULL);
        r->peer_pin = 0;
    }
    r->pin_flushing = 0;
}

static void reader_destroy(reader_t *r) {
    reader_disconnect(r);
    if (r->clock_va) guest_call(r->clock_va, 2, 0, NULL);
    if (r->decoder) tm_asf_decoder_close(r->decoder);
    if (r->media_type_va) hle_free(hle_ptr(r->media_type_va));
    if (r->format_va) hle_free(hle_ptr(r->format_va));
    if (r->pin_va) hle_free(hle_ptr(r->pin_va));
    if (r->file_va) hle_free(hle_ptr(r->file_va));
    if (r->filter_va) hle_free(hle_ptr(r->filter_va));
    for (int i = 0; i < MAX_READER_OBJECTS; i++)
        if (g_readers[i] == r) g_readers[i] = NULL;
    fprintf(stderr, "[dshow-reader] destroyed filter 0x%08X\n", r->filter_va);
    free(r);
}

static uint32_t reader_release_host(reader_t *r) {
    int refs = __atomic_sub_fetch(&r->refs, 1, __ATOMIC_ACQ_REL);
    if (refs < 0) hle_fatal("[dshow-reader] reference count underflow");
    if (!refs) {
        /* The last Release may be the worker's self-reference after an
         * EC_COMPLETE callback removed the filter. Destruction frees guest
         * interface storage, so it must run under the HLE machine lock. */
        mach_enter();
        reader_destroy(r);
        mach_leave();
    }
    return (uint32_t)refs;
}

static int wide_to_utf8(const uint16_t *src, char *dst, size_t capacity) {
    size_t out = 0;
    if (!src || !dst || !capacity) return 0;
    for (size_t i = 0; src[i]; i++) {
        uint32_t cp = src[i];
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            uint32_t lo = src[i + 1];
            if (lo < 0xDC00 || lo > 0xDFFF) return 0;
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
            i++;
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return 0;
        }
        uint8_t bytes[4];
        size_t n;
        if (cp < 0x80) bytes[0] = (uint8_t)cp, n = 1;
        else if (cp < 0x800) {
            bytes[0] = 0xC0u | (uint8_t)(cp >> 6);
            bytes[1] = 0x80u | (uint8_t)(cp & 0x3Fu);
            n = 2;
        } else if (cp < 0x10000) {
            bytes[0] = 0xE0u | (uint8_t)(cp >> 12);
            bytes[1] = 0x80u | (uint8_t)((cp >> 6) & 0x3Fu);
            bytes[2] = 0x80u | (uint8_t)(cp & 0x3Fu);
            n = 3;
        } else {
            bytes[0] = 0xF0u | (uint8_t)(cp >> 18);
            bytes[1] = 0x80u | (uint8_t)((cp >> 12) & 0x3Fu);
            bytes[2] = 0x80u | (uint8_t)((cp >> 6) & 0x3Fu);
            bytes[3] = 0x80u | (uint8_t)(cp & 0x3Fu);
            n = 4;
        }
        if (out + n + 1 > capacity) return 0;
        memcpy(dst + out, bytes, n);
        out += n;
    }
    dst[out] = 0;
    return 1;
}

static int utf8_to_wide(const char *src, uint16_t *dst, size_t capacity) {
    size_t i = 0, out = 0;
    while (src[i]) {
        uint32_t cp;
        uint8_t c = (uint8_t)src[i++];
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0) {
            if (!src[i]) return 0;
            cp = ((uint32_t)(c & 0x1F) << 6) | ((uint8_t)src[i++] & 0x3F);
        } else if ((c & 0xF0) == 0xE0) {
            if (!src[i] || !src[i + 1]) return 0;
            cp = ((uint32_t)(c & 0x0F) << 12) |
                 (((uint32_t)(uint8_t)src[i] & 0x3F) << 6) |
                 ((uint8_t)src[i + 1] & 0x3F);
            i += 2;
        } else {
            if (!src[i] || !src[i + 1] || !src[i + 2]) return 0;
            cp = ((uint32_t)(c & 0x07) << 18) |
                 (((uint32_t)(uint8_t)src[i] & 0x3F) << 12) |
                 (((uint32_t)(uint8_t)src[i + 1] & 0x3F) << 6) |
                 ((uint8_t)src[i + 2] & 0x3F);
            i += 3;
        }
        if (cp <= 0xFFFF) {
            if (out + 1 >= capacity) return 0;
            dst[out++] = (uint16_t)cp;
        } else {
            if (out + 2 >= capacity || cp > 0x10FFFF) return 0;
            cp -= 0x10000;
            dst[out++] = (uint16_t)(0xD800u + (cp >> 10));
            dst[out++] = (uint16_t)(0xDC00u + (cp & 0x3FFu));
        }
    }
    if (out >= capacity) return 0;
    dst[out] = 0;
    return 1;
}

static uint32_t make_media_type(reader_t *r) {
    if (r->media_type_va) return r->media_type_va;
    uint8_t *mt = hle_alloc(72);
    uint8_t *fmt = hle_alloc(88);
    if (!mt || !fmt) {
        if (mt) hle_free(mt);
        if (fmt) hle_free(fmt);
        return 0;
    }
    memset(mt, 0, 72);
    memset(fmt, 0, 88);
    memcpy(mt, MEDIATYPE_Video, 16);
    memcpy(mt + 16, MEDIASUBTYPE_RGB24, 16);
    MEM32(hle_va(mt) + 32) = 1;                         /* fixed-size samples */
    MEM32(hle_va(mt) + 40) = r->sample_size;
    memcpy(mt + 44, FORMAT_VideoInfo, 16);
    MEM32(hle_va(mt) + 64) = 88;
    MEM32(hle_va(mt) + 68) = hle_va(fmt);

    /* VIDEOINFOHEADER: rcSource, rcTarget, bit rate, error rate,
     * AvgTimePerFrame, BITMAPINFOHEADER. */
    MEM32(hle_va(fmt) + 8) = (uint32_t)r->width;
    MEM32(hle_va(fmt) + 12) = (uint32_t)r->height;
    MEM32(hle_va(fmt) + 24) = (uint32_t)r->width;
    MEM32(hle_va(fmt) + 28) = (uint32_t)r->height;
    if (r->avg_frame_seconds > 0.0) {
        uint64_t avg = (uint64_t)(r->avg_frame_seconds * (double)REFERENCE_TIME_PER_SEC);
        MEM32(hle_va(fmt) + 40) = (uint32_t)avg;
        MEM32(hle_va(fmt) + 44) = (uint32_t)(avg >> 32);
    }
    MEM32(hle_va(fmt) + 48) = 40;                       /* biSize */
    MEM32(hle_va(fmt) + 52) = (uint32_t)r->width;       /* biWidth */
    MEM32(hle_va(fmt) + 56) = (uint32_t)r->height;      /* positive: bottom-up */
    MEM32(hle_va(fmt) + 60) = 1u | (24u << 16);         /* planes=1, bitCount=24 */
    MEM32(hle_va(fmt) + 64) = 0;                        /* BI_RGB */
    MEM32(hle_va(fmt) + 68) = r->sample_size;          /* biSizeImage */
    r->media_type_va = hle_va(mt);
    r->format_va = hle_va(fmt);
    return r->media_type_va;
}

static uint32_t media_type_clone(reader_t *r, uint32_t *out) {
    uint32_t src = make_media_type(r);
    if (!src) return E_OUTOFMEMORY;
    uint8_t *mt = hle_alloc(72);
    uint8_t *fmt = hle_alloc(88);
    if (!mt || !fmt) {
        if (mt) hle_free(mt);
        if (fmt) hle_free(fmt);
        return E_OUTOFMEMORY;
    }
    memcpy(mt, hle_ptr(src), 72);
    uint32_t src_fmt = MEM32(src + 68);
    memcpy(fmt, hle_ptr(src_fmt), 88);
    MEM32(hle_va(mt) + 68) = hle_va(fmt);
    *out = hle_va(mt);
    return S_OK;
}

static void media_type_free(uint32_t va) {
    if (!va) return;
    uint32_t fmt = MEM32(va + 68);
    if (fmt) hle_free(hle_ptr(fmt));
    hle_free(hle_ptr(va));
}

static uint32_t reader_load_path(reader_t *r, const uint16_t *wide_path) {
    char guest_path[PATH_MAX], host_path[PATH_MAX], error[256];
    if (!wide_to_utf8(wide_path, guest_path, sizeof guest_path))
        return E_INVALIDARG;
    if (!hle_host_path_base(guest_path, host_path, sizeof host_path))
        return 0x80070002u;                         /* HRESULT_FROM_WIN32(FILE_NOT_FOUND) */

    tm_asf_decoder *decoder = NULL;
    tm_asf_video_info info;
    int rc = tm_asf_decoder_open(host_path, &decoder, &info, error, sizeof error);
    if (rc < 0 || !decoder) {
        fprintf(stderr, "[dshow-reader] open %s failed: %s (%d)\n", host_path, error, rc);
        return VFW_E_CANNOT_RENDER;
    }
    if (info.width <= 0 || info.height <= 0 ||
        info.width > MAX_VIDEO_DIMENSION || info.height > MAX_VIDEO_DIMENSION) {
        tm_asf_decoder_close(decoder);
        fprintf(stderr, "[dshow-reader] invalid video size %dx%d in %s\n", info.width, info.height, host_path);
        return VFW_E_CANNOT_RENDER;
    }
    int stride = (info.width * 3 + 3) & ~3;
    uint64_t sample_size = (uint64_t)(uint32_t)stride * (uint64_t)(uint32_t)info.height;
    if (!sample_size || sample_size > 64u * 1024u * 1024u) {
        tm_asf_decoder_close(decoder);
        return VFW_E_CANNOT_RENDER;
    }

    reader_stop_worker(r);
    if (r->decoder) tm_asf_decoder_close(r->decoder);
    if (r->media_type_va) hle_free(hle_ptr(r->media_type_va)), r->media_type_va = 0;
    if (r->format_va) hle_free(hle_ptr(r->format_va)), r->format_va = 0;

    r->decoder = decoder;
    r->video = info;
    r->width = info.width;
    r->height = info.height;
    r->stride = stride;
    r->sample_size = (uint32_t)sample_size;
    r->duration_seconds = (info.duration >= 0 && info.time_base_den > 0)
        ? (double)info.duration * (double)info.time_base_num / (double)info.time_base_den
        : 0.0;
    r->avg_frame_seconds = (info.frame_rate_num > 0 && info.frame_rate_den > 0)
        ? (double)info.frame_rate_den / (double)info.frame_rate_num
        : 1.0 / 25.0;
    if (r->avg_frame_seconds < 0.001 || r->avg_frame_seconds > 1.0)
        r->avg_frame_seconds = 1.0 / 25.0;
    snprintf(r->host_path, sizeof r->host_path, "%s", host_path);
    size_t wlen = 0;
    while (wide_path[wlen] && wlen + 1 < PATH_MAX) wlen++;
    memcpy(r->current_path, wide_path, (wlen + 1) * sizeof(uint16_t));
    r->looping = (strcasestr(guest_path, "frontend_loop") != NULL ||
                  strcasestr(guest_path, "loop.wmv") != NULL);
    r->state = FILTER_STATE_STOPPED;
    fprintf(stderr, "[dshow-reader] loaded %s: %dx%d %.3f fps, duration %.3f s, loop=%d\n",
            host_path, r->width, r->height,
            r->avg_frame_seconds > 0 ? 1.0 / r->avg_frame_seconds : 0.0,
            r->duration_seconds, r->looping);
    return S_OK;
}

static uint32_t reader_build_output_pin_connection(reader_t *r, uint32_t peer, uint32_t requested_mt) {
    if (!r->decoder) return VFW_E_CANNOT_RENDER;
    if (r->peer_pin) return VFW_E_ALREADY_CONNECTED;
    if (!peer) return E_POINTER;
    if (requested_mt) {
        if (memcmp(hle_ptr(requested_mt), MEDIATYPE_Video, 16) ||
            memcmp((uint8_t *)hle_ptr(requested_mt) + 16, MEDIASUBTYPE_RGB24, 16))
            return VFW_E_TYPE_NOT_ACCEPTED;
    }
    uint32_t mt = make_media_type(r);
    if (!mt) return E_OUTOFMEMORY;

    uint32_t receive_args[2] = { r->pin_va, mt };
    uint32_t hr = guest_call(peer, 4, 2, receive_args);     /* IPin::ReceiveConnection */
    if (hr & 0x80000000u) {
        fprintf(stderr, "[dshow-reader] renderer ReceiveConnection -> 0x%08X\n", hr);
        return hr;
    }

    uint8_t *iid = hle_alloc(16);
    uint8_t *out = hle_alloc(4);
    if (!iid || !out) {
        if (iid) hle_free(iid);
        if (out) hle_free(out);
        guest_call(peer, 5, 0, NULL);                       /* IPin::Disconnect */
        return E_OUTOFMEMORY;
    }
    memcpy(iid, IID_IMemInputPin, 16);
    MEM32(hle_va(out)) = 0;
    uint32_t qi_args[2] = { hle_va(iid), hle_va(out) };
    hr = guest_call(peer, 0, 2, qi_args);
    uint32_t mem_input = MEM32(hle_va(out));
    hle_free(out);
    hle_free(iid);
    if ((hr & 0x80000000u) || !mem_input) {
        guest_call(peer, 5, 0, NULL);
        return (hr & 0x80000000u) ? hr : E_UNEXPECTED;
    }

    uint8_t *alloc_out = hle_alloc(4);
    if (!alloc_out) {
        guest_call(mem_input, 2, 0, NULL);
        guest_call(peer, 5, 0, NULL);
        return E_OUTOFMEMORY;
    }
    MEM32(hle_va(alloc_out)) = 0;
    uint32_t get_alloc_args[1] = { hle_va(alloc_out) };
    hr = guest_call(mem_input, 3, 1, get_alloc_args);        /* IMemInputPin::GetAllocator */
    uint32_t allocator = MEM32(hle_va(alloc_out));
    hle_free(alloc_out);
    if ((hr & 0x80000000u) || !allocator) {
        guest_call(mem_input, 2, 0, NULL);
        guest_call(peer, 5, 0, NULL);
        return (hr & 0x80000000u) ? hr : E_UNEXPECTED;
    }

    uint8_t *properties = hle_alloc(32);
    if (!properties) {
        guest_call(allocator, 2, 0, NULL);
        guest_call(mem_input, 2, 0, NULL);
        guest_call(peer, 5, 0, NULL);
        return E_OUTOFMEMORY;
    }
    MEM32(hle_va(properties) + 0) = 4;                       /* cBuffers */
    MEM32(hle_va(properties) + 4) = r->sample_size;          /* cbBuffer */
    MEM32(hle_va(properties) + 8) = 1;                       /* cbAlign */
    MEM32(hle_va(properties) + 12) = 0;                      /* cbPrefix */
    uint32_t set_args[2] = { hle_va(properties), hle_va(properties) + 16 };
    hr = guest_call(allocator, 3, 2, set_args);              /* SetProperties */
    uint32_t actual_buffers = MEM32(hle_va(properties) + 16);
    uint32_t actual_size = MEM32(hle_va(properties) + 20);
    hle_free(properties);
    if ((hr & 0x80000000u) || actual_buffers < 1 || actual_size < r->sample_size) {
        guest_call(allocator, 2, 0, NULL);
        guest_call(mem_input, 2, 0, NULL);
        guest_call(peer, 5, 0, NULL);
        return (hr & 0x80000000u) ? hr : VFW_E_TYPE_NOT_ACCEPTED;
    }

    uint32_t notify_args[2] = { allocator, 0 };
    hr = guest_call(mem_input, 4, 2, notify_args);            /* NotifyAllocator */
    if (hr & 0x80000000u) {
        guest_call(allocator, 2, 0, NULL);
        guest_call(mem_input, 2, 0, NULL);
        guest_call(peer, 5, 0, NULL);
        return hr;
    }

    guest_call(peer, 1, 0, NULL);
    r->peer_pin = peer;
    r->peer_mem_input = mem_input;
    r->allocator = allocator;
    r->state = FILTER_STATE_PAUSED;
    fprintf(stderr, "[dshow-reader] connected RGB24 %dx%d sample=%u allocator=%u x %u\n",
            r->width, r->height, r->sample_size, actual_buffers, actual_size);
    return S_OK;
}

static void reader_notify_complete(reader_t *r) {
    if (!r->graph_va) return;
    uint32_t args[3] = { EC_COMPLETE, 0, 0 };
    (void)guest_call(r->graph_va, 3, 3, args);              /* IFilterGraph slot aliases Notify */
}

static int reader_deliver_frame(reader_t *r, const uint8_t *frame, uint32_t size,
                                int64_t start, int64_t end) {
    if (!r->peer_mem_input || !r->allocator) return 0;
    uint32_t sample_out_va = guest_alloc_locked(4);
    uint32_t time_va = guest_alloc_locked(16);
    if (!sample_out_va || !time_va) {
        guest_free_locked(sample_out_va);
        guest_free_locked(time_va);
        return 0;
    }
    guest_write32_locked(sample_out_va, 0);
    uint32_t get_args[4] = { sample_out_va, 0, 0, AM_GBF_NOWAIT };
    uint32_t hr = guest_call(r->allocator, 7, 4, get_args);
    uint32_t sample = guest_read32_locked(sample_out_va);
    if ((hr & 0x80000000u) || !sample) {
        guest_free_locked(time_va);
        guest_free_locked(sample_out_va);
        return 1;                                      /* dropped frame: renderer busy */
    }

    uint32_t pointer_out_va = guest_alloc_locked(4);
    if (!pointer_out_va) {
        guest_call(sample, 2, 0, NULL);
        guest_free_locked(time_va);
        guest_free_locked(sample_out_va);
        return 0;
    }
    guest_write32_locked(pointer_out_va, 0);
    uint32_t pointer_args[1] = { pointer_out_va };
    hr = guest_call(sample, 3, 1, pointer_args);        /* IMediaSample::GetPointer */
    uint32_t buffer_va = guest_read32_locked(pointer_out_va);
    uint32_t max_size = guest_call(sample, 4, 0, NULL); /* IMediaSample::GetSize */
    if (!(hr & 0x80000000u) && buffer_va && max_size >= size) {
        guest_copy_locked(buffer_va, frame, size);
        uint64_t times[2] = { (uint64_t)start, (uint64_t)end };
        guest_copy_locked(time_va, times, sizeof(times));
        uint32_t set_time_args[2] = { time_va, time_va + 8 };
        (void)guest_call(sample, 6, 2, set_time_args);  /* SetTime */
        uint32_t one[1] = { 1 }, zero[1] = { 0 }, actual[1] = { size };
        (void)guest_call(sample, 8, 1, one);            /* SetSyncPoint */
        (void)guest_call(sample, 10, 1, zero);          /* SetPreroll */
        (void)guest_call(sample, 12, 1, actual);        /* SetActualDataLength */
        (void)guest_call(sample, 16, 1, zero);          /* SetDiscontinuity */
        uint32_t receive_args[1] = { sample };
        hr = guest_call(r->peer_mem_input, 6, 1, receive_args); /* IMemInputPin::Receive (slot 6) */
    } else {
        hr = E_FAIL;
    }

    guest_call(sample, 2, 0, NULL);
    guest_free_locked(pointer_out_va);
    guest_free_locked(time_va);
    guest_free_locked(sample_out_va);
    return !(hr & 0x80000000u);
}

static uint64_t frame_pts_100ns(reader_t *r, const tm_asf_frame_info *frame,
                                int64_t first_pts, uint64_t frame_number) {
    if (frame->pts != INT64_MIN && frame->time_base_den > 0) {
        long double ticks = (long double)(frame->pts - first_pts) *
            (long double)frame->time_base_num * (long double)REFERENCE_TIME_PER_SEC /
            (long double)frame->time_base_den;
        if (ticks >= 0.0L && ticks < 9.0e18L) return (uint64_t)ticks;
    }
    return (uint64_t)((long double)frame_number * (long double)r->avg_frame_seconds *
                      (long double)REFERENCE_TIME_PER_SEC);
}

static int reader_wait_unpaused(reader_t *r, uint64_t *clock_shift) {
    while (atomic_load_explicit(&r->paused, memory_order_acquire) &&
           !atomic_load_explicit(&r->stop_requested, memory_order_acquire)) {
        uint64_t paused_at = hle_mono_ns() / 100u;
        while (atomic_load_explicit(&r->paused, memory_order_acquire) &&
               !atomic_load_explicit(&r->stop_requested, memory_order_acquire))
            usleep(5000);
        uint64_t resumed_at = hle_mono_ns() / 100u;
        if (resumed_at > paused_at) *clock_shift += resumed_at - paused_at;
    }
    return !atomic_load_explicit(&r->stop_requested, memory_order_acquire);
}

static int reader_wait_until(reader_t *r, uint64_t due_100ns, uint64_t *clock_shift) {
    for (;;) {
        if (atomic_load_explicit(&r->stop_requested, memory_order_acquire)) return 0;
        if (atomic_load_explicit(&r->paused, memory_order_acquire)) {
            uint64_t paused_at = hle_mono_ns() / 100u;
            while (atomic_load_explicit(&r->paused, memory_order_acquire) &&
                   !atomic_load_explicit(&r->stop_requested, memory_order_acquire))
                usleep(5000);
            uint64_t resumed_at = hle_mono_ns() / 100u;
            if (resumed_at > paused_at) *clock_shift += resumed_at - paused_at;
            continue;
        }
        uint64_t now = hle_mono_ns() / 100u;
        uint64_t target = due_100ns + *clock_shift;
        if (now >= target) return 1;
        uint64_t wait_100ns = target - now;
        useconds_t sleep_us = (useconds_t)(wait_100ns / 10u);
        if (sleep_us > 5000) sleep_us = 5000;
        if (sleep_us < 250) sleep_us = 250;
        usleep(sleep_us);
    }
}

static void *reader_worker(void *opaque) {
    reader_t *r = (reader_t *)opaque;
    hle_thread_id_adopt(r->worker_tid);

    uint8_t *frame = (uint8_t *)malloc(r->sample_size);
    if (!frame) {
        fprintf(stderr, "[dshow-reader] frame buffer allocation failed\n");
        reader_notify_complete(r);
        atomic_store_explicit(&r->running, 0, memory_order_release);
        atomic_store_explicit(&r->worker_done, 1, memory_order_release);
        reader_release_host(r);                     /* drop worker-owned COM reference */
        return NULL;
    }

    uint64_t run_start = r->run_start_100ns ? r->run_start_100ns : hle_mono_ns() / 100u;
    int64_t first_pts = INT64_MIN;
    uint64_t frame_number = 0;
    uint64_t clock_shift = 0;
    char error[256];

    while (!atomic_load_explicit(&r->stop_requested, memory_order_acquire)) {
        if (!reader_wait_unpaused(r, &clock_shift)) break;
        tm_asf_frame_info frame_info;
        int result = tm_asf_decoder_read_rgb24(r->decoder, frame, r->sample_size,
                                               r->stride, &frame_info, error, sizeof error);
        if (result == TM_ASF_READ_EOF) {
            if (r->looping && !atomic_load_explicit(&r->stop_requested, memory_order_acquire)) {
                tm_asf_decoder_close(r->decoder);
                r->decoder = NULL;
                tm_asf_video_info info;
                tm_asf_decoder *next = NULL;
                int rc = tm_asf_decoder_open(r->host_path, &next, &info, error, sizeof error);
                if (rc == 0 && next) {
                    r->decoder = next;
                    first_pts = INT64_MIN;
                    frame_number = 0;
                    run_start = hle_mono_ns() / 100u + 1000u;
                    continue;
                }
                fprintf(stderr, "[dshow-reader] loop reopen failed: %s (%d)\n", error, rc);
            }
            break;
        }
        if (result == TM_ASF_READ_ERROR) {
            fprintf(stderr, "[dshow-reader] decode failed: %s\n", error);
            break;
        }
        if (first_pts == INT64_MIN && frame_info.pts != INT64_MIN)
            first_pts = frame_info.pts;
        uint64_t pts = frame_pts_100ns(r, &frame_info, first_pts, frame_number);
        if (!reader_wait_until(r, run_start + pts, &clock_shift)) break;
        uint64_t duration = frame_info.duration > 0 && frame_info.time_base_den > 0
            ? (uint64_t)((long double)frame_info.duration * frame_info.time_base_num *
                         REFERENCE_TIME_PER_SEC / frame_info.time_base_den)
            : (uint64_t)(r->avg_frame_seconds * (double)REFERENCE_TIME_PER_SEC);
        if (!duration) duration = 1;
        if (!reader_deliver_frame(r, frame, r->sample_size, (int64_t)pts,
                                  (int64_t)(pts + duration))) {
            if (!atomic_load_explicit(&r->stop_requested, memory_order_acquire))
                fprintf(stderr, "[dshow-reader] downstream rejected frame %llu\n",
                        (unsigned long long)frame_number);
            break;
        }
        frame_number++;
    }

    free(frame);
    atomic_store_explicit(&r->running, 0, memory_order_release);
    atomic_store_explicit(&r->worker_done, 1, memory_order_release);
    if (!atomic_load_explicit(&r->stop_requested, memory_order_acquire) && !r->looping) {
        if (r->peer_pin) guest_call(r->peer_pin, 14, 0, NULL);  /* IPin::EndOfStream */
        reader_notify_complete(r);
    }
    reader_release_host(r);                         /* worker reference: no reader access after this */
    return NULL;
}

static uint32_t reader_join_worker(reader_t *r, int stopping) {
    if (!r->worker_started) return S_OK;
    if (stopping) atomic_store_explicit(&r->stop_requested, 1, memory_order_release);
    if (pthread_equal(pthread_self(), r->worker)) return E_UNEXPECTED;
    if (stopping && !atomic_load_explicit(&r->stop_requested, memory_order_acquire))
        atomic_store_explicit(&r->stop_requested, 1, memory_order_release);
    hle_block_begin();
    int rc = pthread_join(r->worker, NULL);
    hle_block_end();
    r->worker_started = 0;
    return rc == 0 ? S_OK : E_FAIL;
}

static void reader_stop_worker(reader_t *r) {
    if (!r->worker_started) return;
    if (pthread_equal(pthread_self(), r->worker)) {
        /* A downstream Receive/EC_COMPLETE callback can re-enter the graph
         * and remove this filter on the decoder thread itself. The worker
         * owns a COM reference until its final instruction, so defer the
         * final Release until after this call unwinds. */
        atomic_store_explicit(&r->stop_requested, 1, memory_order_release);
        atomic_store_explicit(&r->running, 0, memory_order_release);
        r->worker_started = 0;
        r->state = FILTER_STATE_STOPPED;
        return;
    }
    (void)reader_join_worker(r, 1);
    atomic_store_explicit(&r->running, 0, memory_order_release);
    r->state = FILTER_STATE_STOPPED;
}

static uint32_t reader_start_worker(reader_t *r, uint64_t start_time) {
    if (!r->decoder || !r->peer_mem_input || !r->allocator)
        return VFW_E_NOT_CONNECTED;
    if (r->worker_started) {
        if (!atomic_load_explicit(&r->worker_done, memory_order_acquire)) {
            atomic_store_explicit(&r->paused, 0, memory_order_release);
            atomic_store_explicit(&r->running, 1, memory_order_release);
            r->state = FILTER_STATE_RUNNING;
            return S_OK;
        }
        if (reader_join_worker(r, 0) != S_OK) return E_FAIL;
        if (r->decoder) tm_asf_decoder_close(r->decoder);
        r->decoder = NULL;
        tm_asf_video_info info;
        char error[256];
        int rc = tm_asf_decoder_open(r->host_path, &r->decoder, &info, error, sizeof error);
        if (rc < 0 || !r->decoder) {
            fprintf(stderr, "[dshow-reader] restart failed: %s (%d)\n", error, rc);
            return VFW_E_CANNOT_RENDER;
        }
    }
    reader_addref(r);                               /* worker owns the filter through callbacks */
    atomic_store_explicit(&r->stop_requested, 0, memory_order_release);
    atomic_store_explicit(&r->paused, 0, memory_order_release);
    atomic_store_explicit(&r->running, 1, memory_order_release);
    atomic_store_explicit(&r->worker_done, 0, memory_order_release);
    r->run_start_100ns = start_time;
    r->worker_tid = hle_thread_id_new();
    r->state = FILTER_STATE_RUNNING;
    int rc = pthread_create(&r->worker, NULL, reader_worker, r);
    if (rc != 0) {
        atomic_store_explicit(&r->running, 0, memory_order_release);
        r->state = FILTER_STATE_STOPPED;
        reader_release_host(r);                     /* pthread_create failed */
        return E_FAIL;
    }
    r->worker_started = 1;
    return S_OK;
}

/* ---- IBaseFilter / IMediaFilter ---- */
static void filter_QueryInterface(void) {
    reader_t *r = reader_from_this("IBaseFilter::QueryInterface");
    uint32_t iid = A32(1), out = A32(2);
    if (!out) RET(E_POINTER, 3);
    MEM32(out) = 0;
    if (!iid) RET(E_POINTER, 3);
    const uint8_t *wanted = (const uint8_t *)hle_ptr(iid);
    if (!memcmp(wanted, IID_IUnknown, 16) || !memcmp(wanted, IID_IPersist, 16) ||
        !memcmp(wanted, IID_IMediaFilter, 16) || !memcmp(wanted, IID_IBaseFilter, 16)) {
        reader_addref(r);
        MEM32(out) = r->filter_va;
        RET(S_OK, 3);
    }
    if (!memcmp(wanted, IID_IFileSourceFilter, 16)) {
        reader_addref(r);
        MEM32(out) = r->file_va;
        RET(S_OK, 3);
    }
    RET(E_NOINTERFACE, 3);
}
static void filter_AddRef(void) {
    reader_t *r = reader_from_this("IBaseFilter::AddRef");
    RET((uint32_t)__atomic_add_fetch(&r->refs, 1, __ATOMIC_RELAXED), 1);
}
static void filter_Release(void) {
    reader_t *r = reader_from_this("IBaseFilter::Release");
    RET(reader_release_host(r), 1);
}
static void filter_GetClassID(void) {
    reader_from_this("IPersist::GetClassID");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    memcpy(hle_ptr(out), CLSID_WMAsfReader, 16);
    RET(S_OK, 2);
}
static void filter_Stop(void) {
    reader_t *r = reader_from_this("IMediaFilter::Stop");
    reader_stop_worker(r);
    if (r->allocator && r->allocator_committed) {
        (void)guest_call(r->allocator, 6, 0, NULL);      /* IMemAllocator::Decommit */
        r->allocator_committed = 0;
    }
    RET(S_OK, 1);
}
static void filter_Pause(void) {
    reader_t *r = reader_from_this("IMediaFilter::Pause");
    if (!r->decoder) RET(VFW_E_CANNOT_RENDER, 1);
    atomic_store_explicit(&r->paused, 1, memory_order_release);
    atomic_store_explicit(&r->running, 0, memory_order_release);
    r->state = FILTER_STATE_PAUSED;
    RET(S_OK, 1);
}
static void filter_Run(void) {
    reader_t *r = reader_from_this("IMediaFilter::Run");
    uint64_t start = (uint64_t)A32(1) | ((uint64_t)A32(2) << 32);
    if (r->allocator && !r->allocator_committed) {
        uint32_t commit_hr = guest_call(r->allocator, 5, 0, NULL); /* IMemAllocator::Commit */
        if (commit_hr & 0x80000000u) RET(commit_hr, 3);
        r->allocator_committed = 1;
    }
    RET(reader_start_worker(r, start), 3);
}
static void filter_GetState(void) {
    reader_t *r = reader_from_this("IMediaFilter::GetState");
    uint32_t out = A32(2);
    if (!out) RET(E_POINTER, 3);
    MEM32(out) = r->state;
    RET(S_OK, 3);
}
static void filter_SetSyncSource(void) {
    reader_t *r = reader_from_this("IMediaFilter::SetSyncSource");
    uint32_t clock = A32(1);
    if (clock) guest_call(clock, 1, 0, NULL);
    if (r->clock_va) guest_call(r->clock_va, 2, 0, NULL);
    r->clock_va = clock;
    RET(S_OK, 2);
}
static void filter_GetSyncSource(void) {
    reader_t *r = reader_from_this("IMediaFilter::GetSyncSource");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    MEM32(out) = r->clock_va;
    if (r->clock_va) guest_call(r->clock_va, 1, 0, NULL);
    RET(S_OK, 2);
}
static void filter_EnumPins(void) {
    reader_t *r = reader_from_this("IBaseFilter::EnumPins");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    MEM32(out) = 0;
    pin_enum_t *e = NULL;
    for (int i = 0; i < MAX_PIN_ENUMS; i++)
        if (!g_pin_enums[i].va) { e = &g_pin_enums[i]; break; }
    if (!e) RET(E_OUTOFMEMORY, 2);
    uint8_t *memory = hle_alloc(4);
    if (!memory) RET(E_OUTOFMEMORY, 2);
    memset(e, 0, sizeof(*e));
    e->va = hle_va(memory);
    e->refs = 1;
    e->reader = r;
    MEM32(e->va) = g_pin_enum_vt;
    reader_addref(r);
    MEM32(out) = e->va;
    RET(S_OK, 2);
}
static void filter_FindPin(void) {
    reader_t *r = reader_from_this("IBaseFilter::FindPin");
    uint32_t id = A32(1), out = A32(2);
    if (!out) RET(E_POINTER, 3);
    MEM32(out) = 0;
    if (!id) RET(E_POINTER, 3);
    if (!id) RET(E_POINTER, 3);
    const uint16_t *s = (const uint16_t *)hle_ptr(id);
    if ((s[0] == 'V' || s[0] == 'v') && (s[1] == 'i' || s[1] == 'I') &&
        (s[2] == 'd' || s[2] == 'D') && (s[3] == 'e' || s[3] == 'E') &&
        (s[4] == 'o' || s[4] == 'O') && !s[5]) {
        reader_addref(r);
        MEM32(out) = r->pin_va;
        RET(S_OK, 3);
    }
    RET(VFW_E_CANNOT_RENDER, 3);
}
static void filter_QueryFilterInfo(void) {
    reader_t *r = reader_from_this("IBaseFilter::QueryFilterInfo");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    memset(hle_ptr(out), 0, 260);
    memcpy(hle_ptr(out), r->filter_name, sizeof(r->filter_name));
    MEM32(out + 256) = r->graph_va;
    if (r->graph_va) guest_call(r->graph_va, 1, 0, NULL);
    RET(S_OK, 2);
}
static void filter_JoinFilterGraph(void) {
    reader_t *r = reader_from_this("IBaseFilter::JoinFilterGraph");
    uint32_t graph = A32(1), name = A32(2);
    if (!graph) {
        reader_stop_worker(r);
        r->graph_va = 0;
        r->filter_name[0] = 0;
    } else {
        r->graph_va = graph;
        if (name) {
            const uint16_t *s = (const uint16_t *)hle_ptr(name);
            size_t n = 0;
            while (s[n] && n + 1 < 128) n++;
            memcpy(r->filter_name, s, (n + 1) * sizeof(uint16_t));
        } else {
            static const uint16_t fallback[] = {'W','M',' ','A','S','F',' ','R','e','a','d','e','r',0};
            memcpy(r->filter_name, fallback, sizeof(fallback));
        }
    }
    RET(S_OK, 3);
}
static void filter_QueryVendorInfo(void) {
    reader_from_this("IBaseFilter::QueryVendorInfo");
    if (A32(1)) MEM32(A32(1)) = 0;
    RET(E_NOTIMPL, 2);
}

/* ---- IFileSourceFilter ---- */
static void file_QueryInterface(void) {
    reader_t *r = reader_from_this("IFileSourceFilter::QueryInterface");
    uint32_t iid = A32(1), out = A32(2);
    if (!out) RET(E_POINTER, 3);
    MEM32(out) = 0;
    if (!iid) RET(E_POINTER, 3);
    const uint8_t *wanted = (const uint8_t *)hle_ptr(iid);
    if (!memcmp(wanted, IID_IUnknown, 16) || !memcmp(wanted, IID_IPersist, 16) ||
        !memcmp(wanted, IID_IMediaFilter, 16) || !memcmp(wanted, IID_IBaseFilter, 16)) {
        reader_addref(r);
        MEM32(out) = r->filter_va;
        RET(S_OK, 3);
    }
    if (!memcmp(wanted, IID_IFileSourceFilter, 16)) {
        reader_addref(r);
        MEM32(out) = r->file_va;
        RET(S_OK, 3);
    }
    RET(E_NOINTERFACE, 3);
}
static void file_AddRef(void) {
    reader_t *r = reader_from_this("IFileSourceFilter::AddRef");
    RET((uint32_t)__atomic_add_fetch(&r->refs, 1, __ATOMIC_RELAXED), 1);
}
static void file_Release(void) {
    reader_t *r = reader_from_this("IFileSourceFilter::Release");
    RET(reader_release_host(r), 1);
}
static void file_GetClassID(void) {
    reader_from_this("IFileSourceFilter::GetClassID");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    memcpy(hle_ptr(out), CLSID_WMAsfReader, 16);
    RET(S_OK, 2);
}
static void file_Load(void) {
    reader_t *r = reader_from_this("IFileSourceFilter::Load");
    uint32_t name = A32(1);
    if (!name) RET(E_POINTER, 3);
    reader_disconnect(r);
    RET(reader_load_path(r, (const uint16_t *)hle_ptr(name)), 3);
}
static void file_GetCurFile(void) {
    reader_t *r = reader_from_this("IFileSourceFilter::GetCurFile");
    uint32_t name_out = A32(1), mt_out = A32(2);
    if (!name_out) RET(E_POINTER, 3);
    MEM32(name_out) = 0;
    if (!r->current_path[0]) RET(VFW_E_CANNOT_RENDER, 3);
    size_t n = 0;
    while (r->current_path[n] && n + 1 < PATH_MAX) n++;
    uint16_t *copy = (uint16_t *)hle_alloc((uint32_t)((n + 1) * sizeof(uint16_t)));
    if (!copy) RET(E_OUTOFMEMORY, 3);
    memcpy(copy, r->current_path, (n + 1) * sizeof(uint16_t));
    MEM32(name_out) = hle_va(copy);
    if (mt_out) {
        /* pmt is an AM_MEDIA_TYPE structure, not AM_MEDIA_TYPE**. Copy the
         * structure and transfer ownership of its format block to caller. */
        memset(hle_ptr(mt_out), 0, 72);
        uint32_t mt = 0;
        uint32_t hr = media_type_clone(r, &mt);
        if (hr != S_OK) {
            hle_free(copy);
            MEM32(name_out) = 0;
            RET(hr, 3);
        }
        memcpy(hle_ptr(mt_out), hle_ptr(mt), 72);
        hle_free(hle_ptr(mt));                    /* format block now belongs to caller */
    }
    RET(S_OK, 3);
}

/* ---- IPin ---- */
static void pin_QueryInterface(void) {
    reader_t *r = reader_from_this("IPin::QueryInterface");
    uint32_t iid = A32(1), out = A32(2);
    if (!out) RET(E_POINTER, 3);
    MEM32(out) = 0;
    if (!iid) RET(E_POINTER, 3);
    const uint8_t *wanted = (const uint8_t *)hle_ptr(iid);
    if (!memcmp(wanted, IID_IUnknown, 16) || !memcmp(wanted, IID_IPin, 16)) {
        reader_addref(r);
        MEM32(out) = r->pin_va;
        RET(S_OK, 3);
    }
    RET(E_NOINTERFACE, 3);
}
static void pin_AddRef(void) {
    reader_t *r = reader_from_this("IPin::AddRef");
    RET((uint32_t)__atomic_add_fetch(&r->refs, 1, __ATOMIC_RELAXED), 1);
}
static void pin_Release(void) {
    reader_t *r = reader_from_this("IPin::Release");
    RET(reader_release_host(r), 1);
}
static void pin_Connect(void) {
    reader_t *r = reader_from_this("IPin::Connect");
    RET(reader_build_output_pin_connection(r, A32(1), A32(2)), 3);
}
static void pin_ReceiveConnection(void) {
    reader_from_this("IPin::ReceiveConnection");
    RET(VFW_E_INVALID_DIRECTION, 3);
}
static void pin_Disconnect(void) {
    reader_t *r = reader_from_this("IPin::Disconnect");
    if (!r->peer_pin) RET(S_FALSE, 1);
    reader_disconnect(r);
    RET(S_OK, 1);
}
static void pin_ConnectedTo(void) {
    reader_t *r = reader_from_this("IPin::ConnectedTo");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    MEM32(out) = r->peer_pin;
    if (!r->peer_pin) RET(VFW_E_NOT_CONNECTED, 2);
    guest_call(r->peer_pin, 1, 0, NULL);
    RET(S_OK, 2);
}
static void pin_ConnectionMediaType(void) {
    reader_t *r = reader_from_this("IPin::ConnectionMediaType");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    if (!r->peer_pin) RET(VFW_E_NOT_CONNECTED, 2);
    uint32_t mt = 0;
    uint32_t hr = media_type_clone(r, &mt);
    if (hr != S_OK) RET(hr, 2);
    memcpy(hle_ptr(out), hle_ptr(mt), 72);
    hle_free(hle_ptr(mt));                       /* preserve cloned format: transferred to caller */
    RET(S_OK, 2);
}
static void pin_QueryPinInfo(void) {
    reader_t *r = reader_from_this("IPin::QueryPinInfo");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    memset(hle_ptr(out), 0, 264);
    MEM32(out) = r->filter_va;
    MEM32(out + 4) = PINDIR_OUTPUT;
    static const uint16_t video_name[] = {'V','i','d','e','o',0};
    memcpy(hle_ptr(out + 8), video_name, sizeof(video_name));
    reader_addref(r);
    RET(S_OK, 2);
}
static void pin_QueryDirection(void) {
    reader_from_this("IPin::QueryDirection");
    if (!A32(1)) RET(E_POINTER, 2);
    MEM32(A32(1)) = PINDIR_OUTPUT;
    RET(S_OK, 2);
}
static void pin_QueryId(void) {
    reader_from_this("IPin::QueryId");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    static const uint16_t video_name[] = {'V','i','d','e','o',0};
    uint16_t *copy = (uint16_t *)hle_alloc(sizeof(video_name));
    if (!copy) RET(E_OUTOFMEMORY, 2);
    memcpy(copy, video_name, sizeof(video_name));
    MEM32(out) = hle_va(copy);
    RET(S_OK, 2);
}
static void pin_QueryAccept(void) {
    reader_from_this("IPin::QueryAccept");
    uint32_t mt = A32(1);
    if (!mt) RET(E_POINTER, 2);
    const uint8_t *p = (const uint8_t *)hle_ptr(mt);
    RET(!memcmp(p, MEDIATYPE_Video, 16) && !memcmp(p + 16, MEDIASUBTYPE_RGB24, 16)
        ? S_OK : S_FALSE, 2);
}
static void pin_EnumMediaTypes(void) {
    reader_t *r = reader_from_this("IPin::EnumMediaTypes");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    MEM32(out) = 0;
    type_enum_t *e = NULL;
    for (int i = 0; i < MAX_TYPE_ENUMS; i++)
        if (!g_type_enums[i].va) { e = &g_type_enums[i]; break; }
    if (!e) RET(E_OUTOFMEMORY, 2);
    uint8_t *memory = hle_alloc(4);
    if (!memory) RET(E_OUTOFMEMORY, 2);
    memset(e, 0, sizeof(*e));
    e->va = hle_va(memory);
    e->refs = 1;
    e->reader = r;
    MEM32(e->va) = g_type_enum_vt;
    reader_addref(r);
    MEM32(out) = e->va;
    RET(S_OK, 2);
}
static void pin_QueryInternalConnections(void) {
    reader_from_this("IPin::QueryInternalConnections");
    RET(E_NOTIMPL, 3);
}
static void pin_EndOfStream(void) {
    reader_t *r = reader_from_this("IPin::EndOfStream");
    if (r->peer_pin) guest_call(r->peer_pin, 14, 0, NULL);
    RET(S_OK, 1);
}
static void pin_BeginFlush(void) {
    reader_t *r = reader_from_this("IPin::BeginFlush");
    r->pin_flushing = 1;
    if (r->peer_pin) guest_call(r->peer_pin, 15, 0, NULL);
    RET(S_OK, 1);
}
static void pin_EndFlush(void) {
    reader_t *r = reader_from_this("IPin::EndFlush");
    r->pin_flushing = 0;
    if (r->peer_pin) guest_call(r->peer_pin, 16, 0, NULL);
    RET(S_OK, 1);
}
static void pin_NewSegment(void) {
    reader_t *r = reader_from_this("IPin::NewSegment");
    if (r->peer_pin) {
        uint32_t args[3] = { A32(1), A32(2), A32(3) };
        guest_call(r->peer_pin, 17, 3, args);
    }
    RET(S_OK, 4);
}

/* ---- IEnumPins ---- */
static pin_enum_t *pin_enum_this(void) {
    uint32_t va = A32(0);
    for (int i = 0; i < MAX_PIN_ENUMS; i++)
        if (g_pin_enums[i].va == va && va) return &g_pin_enums[i];
    hle_fatal("[dshow-reader] IEnumPins on non-live object 0x%08X", va);
    return NULL;
}
static void pin_enum_QueryInterface(void) {
    pin_enum_t *e = pin_enum_this();
    uint32_t iid = A32(1), out = A32(2);
    if (!out) RET(E_POINTER, 3);
    MEM32(out) = 0;
    if (!iid) RET(E_POINTER, 3);
    if (memcmp(hle_ptr(iid), IID_IUnknown, 16) &&
        memcmp(hle_ptr(iid), IID_IEnumPins, 16)) RET(E_NOINTERFACE, 3);
    e->refs++;
    reader_addref(e->reader);
    MEM32(out) = e->va;
    RET(S_OK, 3);
}
static void pin_enum_AddRef(void) {
    pin_enum_t *e = pin_enum_this();
    reader_addref(e->reader);
    RET((uint32_t)++e->refs, 1);
}
static void pin_enum_Release(void) {
    pin_enum_t *e = pin_enum_this();
    reader_t *r = e->reader;
    int refs = --e->refs;
    if (!refs) {
        uint32_t va = e->va;
        memset(e, 0, sizeof(*e));
        hle_free(hle_ptr(va));
    }
    RET(reader_release_host(r), 1);
}
static void pin_enum_Next(void) {
    pin_enum_t *e = pin_enum_this();
    uint32_t count = A32(1), out = A32(2), fetched_out = A32(3);
    if (count && !out) RET(E_POINTER, 4);
    if (count != 1 && !fetched_out) RET(E_POINTER, 4);
    if (fetched_out) MEM32(fetched_out) = 0;
    uint32_t got = 0;
    if (e->cursor == 0 && count) {
        reader_addref(e->reader);
        MEM32(out) = e->reader->pin_va;
        e->cursor = 1;
        got = 1;
    }
    if (fetched_out) MEM32(fetched_out) = got;
    RET(got == count ? S_OK : S_FALSE, 4);
}
static void pin_enum_Skip(void) {
    pin_enum_t *e = pin_enum_this();
    uint32_t count = A32(1);
    if (!count) RET(S_OK, 2);
    if (e->cursor) RET(S_FALSE, 2);
    e->cursor = 1;
    RET(count == 1 ? S_OK : S_FALSE, 2);
}
static void pin_enum_Reset(void) {
    pin_enum_t *e = pin_enum_this();
    e->cursor = 0;
    RET(S_OK, 1);
}
static void pin_enum_Clone(void) {
    pin_enum_t *e = pin_enum_this();
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    MEM32(out) = 0;
    pin_enum_t *clone = NULL;
    for (int i = 0; i < MAX_PIN_ENUMS; i++)
        if (!g_pin_enums[i].va) { clone = &g_pin_enums[i]; break; }
    if (!clone) RET(E_OUTOFMEMORY, 2);
    uint8_t *memory = hle_alloc(4);
    if (!memory) RET(E_OUTOFMEMORY, 2);
    *clone = *e;
    clone->va = hle_va(memory);
    clone->refs = 1;
    MEM32(clone->va) = g_pin_enum_vt;
    reader_addref(clone->reader);
    MEM32(out) = clone->va;
    RET(S_OK, 2);
}

/* ---- IEnumMediaTypes ---- */
static type_enum_t *type_enum_this(void) {
    uint32_t va = A32(0);
    for (int i = 0; i < MAX_TYPE_ENUMS; i++)
        if (g_type_enums[i].va == va && va) return &g_type_enums[i];
    hle_fatal("[dshow-reader] IEnumMediaTypes on non-live object 0x%08X", va);
    return NULL;
}
static void type_enum_QueryInterface(void) {
    type_enum_t *e = type_enum_this();
    uint32_t iid = A32(1), out = A32(2);
    if (!out) RET(E_POINTER, 3);
    MEM32(out) = 0;
    if (!iid) RET(E_POINTER, 3);
    if (memcmp(hle_ptr(iid), IID_IUnknown, 16) &&
        memcmp(hle_ptr(iid), IID_IEnumMediaTypes, 16)) RET(E_NOINTERFACE, 3);
    e->refs++;
    reader_addref(e->reader);
    MEM32(out) = e->va;
    RET(S_OK, 3);
}
static void type_enum_AddRef(void) {
    type_enum_t *e = type_enum_this();
    reader_addref(e->reader);
    RET((uint32_t)++e->refs, 1);
}
static void type_enum_Release(void) {
    type_enum_t *e = type_enum_this();
    reader_t *r = e->reader;
    int refs = --e->refs;
    if (!refs) {
        uint32_t va = e->va;
        memset(e, 0, sizeof(*e));
        hle_free(hle_ptr(va));
    }
    RET(reader_release_host(r), 1);
}
static void type_enum_Next(void) {
    type_enum_t *e = type_enum_this();
    uint32_t count = A32(1), out = A32(2), fetched_out = A32(3);
    if (count && !out) RET(E_POINTER, 4);
    if (count != 1 && !fetched_out) RET(E_POINTER, 4);
    if (fetched_out) MEM32(fetched_out) = 0;
    uint32_t got = 0;
    if (!e->cursor && count) {
        uint32_t mt = 0;
        uint32_t hr = media_type_clone(e->reader, &mt);
        if (hr != S_OK) RET(hr, 4);
        MEM32(out) = mt;
        e->cursor = 1;
        got = 1;
    }
    if (fetched_out) MEM32(fetched_out) = got;
    RET(got == count ? S_OK : S_FALSE, 4);
}
static void type_enum_Skip(void) {
    type_enum_t *e = type_enum_this();
    uint32_t count = A32(1);
    if (!count) RET(S_OK, 2);
    if (e->cursor) RET(S_FALSE, 2);
    e->cursor = 1;
    RET(count == 1 ? S_OK : S_FALSE, 2);
}
static void type_enum_Reset(void) {
    type_enum_t *e = type_enum_this();
    e->cursor = 0;
    RET(S_OK, 1);
}
static void type_enum_Clone(void) {
    type_enum_t *e = type_enum_this();
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    MEM32(out) = 0;
    type_enum_t *clone = NULL;
    for (int i = 0; i < MAX_TYPE_ENUMS; i++)
        if (!g_type_enums[i].va) { clone = &g_type_enums[i]; break; }
    if (!clone) RET(E_OUTOFMEMORY, 2);
    uint8_t *memory = hle_alloc(4);
    if (!memory) RET(E_OUTOFMEMORY, 2);
    *clone = *e;
    clone->va = hle_va(memory);
    clone->refs = 1;
    MEM32(clone->va) = g_type_enum_vt;
    reader_addref(clone->reader);
    MEM32(out) = clone->va;
    RET(S_OK, 2);
}

/* ---- COM factories / vtables ---- */
static const win32hle_shim filter_methods[] = {
    { "WMAsfReaderFilter::QueryInterface", filter_QueryInterface },
    { "WMAsfReaderFilter::AddRef", filter_AddRef },
    { "WMAsfReaderFilter::Release", filter_Release },
    { "WMAsfReaderFilter::GetClassID", filter_GetClassID },
    { "WMAsfReaderFilter::Stop", filter_Stop },
    { "WMAsfReaderFilter::Pause", filter_Pause },
    { "WMAsfReaderFilter::Run", filter_Run },
    { "WMAsfReaderFilter::GetState", filter_GetState },
    { "WMAsfReaderFilter::SetSyncSource", filter_SetSyncSource },
    { "WMAsfReaderFilter::GetSyncSource", filter_GetSyncSource },
    { "WMAsfReaderFilter::EnumPins", filter_EnumPins },
    { "WMAsfReaderFilter::FindPin", filter_FindPin },
    { "WMAsfReaderFilter::QueryFilterInfo", filter_QueryFilterInfo },
    { "WMAsfReaderFilter::JoinFilterGraph", filter_JoinFilterGraph },
    { "WMAsfReaderFilter::QueryVendorInfo", filter_QueryVendorInfo },
    { NULL, NULL }
};
/* IFileSourceFilter inherits IPersist: GetClassID is slot 3, Load slot 4,
 * and GetCurFile slot 5. Keep the inherited method in the vtable. */
static const win32hle_shim file_methods[] = {
    { "WMAsfReaderFileSource::QueryInterface", file_QueryInterface },
    { "WMAsfReaderFileSource::AddRef", file_AddRef },
    { "WMAsfReaderFileSource::Release", file_Release },
    { "WMAsfReaderFileSource::GetClassID", file_GetClassID },
    { "WMAsfReaderFileSource::Load", file_Load },
    { "WMAsfReaderFileSource::GetCurFile", file_GetCurFile },
    { NULL, NULL }
};
static const win32hle_shim pin_methods[] = {
    { "WMAsfReaderPin::QueryInterface", pin_QueryInterface },
    { "WMAsfReaderPin::AddRef", pin_AddRef },
    { "WMAsfReaderPin::Release", pin_Release },
    { "WMAsfReaderPin::Connect", pin_Connect },
    { "WMAsfReaderPin::ReceiveConnection", pin_ReceiveConnection },
    { "WMAsfReaderPin::Disconnect", pin_Disconnect },
    { "WMAsfReaderPin::ConnectedTo", pin_ConnectedTo },
    { "WMAsfReaderPin::ConnectionMediaType", pin_ConnectionMediaType },
    { "WMAsfReaderPin::QueryPinInfo", pin_QueryPinInfo },
    { "WMAsfReaderPin::QueryDirection", pin_QueryDirection },
    { "WMAsfReaderPin::QueryId", pin_QueryId },
    { "WMAsfReaderPin::QueryAccept", pin_QueryAccept },
    { "WMAsfReaderPin::EnumMediaTypes", pin_EnumMediaTypes },
    { "WMAsfReaderPin::QueryInternalConnections", pin_QueryInternalConnections },
    { "WMAsfReaderPin::EndOfStream", pin_EndOfStream },
    { "WMAsfReaderPin::BeginFlush", pin_BeginFlush },
    { "WMAsfReaderPin::EndFlush", pin_EndFlush },
    { "WMAsfReaderPin::NewSegment", pin_NewSegment },
    { NULL, NULL }
};
static const win32hle_shim pin_enum_methods[] = {
    { "WMAsfReaderEnumPins::QueryInterface", pin_enum_QueryInterface },
    { "WMAsfReaderEnumPins::AddRef", pin_enum_AddRef },
    { "WMAsfReaderEnumPins::Release", pin_enum_Release },
    { "WMAsfReaderEnumPins::Next", pin_enum_Next },
    { "WMAsfReaderEnumPins::Skip", pin_enum_Skip },
    { "WMAsfReaderEnumPins::Reset", pin_enum_Reset },
    { "WMAsfReaderEnumPins::Clone", pin_enum_Clone },
    { NULL, NULL }
};
static const win32hle_shim type_enum_methods[] = {
    { "WMAsfReaderEnumTypes::QueryInterface", type_enum_QueryInterface },
    { "WMAsfReaderEnumTypes::AddRef", type_enum_AddRef },
    { "WMAsfReaderEnumTypes::Release", type_enum_Release },
    { "WMAsfReaderEnumTypes::Next", type_enum_Next },
    { "WMAsfReaderEnumTypes::Skip", type_enum_Skip },
    { "WMAsfReaderEnumTypes::Reset", type_enum_Reset },
    { "WMAsfReaderEnumTypes::Clone", type_enum_Clone },
    { NULL, NULL }
};

static uint32_t reader_create(const uint8_t *iid, uint32_t *out) {
    if (!iid || !out) return E_POINTER;
    *out = 0;
    if (memcmp(iid, IID_IUnknown, 16) && memcmp(iid, IID_IPersist, 16) &&
        memcmp(iid, IID_IMediaFilter, 16) && memcmp(iid, IID_IBaseFilter, 16) &&
        memcmp(iid, IID_IFileSourceFilter, 16))
        return E_NOINTERFACE;
    reader_t *r = (reader_t *)calloc(1, sizeof(*r));
    if (!r) return E_OUTOFMEMORY;
    int slot = -1;
    for (int i = 0; i < MAX_READER_OBJECTS; i++)
        if (!g_readers[i]) { slot = i; break; }
    if (slot < 0) { free(r); return E_OUTOFMEMORY; }

    uint8_t *filter = hle_alloc(4), *file = hle_alloc(4), *pin = hle_alloc(4);
    if (!filter || !file || !pin) {
        if (filter) hle_free(filter);
        if (file) hle_free(file);
        if (pin) hle_free(pin);
        free(r);
        return E_OUTOFMEMORY;
    }
    r->filter_va = hle_va(filter);
    r->file_va = hle_va(file);
    r->pin_va = hle_va(pin);
    r->refs = 1;
    r->state = FILTER_STATE_STOPPED;
    static const uint16_t default_name[] = {'W','M',' ','A','S','F',' ','R','e','a','d','e','r',0};
    memcpy(r->filter_name, default_name, sizeof(default_name));
    MEM32(r->filter_va) = g_filter_vt;
    MEM32(r->file_va) = g_file_vt;
    MEM32(r->pin_va) = g_pin_vt;
    atomic_init(&r->stop_requested, 0);
    atomic_init(&r->paused, 0);
    atomic_init(&r->running, 0);
    atomic_init(&r->worker_done, 1);
    g_readers[slot] = r;

    *out = !memcmp(iid, IID_IFileSourceFilter, 16) ? r->file_va : r->filter_va;
    fprintf(stderr, "[dshow-reader] CoCreateInstance(WMAsfReader) -> 0x%08X\n", *out);
    return S_OK;
}

uint32_t tm_dshow_reader_add_source_filter(uint32_t graph, uint32_t file_name,
                                           uint32_t filter_name, uint32_t *out_filter) {
    if (!graph || !file_name || !out_filter) return E_POINTER;
    *out_filter = 0;

    uint32_t filter = 0;
    uint32_t hr = reader_create(IID_IBaseFilter, &filter);
    if (hr & 0x80000000u) return hr;

    uint8_t *iid = (uint8_t *)hle_alloc(16);
    uint32_t *file_out = (uint32_t *)hle_alloc(4);
    if (!iid || !file_out) {
        if (iid) hle_free(iid);
        if (file_out) hle_free(file_out);
        guest_call(filter, 2, 0, NULL);
        return E_OUTOFMEMORY;
    }
    memcpy(iid, IID_IFileSourceFilter, 16);
    MEM32(hle_va(file_out)) = 0;
    uint32_t qi_args[2] = { hle_va(iid), hle_va(file_out) };
    hr = guest_call(filter, 0, 2, qi_args);
    uint32_t file_source = MEM32(hle_va(file_out));
    hle_free(file_out);
    hle_free(iid);
    if ((hr & 0x80000000u) || !file_source) {
        guest_call(filter, 2, 0, NULL);
        return (hr & 0x80000000u) ? hr : E_NOINTERFACE;
    }

    uint32_t load_args[2] = { file_name, 0 };
    hr = guest_call(file_source, 4, 2, load_args);       /* IFileSourceFilter::Load (after IPersist::GetClassID) */
    guest_call(file_source, 2, 0, NULL);
    if (hr & 0x80000000u) {
        guest_call(filter, 2, 0, NULL);
        return hr;
    }

    uint32_t add_args[2] = { filter, filter_name };
    hr = guest_call(graph, 3, 2, add_args);              /* IFilterGraph::AddFilter */
    if (hr & 0x80000000u) {
        guest_call(filter, 2, 0, NULL);
        return hr;
    }

    *out_filter = filter;                               /* transfer initial caller reference */
    return hr;
}

int tm_dshow_reader_media_info(uint32_t filter, double *duration_seconds,
                               double *frame_seconds, int *width, int *height) {
    reader_t *r = reader_by_interface(filter);
    if (!r || !r->decoder) return 0;
    if (duration_seconds) *duration_seconds = r->duration_seconds;
    if (frame_seconds) *frame_seconds = r->avg_frame_seconds;
    if (width) *width = r->width;
    if (height) *height = r->height;
    return 1;
}

void tm_dshow_reader_init(void) {
    if (g_filter_vt) return;
    g_filter_vt = hle_com_vtable(filter_methods);
    g_file_vt = hle_com_vtable(file_methods);
    g_pin_vt = hle_com_vtable(pin_methods);
    g_pin_enum_vt = hle_com_vtable(pin_enum_methods);
    g_type_enum_vt = hle_com_vtable(type_enum_methods);
    hle_com_register_class(CLSID_WMAsfReader, reader_create);
}
