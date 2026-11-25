/*
 * tm_vfs.c - the guest's file system view where it is more than the host's
 * directories (docs/IMPORTS.md, "The virtual file system").
 *
 * Two kinds of entry exist without a directory entry on disk:
 *   - a mount: a guest directory served from its own host directory
 *     (C:\WINDOWS from the runtime system directory). It is an entry of its
 *     parent (WINDOWS in C:\), a directory, with the host directory's times.
 *   - a visible host DLL (tm_loader.c): a file entry of its directory
 *     (wmvcore.dll, dinput8.dll in C:\WINDOWS\SYSTEM32), archive attribute,
 *     size 0, the times of that directory. It has no contents: opening it
 *     with CreateFileA fails with ERROR_ACCESS_DENIED, deleting it too, and
 *     CreateDirectoryA of its name fails with ERROR_ALREADY_EXISTS.
 * A real entry of the same name (in any case) is hidden by the virtual one,
 * so a listing never shows a name twice; the loader answers that name with
 * the host DLL too.
 *
 * Overrides ("tm:<name>"): FindFirstFileA, FindNextFileA, FindClose,
 * GetFileAttributesA, GetFullPathNameA, CreateFileA, DeleteFileA,
 * CreateDirectoryA. Paths are made absolute the
 * Windows way (hle_guest_full_path, patches/pcrecomp/0001) before anything
 * is matched, so case, '/' and "..", relative names and C:name all reach
 * the same entry.
 *
 * Listings (FindFirstFileA with * or ?): the names of the host directory
 * that match, and the virtual entries of that guest directory, in NTFS's
 * order (by name without case; "." and ".." first). A drive's root has no
 * "." and "..", as on NTFS. Without a wildcard, the one entry of that name.
 * Masks follow the Win32 rules for FindFirstFile: the mask is rewritten into
 * the DOS wildcard forms (DOS_STAR, DOS_QM, DOS_DOT) and matched as
 * FsRtlIsNameInExpression documents them (tm_vfs_wild).
 *
 * Names longer than MAX_PATH - 1 fail with ERROR_FILENAME_EXCED_RANGE, as the
 * ANSI file functions do (they convert the name into a MAX_PATH buffer).
 *
 * Threads: every function here runs as an import of guest code, under
 * win32hle's machine lock (one thread at a time), or at start-up before any
 * guest thread exists (tm_vfs_mount). The tables need no lock of their own.
 */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "win32hle.h"
#include "tm_loader.h"
#include "tm_vfs.h"

#define ERROR_FILE_NOT_FOUND     2u
#define ERROR_PATH_NOT_FOUND     3u
#define ERROR_ACCESS_DENIED      5u
#define ERROR_INVALID_HANDLE     6u
#define ERROR_NOT_ENOUGH_MEMORY  8u
#define ERROR_TOO_MANY_OPEN_FILES 4u
#define ERROR_NO_MORE_FILES     18u
#define ERROR_INVALID_PARAMETER 87u
#define ERROR_INVALID_NAME     123u
#define ERROR_ALREADY_EXISTS   183u
#define ERROR_FILENAME_EXCED_RANGE 206u
#define TM_MAX_PATH 260
#define FILE_ATTRIBUTE_READONLY  0x01u
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_ARCHIVE   0x20u
#define INVALID_HANDLE 0xFFFFFFFFu
#define TM_H_FIND 24                         /* a handle type of our own (win32hle's stop at 11) */
#define EPOCH_DIFF 11644473600ull

void hle_set_mount(const char *guest_dir, const char *host_dir);          /* patches/pcrecomp/0001 */
int  hle_guest_full_path(const char *in, char *out, size_t n);

#define TM_MAX_MOUNTS 8                      /* = MAX_MOUNTS in patches/pcrecomp/0001 */
/* virtual entries one directory can have: every mount and every host DLL */
#define TM_VFS_MAX_VIRTUAL (TM_MAX_MOUNTS + TM_LOADER_MAX_DLLS)
static struct { char guest[260], parent[260], leaf[64], host[PATH_MAX]; } g_mnt[TM_MAX_MOUNTS];
static int g_nmnt;

/* ---- mounts ---- */
static int prefix_ci(const char *a, const char *b) {     /* a at or under b (guest, '\\') */
    size_t l = strlen(b);
    return !strncasecmp(a, b, l) && (!a[l] || a[l] == '\\' || (l && b[l - 1] == '\\'));
}
static int prefix_host(const char *a, const char *b) {
    size_t l = strlen(b);
    return !strncmp(a, b, l) && (!a[l] || a[l] == '/');
}

int tm_vfs_mount(const char *guest, const char *host, char *why, size_t n) {
    char g[260], h[PATH_MAX];
    if (!guest || !hle_guest_full_path(guest, g, sizeof g) || strlen(g) <= 3) {
        snprintf(why, n, "the guest path %s is not a directory below a drive's root", guest ? guest : "(null)");
        return 0;
    }
    size_t gl = strlen(g);
    if (g[gl - 1] == '\\') g[--gl] = 0;
    const char *last = strrchr(g, '\\');
    if (!last || strlen(last + 1) >= sizeof g_mnt[0].leaf) {
        snprintf(why, n, "the last component of %s is too long", g);
        return 0;
    }
    if (!host || !realpath(host, h)) { snprintf(why, n, "the host directory %s does not exist", host); return 0; }
    struct stat st;
    if (stat(h, &st) || !S_ISDIR(st.st_mode)) { snprintf(why, n, "%s is not a directory", h); return 0; }
    for (int i = 0; i < g_nmnt; i++) {
        if (prefix_ci(g, g_mnt[i].guest) || prefix_ci(g_mnt[i].guest, g)) {
            snprintf(why, n, "%s overlaps the mount %s", g, g_mnt[i].guest);
            return 0;
        }
        if (prefix_host(h, g_mnt[i].host) || prefix_host(g_mnt[i].host, h)) {
            snprintf(why, n, "%s overlaps the host directory of %s (%s)", h, g_mnt[i].guest, g_mnt[i].host);
            return 0;
        }
    }
    if (g_nmnt == TM_MAX_MOUNTS) { snprintf(why, n, "too many mounts"); return 0; }
    snprintf(g_mnt[g_nmnt].guest, sizeof g_mnt[0].guest, "%s", g);
    char *bs = strrchr(g, '\\');
    snprintf(g_mnt[g_nmnt].leaf, sizeof g_mnt[0].leaf, "%s", bs + 1);
    if (bs == g + 2) bs[1] = 0; else *bs = 0;              /* C:\WINDOWS -> C:\ */
    snprintf(g_mnt[g_nmnt].parent, sizeof g_mnt[0].parent, "%s", g);
    snprintf(g_mnt[g_nmnt].host, sizeof g_mnt[0].host, "%s", h);
    g_nmnt++;
    hle_set_mount(g_mnt[g_nmnt - 1].guest, h);
    hle_path_changed();
    return 1;
}

/* ---- virtual entries of a guest directory ---- */
typedef struct { char name[260]; int is_dir; char host[PATH_MAX]; } ventry;   /* host: what gives its times */

/* dir: absolute, normalized; "C:\" for a root, else no trailing '\'. */
static int virtual_entries(const char *dir, ventry *out, int max) {
    int k = 0;
    for (int i = 0; i < g_nmnt && k < max; i++)
        if (!strcasecmp(g_mnt[i].parent, dir)) {
            snprintf(out[k].name, sizeof out[k].name, "%s", g_mnt[i].leaf);
            out[k].is_dir = 1;
            snprintf(out[k].host, sizeof out[k].host, "%s", g_mnt[i].host);
            k++;
        }
    for (int i = 0; i < tm_loader_count() && k < max; i++) {
        const tm_host_dll *d = tm_loader_dll(i);
        if (!d->visible || strcasecmp(tm_loader_dir(d), dir)) continue;
        snprintf(out[k].name, sizeof out[k].name, "%s", d->name);
        out[k].is_dir = 0;
        hle_host_path_base(dir, out[k].host, sizeof out[k].host);
        k++;
    }
    return k;
}

/* Split an absolute guest path into its directory (normalized as above) and
 * its last component. 0 for a root. */
static int split(const char *full, char *dir, size_t dn, char *leaf, size_t ln) {
    const char *bs = strrchr(full, '\\');
    if (!bs || !bs[1]) return 0;
    snprintf(leaf, ln, "%s", bs + 1);
    snprintf(dir, dn, "%.*s", (int)(bs - full == 2 ? 3 : bs - full), full);
    return 1;
}

/* The virtual entry an absolute guest path names, if any. */
static int virtual_of(const char *full, ventry *e) {
    char dir[1024], leaf[260];
    ventry v[TM_VFS_MAX_VIRTUAL];
    if (!split(full, dir, sizeof dir, leaf, sizeof leaf)) return 0;
    int n = virtual_entries(dir, v, TM_VFS_MAX_VIRTUAL);
    for (int i = 0; i < n; i++) if (!strcasecmp(v[i].name, leaf)) { *e = v[i]; return 1; }
    return 0;
}

int tm_vfs_virtual_file(const char *guest) {
    char full[1024];
    ventry e;
    return guest && hle_guest_full_path(guest, full, sizeof full) && virtual_of(full, &e) && !e.is_dir;
}

/* ---- WIN32_FIND_DATAA: attrs, 3 FILETIMEs, size high/low, 2 reserved,
 * cFileName[260], cAlternateFileName[14]: 320 bytes ---- */
static uint64_t ft_of(time_t s, long ns) { return ((uint64_t)s + EPOCH_DIFF) * 10000000ull + (uint64_t)ns / 100u; }
static void put_ft(uint32_t at, uint64_t ft) { MEM32(at) = (uint32_t)ft, MEM32(at + 4) = (uint32_t)(ft >> 32); }
static uint32_t attrs_of(const struct stat *st) {         /* as win32hle's */
    uint32_t a = S_ISDIR(st->st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if (!(st->st_mode & S_IWUSR)) a |= FILE_ATTRIBUTE_READONLY;
    return a;
}
/* host: the file whose metadata is reported; virtual file: size 0, archive. */
static void fill(uint32_t o, const char *name, const char *host, int virtual_file) {
    struct stat st;
    memset(hle_ptr(o), 0, 320);
    if (stat(host, &st) == 0) {
        MEM32(o) = virtual_file ? FILE_ATTRIBUTE_ARCHIVE : attrs_of(&st);
        put_ft(o + 4, ft_of(st.st_mtim.tv_sec, st.st_mtim.tv_nsec));
        put_ft(o + 12, ft_of(st.st_atim.tv_sec, st.st_atim.tv_nsec));
        put_ft(o + 20, ft_of(st.st_mtim.tv_sec, st.st_mtim.tv_nsec));
        if (!virtual_file && !S_ISDIR(st.st_mode))
            MEM32(o + 28) = (uint32_t)((uint64_t)st.st_size >> 32), MEM32(o + 32) = (uint32_t)st.st_size;
    } else if (virtual_file) {
        MEM32(o) = FILE_ATTRIBUTE_ARCHIVE;
    }
    strncpy((char *)hle_ptr(o + 44), name, 259);
}

/* ---- masks ----
 * FindFirstFile rewrites a mask that has a wildcard before matching it:
 * trailing dots and spaces go (a trailing '.' meant "no extension"); a '.'
 * followed by '*' or '?' becomes DOS_DOT; every other '?' becomes DOS_QM;
 * and, for a mask that ended in '.', a final '*' becomes DOS_STAR. The
 * tokens then match as FsRtlIsNameInExpression documents:
 *   *         zero or more characters
 *   ?         exactly one character (not produced by the rewrite)
 *   DOS_STAR  zero or more characters, but not past the name's last '.'
 *   DOS_QM    one character; at a '.' or at the end of the name, it and the
 *             DOS_QMs after it match nothing
 *   DOS_DOT   a '.', or nothing at the end of the name
 * Characters compare without case (ASCII). "." and ".." are matched by a
 * mask made only of wildcard tokens. 8.3 short names are not generated, so
 * a mask is never matched against one (Windows can: "*.htm" may list
 * "a.html" there). */
enum { DOS_STAR = 1, DOS_QM = 2, DOS_DOT = 3 };

static int expr_match(const unsigned char *e, const unsigned char *n, const unsigned char *last_dot, int depth) {
    if (depth > 512) return 0;                               /* a pathological mask */
    for (;;) {
        switch (*e) {
        case 0: return !*n;
        case '*':
            for (const unsigned char *t = n;; t++) {
                if (expr_match(e + 1, t, last_dot, depth + 1)) return 1;
                if (!*t) return 0;
            }
        case DOS_STAR:
            for (const unsigned char *t = n;; t++) {
                if (expr_match(e + 1, t, last_dot, depth + 1)) return 1;
                if (!*t || (last_dot && t == last_dot)) return 0;
            }
        case DOS_QM:
            if (!*n || *n == '.') { while (*e == DOS_QM) e++; continue; }
            e++, n++;
            continue;
        case DOS_DOT:
            if (*n == '.') { e++, n++; continue; }
            if (!*n) { e++; continue; }
            return 0;
        case '?':
            if (!*n) return 0;
            e++, n++;
            continue;
        default: {
            unsigned a = *e, c = *n;
            if (a >= 'A' && a <= 'Z') a |= 0x20;
            if (c >= 'A' && c <= 'Z') c |= 0x20;
            if (!c || a != c) return 0;
            e++, n++;
            continue;
        }
        }
    }
}

/* The mask as FindFirstFile matches it (see above); 0 if it does not fit. */
int tm_vfs_fixup_mask(const char *mask, char *out, size_t n) {
    size_t len = strlen(mask);
    if (len + 1 > n) return 0;
    memcpy(out, mask, len + 1);
    if (!len) return 1;
    int no_ext = out[len - 1] == '.';
    while (len && (out[len - 1] == '.' || out[len - 1] == ' ')) len--;
    for (size_t i = 0; i < len; i++) {
        if (out[i] == '.' && (out[i + 1] == '*' || out[i + 1] == '?')) out[i] = DOS_DOT;
        else if (out[i] == '?') out[i] = DOS_QM;
    }
    out[len] = 0;
    if (no_ext && len && out[len - 1] == '*') out[len - 1] = DOS_STAR;
    return 1;
}

/* name against a mask as given to FindFirstFileA (a mask with a wildcard is
 * rewritten first). */
int tm_vfs_wild(const char *mask, const char *name) {
    char e[TM_MAX_PATH * 2];
    int wildcard = strpbrk(mask, "*?") != NULL;
    if (!wildcard) return !strcasecmp(mask, name);
    if (!tm_vfs_fixup_mask(mask, e, sizeof e)) return 0;
    if (!strcmp(name, ".") || !strcmp(name, "..")) {
        for (const char *c = e; *c; c++)
            if (*c != '*' && *c != '?' && *c != DOS_STAR && *c != DOS_QM && *c != DOS_DOT) return 0;
        return 1;
    }
    const char *dot = strrchr(name, '.');
    return expr_match((const unsigned char *)e, (const unsigned char *)name, (const unsigned char *)dot, 0);
}

/* One search: its names, each either in the host directory (vi < 0) or a
 * virtual entry of v[vi]. */
typedef struct { char name[260]; int vi; } fentry;
typedef struct {
    fentry *e;
    int n, cap, at;
    char hdir[PATH_MAX];
    ventry *v;                               /* the directory's virtual entries */
} tfind;
static void find_closer(void *o) { tfind *f = (tfind *)o; if (f) free(f->e), free(f->v); free(f); }
static int by_name(const void *a, const void *b) {
    const char *x = ((const fentry *)a)->name, *y = ((const fentry *)b)->name;
    int dx = !strcmp(x, ".") ? 0 : !strcmp(x, "..") ? 1 : 2, dy = !strcmp(y, ".") ? 0 : !strcmp(y, "..") ? 1 : 2;
    return dx != dy ? dx - dy : strcasecmp(x, y);
}
/* 0 when memory runs out (nothing added). */
static int add(tfind *f, const char *name, int vi) {
    if (f->n == f->cap) {
        int cap = f->cap ? f->cap * 2 : 64;
        fentry *e = (fentry *)realloc(f->e, sizeof *f->e * (size_t)cap);
        if (!e) return 0;
        f->e = e, f->cap = cap;
    }
    snprintf(f->e[f->n].name, sizeof f->e[0].name, "%s", name);
    f->e[f->n++].vi = vi;
    return 1;
}
static void fill_entry(uint32_t out, const tfind *f, const fentry *e) {
    if (e->vi >= 0) { fill(out, e->name, f->v[e->vi].host, !f->v[e->vi].is_dir); return; }
    char h[PATH_MAX + 264];
    snprintf(h, sizeof h, "%s/%s", f->hdir, e->name);
    fill(out, e->name, h, 0);
}
static void not_found(const char *pat, uint32_t err) {
    if (hle_filetrace()) fprintf(stderr, "[file] FindFirstFileA \"%s\": not found\n", pat);
    hle_set_last_error(err);
}

/* Allocation can be made to fail, for the tests. */
static int g_fail_alloc_after = -1;
void tm_vfs_test_fail_alloc(int after) { g_fail_alloc_after = after; }
static int alloc_fails(void) { return g_fail_alloc_after >= 0 && g_fail_alloc_after-- == 0; }

static void k_FindFirstFileA(void) {                     /* (pattern, &data) */
    static int closer_set;
    if (!closer_set) hle_handle_set_closer(TM_H_FIND, find_closer), closer_set = 1;
    const char *pat = ASTR(0);
    uint32_t out = A32(1);
    char full[1024], dir[1024], mask[TM_MAX_PATH];
    if (!pat || !out) { hle_set_last_error(ERROR_INVALID_PARAMETER); RET(INVALID_HANDLE, 2); }
    size_t pl = strlen(pat);
    if (pl >= TM_MAX_PATH) { not_found(pat, ERROR_FILENAME_EXCED_RANGE); RET(INVALID_HANDLE, 2); }
    if (!pl || pat[pl - 1] == '\\' || pat[pl - 1] == '/') { not_found(pat, ERROR_FILE_NOT_FOUND); RET(INVALID_HANDLE, 2); }
    /* the mask as given ("*." keeps its dot); the directory made absolute */
    const char *sl = pat + pl;
    while (sl > pat && sl[-1] != '\\' && sl[-1] != '/' && sl[-1] != ':') sl--;
    char gdir[TM_MAX_PATH + 2];
    snprintf(mask, sizeof mask, "%s", sl);
    snprintf(gdir, sizeof gdir, "%.*s", (int)(sl - pat), pat);
    if (!gdir[0]) snprintf(gdir, sizeof gdir, ".");
    int dl = hle_guest_full_path(gdir, dir, sizeof dir);
    if (!dl) { not_found(pat, ERROR_PATH_NOT_FOUND); RET(INVALID_HANDLE, 2); }
    if (dl > 3 && dir[dl - 1] == '\\') dir[--dl] = 0;
    snprintf(full, sizeof full, "%s%s%s", dir, dl == 3 ? "" : "\\", mask);
    tfind *f = alloc_fails() ? NULL : (tfind *)calloc(1, sizeof *f);
    if (!f) { not_found(pat, ERROR_NOT_ENOUGH_MEMORY); RET(INVALID_HANDLE, 2); }
    struct stat st;
    if (!hle_host_path_base(dir, f->hdir, sizeof f->hdir) || stat(f->hdir, &st) || !S_ISDIR(st.st_mode)) {
        find_closer(f);
        not_found(pat, ERROR_PATH_NOT_FOUND);
        RET(INVALID_HANDLE, 2);
    }
    ventry vt[TM_VFS_MAX_VIRTUAL];
    int nv = virtual_entries(dir, vt, TM_VFS_MAX_VIRTUAL);
    if (nv) {
        f->v = alloc_fails() ? NULL : (ventry *)malloc(sizeof *f->v * (size_t)nv);
        if (!f->v) { find_closer(f); not_found(pat, ERROR_NOT_ENOUGH_MEMORY); RET(INVALID_HANDLE, 2); }
        memcpy(f->v, vt, sizeof *f->v * (size_t)nv);
    }
    int wildcard = strchr(mask, '*') || strchr(mask, '?');
    int oom = 0;
    if (!wildcard) {
        int done = 0;
        for (int i = 0; i < nv && !done; i++)
            if (!strcasecmp(f->v[i].name, mask)) oom = !add(f, f->v[i].name, i), done = 1;
        char one[PATH_MAX];
        if (!done && strcmp(mask, ".") && strcmp(mask, "..") && hle_host_path_base(full, one, sizeof one) && stat(one, &st) == 0) {
            const char *b = strrchr(one, '/');
            /* the name as it is on disk, in the directory hle_path found it in */
            if (b) snprintf(f->hdir, sizeof f->hdir, "%.*s", (int)(b - one) ? (int)(b - one) : 1, one);
            oom = alloc_fails() || !add(f, b ? b + 1 : one, -1);
        }
    } else {
        int root = strlen(dir) == 3;
        DIR *d = opendir(f->hdir);
        struct dirent *de;
        while (!oom && d && (de = readdir(d))) {
            int dot = !strcmp(de->d_name, ".") || !strcmp(de->d_name, "..");
            if ((dot && root) || !tm_vfs_wild(mask, de->d_name)) continue;
            int hidden = 0;
            for (int i = 0; i < nv; i++) if (!strcasecmp(f->v[i].name, de->d_name)) hidden = 1;
            if (hidden) continue;
            oom = alloc_fails() || !add(f, de->d_name, -1);
        }
        if (d) closedir(d);
        for (int i = 0; i < nv && !oom; i++) if (tm_vfs_wild(mask, f->v[i].name)) oom = !add(f, f->v[i].name, i);
        if (f->n && !oom) qsort(f->e, (size_t)f->n, sizeof *f->e, by_name);
    }
    if (oom) { find_closer(f); not_found(pat, ERROR_NOT_ENOUGH_MEMORY); RET(INVALID_HANDLE, 2); }
    if (!f->n) { find_closer(f); not_found(pat, ERROR_FILE_NOT_FOUND); RET(INVALID_HANDLE, 2); }
    uint32_t h = hle_handle_alloc(TM_H_FIND, f);
    if (!h) { find_closer(f); not_found(pat, ERROR_TOO_MANY_OPEN_FILES); RET(INVALID_HANDLE, 2); }
    fill_entry(out, f, &f->e[0]);
    f->at = 1;
    if (hle_filetrace()) fprintf(stderr, "[file] FindFirstFileA \"%s\" in %s\n", pat, f->hdir);
    RET(h, 2);
}

/* After the last entry the handle stays valid (ERROR_NO_MORE_FILES each
 * time) until FindClose. */
static void k_FindNextFileA(void) {                      /* (handle, &data) */
    tfind *f = (tfind *)hle_handle_obj(A32(0), TM_H_FIND);
    if (!f) { hle_set_last_error(ERROR_INVALID_HANDLE); RET(0, 2); }
    if (!A32(1)) { hle_set_last_error(ERROR_INVALID_PARAMETER); RET(0, 2); }
    if (f->at >= f->n) { hle_set_last_error(ERROR_NO_MORE_FILES); RET(0, 2); }
    fill_entry(A32(1), f, &f->e[f->at++]);
    RET(1, 2);
}

/* Only a search handle: anything else (a file, a closed search) fails with
 * ERROR_INVALID_HANDLE and is left open. */
static void k_FindClose(void) {                          /* (handle) */
    uint32_t h = A32(0);
    if (hle_handle_type(h) != TM_H_FIND) { hle_set_last_error(ERROR_INVALID_HANDLE); RET(0, 1); }
    hle_handle_close(h);
    RET(1, 1);
}

/* ---- single-path queries ---- */
static void delegate(const char *name) { recomp_lookup_import(hle_resolve(name))(); }

/* GetFileAttributesA: INVALID_FILE_ATTRIBUTES with ERROR_FILE_NOT_FOUND when
 * the directory exists but the name is not in it, ERROR_PATH_NOT_FOUND when
 * the directory (or the drive) does not exist or the name is empty,
 * ERROR_FILENAME_EXCED_RANGE past MAX_PATH. */
uint32_t tm_vfs_attributes(const char *name, uint32_t *err) {
    char full[1024], dir[1024], leaf[TM_MAX_PATH], host[PATH_MAX];
    struct stat st;
    *err = 0;
    if (!name || !*name) { *err = ERROR_PATH_NOT_FOUND; return 0xFFFFFFFFu; }
    if (strlen(name) >= TM_MAX_PATH) { *err = ERROR_FILENAME_EXCED_RANGE; return 0xFFFFFFFFu; }
    if (!hle_guest_full_path(name, full, sizeof full)) { *err = ERROR_PATH_NOT_FOUND; return 0xFFFFFFFFu; }
    ventry e;
    if (virtual_of(full, &e) && !e.is_dir) return FILE_ATTRIBUTE_ARCHIVE;
    if (hle_host_path_base(full, host, sizeof host) && stat(host, &st) == 0) return attrs_of(&st);
    int in_dir = split(full, dir, sizeof dir, leaf, sizeof leaf) && hle_host_path_base(dir, host, sizeof host) &&
                 stat(host, &st) == 0 && S_ISDIR(st.st_mode);
    *err = in_dir ? ERROR_FILE_NOT_FOUND : ERROR_PATH_NOT_FOUND;
    return 0xFFFFFFFFu;
}
static void k_GetFileAttributesA(void) {                 /* (name) */
    const char *name = ASTR(0);
    char full[1024];
    if (name && *name && strlen(name) < TM_MAX_PATH && !hle_guest_full_path(name, full, sizeof full)) {
        delegate("GetFileAttributesA");                  /* a UNC name: win32hle's */
        return;
    }
    uint32_t err, a = tm_vfs_attributes(name, &err);
    if (a == 0xFFFFFFFFu) hle_set_last_error(err);
    RET(a, 1);
}

/* GetFullPathNameA(name, size, buf, &filePart): the length without the
 * terminator when it fits, else the size needed with it (buf untouched);
 * filePart is the last component, or NULL when the result ends in '\'. */
static void k_GetFullPathNameA(void) {
    const char *name = ASTR(0);
    uint32_t size = A32(1), part = A32(3);
    char *buf = ASTR(2);
    char full[1024];
    if (!name || !*name) { hle_set_last_error(ERROR_INVALID_NAME); RET(0, 4); }
    int len = hle_guest_full_path(name, full, sizeof full);
    if (!len) { delegate("GetFullPathNameA"); return; }  /* a UNC name: win32hle's */
    if (!buf || size < (uint32_t)len + 1) RET((uint32_t)len + 1, 4);
    memcpy(buf, full, (size_t)len + 1);
    if (part) {
        char *bs = strrchr(buf, '\\');
        MEM32(part) = bs && bs[1] ? hle_va(bs + 1) : 0;
    }
    RET((uint32_t)len, 4);
}

static void k_CreateFileA(void) {                        /* (name, access, share, sa, disposition, flags, template) */
    if (tm_vfs_virtual_file(ASTR(0))) {
        fprintf(stderr, "[vfs] CreateFileA \"%s\": a host DLL entry has no file contents\n", ASTR(0));
        hle_set_last_error(ERROR_ACCESS_DENIED);
        RET(INVALID_HANDLE, 7);
    }
    delegate("CreateFileA");
}
static void k_DeleteFileA(void) {                        /* (name) */
    if (tm_vfs_virtual_file(ASTR(0))) { hle_set_last_error(ERROR_ACCESS_DENIED); RET(0, 1); }
    delegate("DeleteFileA");
}
static void k_CreateDirectoryA(void) {                   /* (name, sa) */
    if (tm_vfs_virtual_file(ASTR(0))) { hle_set_last_error(ERROR_ALREADY_EXISTS); RET(0, 2); }
    delegate("CreateDirectoryA");
}

const win32hle_shim tm_vfs_overrides[] = {
    { "tm:FindFirstFileA",     k_FindFirstFileA },
    { "tm:FindNextFileA",      k_FindNextFileA },
    { "tm:FindClose",          k_FindClose },
    { "tm:GetFileAttributesA", k_GetFileAttributesA },
    { "tm:GetFullPathNameA",   k_GetFullPathNameA },
    { "tm:CreateFileA",        k_CreateFileA },
    { "tm:DeleteFileA",        k_DeleteFileA },
    { "tm:CreateDirectoryA",   k_CreateDirectoryA },
    { 0, 0 }
};
