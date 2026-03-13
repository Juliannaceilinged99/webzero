/*
 * bundle.c — .web bundle loader
 * Maps the entire site into virtual memory at startup.
 * No file I/O during request serving.
 */
#include "bundle.h"
#include "pool.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* Portable file mapping                                               */
/* ------------------------------------------------------------------ */

#ifdef _WIN32

static HANDLE g_file_handle   = INVALID_HANDLE_VALUE;
static HANDLE g_mapping_handle = NULL;

static const uint8_t *map_file(const char *path, size_t *out_size) {
    g_file_handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                NULL);
    if (g_file_handle == INVALID_HANDLE_VALUE) return NULL;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(g_file_handle, &sz)) {
        CloseHandle(g_file_handle);
        return NULL;
    }
    *out_size = (size_t)sz.QuadPart;

    g_mapping_handle = CreateFileMappingA(g_file_handle, NULL,
                                          PAGE_READONLY, 0, 0, NULL);
    if (!g_mapping_handle) {
        CloseHandle(g_file_handle);
        return NULL;
    }

    const uint8_t *base = (const uint8_t *)MapViewOfFile(g_mapping_handle,
                                                          FILE_MAP_READ, 0, 0, 0);
    if (!base) {
        CloseHandle(g_mapping_handle);
        CloseHandle(g_file_handle);
        return NULL;
    }
    return base;
}

static void unmap_file(const uint8_t *base, size_t size) {
    (void)size;
    if (base)              UnmapViewOfFile((LPCVOID)base);
    if (g_mapping_handle)  CloseHandle(g_mapping_handle);
    if (g_file_handle != INVALID_HANDLE_VALUE) CloseHandle(g_file_handle);
}

#else /* POSIX */

static int g_fd = -1;

static const uint8_t *map_file(const char *path, size_t *out_size) {
    g_fd = open(path, O_RDONLY);
    if (g_fd < 0) return NULL;

    struct stat st;
    if (fstat(g_fd, &st) < 0) { close(g_fd); return NULL; }
    *out_size = (size_t)st.st_size;

    const uint8_t *base = (const uint8_t *)mmap(NULL, *out_size,
                                                  PROT_READ,
                                                  MAP_SHARED | MAP_POPULATE,
                                                  g_fd, 0);
    if (base == MAP_FAILED) { close(g_fd); return NULL; }
    return base;
}

static void unmap_file(const uint8_t *base, size_t size) {
    if (base && base != MAP_FAILED) munmap((void *)base, size);
    if (g_fd >= 0) close(g_fd);
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int bundle_load(const char *path, Bundle *out) {
    memset(out, 0, sizeof(*out));

    out->base = map_file(path, &out->file_size);
    if (!out->base) {
        fprintf(stderr, "webzero: cannot map bundle '%s'\n", path);
        return -1;
    }

    if (bundle_validate(out) != 0) {
        unmap_file(out->base, out->file_size);
        return -1;
    }

    const BundleHeader *hdr = (const BundleHeader *)out->base;

    /* Parse config section */
    if (hdr->config_offset + sizeof(BundleConfig) > out->file_size) {
        fprintf(stderr, "webzero: bundle config section out of bounds\n");
        unmap_file(out->base, out->file_size);
        return -1;
    }
    memcpy(&out->config,
           out->base + hdr->config_offset,
           sizeof(BundleConfig));

    /* Set asset/handler pointers */
    out->assets   = (const AssetEntry *)(out->base + hdr->assets_offset);
    out->handlers = (const HandlerEntry *)(out->base + hdr->handlers_offset);

    return 0;
}

void bundle_unload(Bundle *b) {
    if (b && b->base) {
        unmap_file(b->base, b->file_size);
        b->base = NULL;
    }
}

int bundle_validate(const Bundle *b) {
    if (b->file_size < sizeof(BundleHeader)) {
        fprintf(stderr, "webzero: bundle too small\n");
        return -1;
    }
    const BundleHeader *hdr = (const BundleHeader *)b->base;
    if (hdr->magic != WEB_MAGIC) {
        fprintf(stderr, "webzero: bad magic (got 0x%08X, want 0x%08X)\n",
                hdr->magic, WEB_MAGIC);
        return -1;
    }
    if (hdr->version != WEB_VERSION) {
        fprintf(stderr, "webzero: unsupported bundle version %u\n", hdr->version);
        return -1;
    }
    if (hdr->total_size != (uint32_t)b->file_size) {
        fprintf(stderr, "webzero: bundle size mismatch (header says %u, file is %zu)\n",
                hdr->total_size, b->file_size);
        return -1;
    }
    return 0;
}
