/*
 * main.c — WebZero entry point
 * Request pipeline:
 *   accept → parse headers → trie_lookup → send asset / run VM handler → done
 *
 * Zero malloc after startup. Zero threads.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "core/pool.h"
#include "core/bundle.h"
#include "core/router.h"
#include "core/vm.h"
#include "platform/platform.h"

/* ------------------------------------------------------------------ */
/* Pre-built response header templates (never formatted at runtime)    */
/* ------------------------------------------------------------------ */

static const char HDR_200_BR[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Encoding: br\r\n"
    "Cache-Control: max-age=31536000, immutable\r\n"
<<<<<<< HEAD
    "Vary: Accept, Accept-Encoding\r\n"
=======
    "Vary: Accept-Encoding\r\n"
>>>>>>> 38ba2c925942c3074670f9c31b3703f4b206263d
    "Content-Length: ";
/* Append: <len>\r\n\r\n then asset bytes */

static const char HDR_200_RAW[] =
    "HTTP/1.1 200 OK\r\n"
    "Cache-Control: max-age=3600\r\n"
    "Content-Length: ";

static const char HDR_404[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 9\r\n\r\n"
    "Not Found";

static const char HDR_405[] =
    "HTTP/1.1 405 Method Not Allowed\r\n"
    "Content-Length: 0\r\n\r\n";

static const char HDR_500[] =
    "HTTP/1.1 500 Internal Server Error\r\n"
    "Content-Length: 0\r\n\r\n";

static const char HDR_302_PREFIX[] =
    "HTTP/1.1 302 Found\r\nLocation: ";
static const char HDR_302_SUFFIX[] =
    "\r\nContent-Length: 0\r\n\r\n";

/* ------------------------------------------------------------------ */
/* Loaded bundle (global, single instance)                             */
/* ------------------------------------------------------------------ */

static Bundle g_bundle;

/* ------------------------------------------------------------------ */
/* HTTP request mini-parser (header-only, no body yet)                */
/* ------------------------------------------------------------------ */

typedef struct {
    char  method[8];
    char  path[512];
    char  query[256];    /* raw query string after '?' */
    char  version[10];
    int   keep_alive;
<<<<<<< HEAD
    int   accepts_webp;  /* non-zero if Accept header contains image/webp */
=======
>>>>>>> 38ba2c925942c3074670f9c31b3703f4b206263d
} HTTPRequest;

/*
 * Parse the first line and Connection header from buf.
 * Returns 0 on success, -1 if the request is malformed.
 */
static int parse_request(const uint8_t *buf, uint32_t len, HTTPRequest *req) {
    const char *p   = (const char *)buf;
    const char *end = p + len;

    /* Method */
    int i = 0;
    while (p < end && *p != ' ' && i < 7) req->method[i++] = *p++;
    req->method[i] = '\0';
    if (p >= end || *p != ' ') return -1;
    p++;

    /* Path */
    i = 0;
    while (p < end && *p != ' ' && *p != '?' && i < 511) req->path[i++] = *p++;
    req->path[i] = '\0';

    /* Query string */
    req->query[0] = '\0';
    if (p < end && *p == '?') {
        p++;
        i = 0;
        while (p < end && *p != ' ' && i < 255) req->query[i++] = *p++;
        req->query[i] = '\0';
    }

    /* HTTP version */
    if (p < end && *p == ' ') p++;
    i = 0;
    while (p < end && *p != '\r' && *p != '\n' && i < 9) req->version[i++] = *p++;
    req->version[i] = '\0';

<<<<<<< HEAD
    /* Scan headers for Connection and Accept */
    req->keep_alive   = (strncmp(req->version, "HTTP/1.1", 8) == 0);
    req->accepts_webp = 0;
=======
    /* Scan headers for Connection */
    req->keep_alive = (strncmp(req->version, "HTTP/1.1", 8) == 0);
>>>>>>> 38ba2c925942c3074670f9c31b3703f4b206263d
    const char *h = p;
    while (h < end - 4) {
        if (*h == '\r' && *(h+1) == '\n') {
            h += 2;
            if (strncasecmp(h, "connection:", 11) == 0) {
<<<<<<< HEAD
                const char *v = h + 11;
                while (v < end && *v == ' ') v++;
                if (strncasecmp(v, "close", 5) == 0)      req->keep_alive = 0;
                if (strncasecmp(v, "keep-alive", 10) == 0) req->keep_alive = 1;
            } else if (strncasecmp(h, "accept:", 7) == 0) {
                /* Scan the Accept value for "image/webp" */
                const char *v = h + 7;
                while (v < end && *v == ' ') v++;
                /* Walk tokens separated by comma */
                while (v < end && *v != '\r' && *v != '\n') {
                    while (v < end && (*v == ' ' || *v == ',')) v++;
                    if (strncasecmp(v, "image/webp", 10) == 0) {
                        req->accepts_webp = 1;
                        break;
                    }
                    /* Skip to next comma */
                    while (v < end && *v != ',' && *v != '\r' && *v != '\n') v++;
                }
            }
            if (*h == '\r' && *(h+1) == '\n') break; /* blank line = end of headers */
=======
                h += 11;
                while (h < end && *h == ' ') h++;
                if (strncasecmp(h, "close", 5) == 0)      req->keep_alive = 0;
                if (strncasecmp(h, "keep-alive", 10) == 0) req->keep_alive = 1;
            }
            if (*h == '\r' && *(h+1) == '\n') break; /* blank line = end */
>>>>>>> 38ba2c925942c3074670f9c31b3703f4b206263d
        } else {
            h++;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Number → ascii helper (avoids sprintf during serving)              */
/* ------------------------------------------------------------------ */

/* Writes decimal representation of n into buf, returns char count. */
static int u32_to_str(uint32_t n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[12];
    int  i = 0;
    while (n > 0) { tmp[i++] = (char)('0' + n % 10); n /= 10; }
    int len = i;
    for (int j = 0; j < len; j++) buf[j] = tmp[len - 1 - j];
    buf[len] = '\0';
    return len;
}

/* ------------------------------------------------------------------ */
/* Response senders                                                    */
/* ------------------------------------------------------------------ */

static void send_asset(ConnState *c, const AssetEntry *asset) {
    const uint8_t *data = g_bundle.base + asset->offset;
    uint32_t       dlen = asset->compressed_len;

    /* Build header in scratch buffer */
    pool_scratch_reset();

    const char *hdr_template = (asset->encoding == 1) ? HDR_200_BR : HDR_200_RAW;
    size_t hlen = strlen(hdr_template);

    uint8_t *hdr_buf = pool_scratch_alloc(hlen + 32 + strlen(asset->mime) + 32);
    if (!hdr_buf) {
        platform_send(c, HDR_500, sizeof(HDR_500) - 1);
        return;
    }

    /* Write: HTTP/1.1 200 OK\r\nContent-Type: MIME\r\n...Content-Length: N\r\n\r\n */
    size_t off = 0;
    memcpy(hdr_buf + off, hdr_template, hlen); off += hlen;

    /* Content-Length value */
    char numstr[12];
    int  nlen = u32_to_str(dlen, numstr);
    memcpy(hdr_buf + off, numstr, (size_t)nlen); off += (size_t)nlen;
    memcpy(hdr_buf + off, "\r\nContent-Type: ", 16); off += 16;
    size_t mlen = strlen(asset->mime);
    memcpy(hdr_buf + off, asset->mime, mlen); off += mlen;
    memcpy(hdr_buf + off, "\r\n\r\n", 4); off += 4;

    platform_send(c, hdr_buf, off);
    platform_send_file(c, data, dlen);
}

static void send_vm_response(ConnState *c, const VMResponse *res) {
    pool_scratch_reset();

    if (res->redirect_to[0] != '\0') {
        uint8_t *buf = pool_scratch_alloc(512);
        if (!buf) { platform_send(c, HDR_500, sizeof(HDR_500) - 1); return; }
        size_t off = 0;
        size_t pl = sizeof(HDR_302_PREFIX) - 1;
        size_t sl = sizeof(HDR_302_SUFFIX) - 1;
        size_t rl = strlen(res->redirect_to);
        memcpy(buf + off, HDR_302_PREFIX, pl); off += pl;
        memcpy(buf + off, res->redirect_to, rl); off += rl;
        memcpy(buf + off, HDR_302_SUFFIX, sl); off += sl;
        platform_send(c, buf, off);
        return;
    }

    const char *ct = res->content_type[0] ? res->content_type
                                           : "text/plain; charset=utf-8";
    size_t ctl  = strlen(ct);
    uint32_t blen = res->body_len;

    char numstr[12];
    int  nlen = u32_to_str(blen, numstr);

    /* "HTTP/1.1 XXX ...\r\nContent-Type: ...\r\nContent-Length: N\r\n\r\n" */
    uint8_t *buf = pool_scratch_alloc(128 + ctl + (size_t)nlen + blen);
    if (!buf) { platform_send(c, HDR_500, sizeof(HDR_500) - 1); return; }

    size_t off = 0;
    /* Status line */
    const char *status_line;
    switch (res->status) {
        case 200: status_line = "HTTP/1.1 200 OK\r\n"; break;
        case 201: status_line = "HTTP/1.1 201 Created\r\n"; break;
        case 400: status_line = "HTTP/1.1 400 Bad Request\r\n"; break;
        case 401: status_line = "HTTP/1.1 401 Unauthorized\r\n"; break;
        case 403: status_line = "HTTP/1.1 403 Forbidden\r\n"; break;
        case 404: status_line = "HTTP/1.1 404 Not Found\r\n"; break;
        default:  status_line = "HTTP/1.1 200 OK\r\n"; break;
    }
    size_t sll = strlen(status_line);
    memcpy(buf + off, status_line, sll); off += sll;
    memcpy(buf + off, "Content-Type: ", 14); off += 14;
    memcpy(buf + off, ct, ctl); off += ctl;
    memcpy(buf + off, "\r\nContent-Length: ", 18); off += 18;
    memcpy(buf + off, numstr, (size_t)nlen); off += (size_t)nlen;
    memcpy(buf + off, "\r\n\r\n", 4); off += 4;
    if (blen > 0) {
        memcpy(buf + off, res->body, blen);
        off += blen;
    }

    platform_send(c, buf, off);
}

/* ------------------------------------------------------------------ */
/* Main request handler (called by platform event loop)               */
/* ------------------------------------------------------------------ */

static void handle_request(ConnState *c, const uint8_t *buf, uint32_t len) {
    HTTPRequest req;
    if (parse_request(buf, len, &req) < 0) {
        platform_send(c, HDR_405, sizeof(HDR_405) - 1);
        if (!req.keep_alive) platform_close(c);
        return;
    }

    /* Only GET and HEAD supported for static; POST for dynamic */
    int is_head = (strncmp(req.method, "HEAD", 4) == 0);
    int is_get  = (strncmp(req.method, "GET",  3) == 0);
    int is_post = (strncmp(req.method, "POST", 4) == 0);

    if (!is_get && !is_head && !is_post) {
        platform_send(c, HDR_405, sizeof(HDR_405) - 1);
        if (!req.keep_alive) platform_close(c);
        return;
    }

    /* Default path for bare "/" */
    const char *path = req.path;
    if (path[0] == '/' && path[1] == '\0') path = "/index";

    RouteMatch match = router_lookup(path);

    if (!match.found) {
        platform_send(c, HDR_404, sizeof(HDR_404) - 1);
        if (!req.keep_alive) platform_close(c);
        return;
    }

    if (match.asset_idx >= 0) {
        /* Static asset */
        if ((uint32_t)match.asset_idx >= g_bundle.config.asset_count) {
            platform_send(c, HDR_500, sizeof(HDR_500) - 1);
        } else {
            const AssetEntry *asset = &g_bundle.assets[match.asset_idx];
<<<<<<< HEAD

            /*
             * WebP content negotiation: if the client accepts image/webp and
             * this asset has a pre-built WebP companion bundled alongside it,
             * serve the WebP version transparently.  No encoding at runtime —
             * the WebP bytes were placed in the bundle at build time by wz.js.
             */
            if (req.accepts_webp
                    && asset->webp_idx >= 0
                    && (uint32_t)asset->webp_idx < g_bundle.config.asset_count) {
                asset = &g_bundle.assets[asset->webp_idx];
            }

=======
>>>>>>> 38ba2c925942c3074670f9c31b3703f4b206263d
            if (!is_head) {
                send_asset(c, asset);
            } else {
                /* HEAD: send headers only */
                const char *hdr_template = (asset->encoding == 1)
                                            ? HDR_200_BR : HDR_200_RAW;
                pool_scratch_reset();
                uint8_t *hbuf = pool_scratch_alloc(256);
                if (hbuf) {
                    size_t off = strlen(hdr_template);
                    memcpy(hbuf, hdr_template, off);
                    char ns[12];
                    off += (size_t)u32_to_str(asset->compressed_len, ns);
                    memcpy(hbuf + off - (size_t)u32_to_str(asset->compressed_len, ns),
                           ns, (size_t)u32_to_str(asset->compressed_len, ns));
                    memcpy(hbuf + off, "\r\n\r\n", 4); off += 4;
                    platform_send(c, hbuf, off);
                }
            }
        }
    } else if (match.handler_idx >= 0) {
        /* Dynamic handler via VM */
        if ((uint32_t)match.handler_idx >= g_bundle.config.handler_count) {
            platform_send(c, HDR_500, sizeof(HDR_500) - 1);
        } else {
            const HandlerEntry *he = &g_bundle.handlers[match.handler_idx];
            const uint8_t *bc     = g_bundle.base + he->offset;

            VMRequest vmreq;
            vmreq.method   = req.method;
            vmreq.path     = req.path;
            vmreq.query    = req.query;
            vmreq.body     = NULL;
            vmreq.body_len = 0;
            vmreq.fd       = c->fd;

            VMResponse vmres;
            VMResult rv = vm_run(bc, he->len, &vmreq, &vmres);
            if (rv != VM_OK) {
                platform_send(c, HDR_500, sizeof(HDR_500) - 1);
            } else {
                send_vm_response(c, &vmres);
            }
        }
    }

    if (!req.keep_alive) {
        platform_close(c);
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {
    const char *bundle_path = NULL;
    int         port        = 8080;

    /* Minimal arg parsing: webzero <bundle.web> [port] */
    if (argc < 2) {
        fprintf(stderr,
            "Usage: webzero <site.web> [port]\n"
            "       webzero --help\n");
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        printf(
            "webzero — ultra-minimalist web server\n"
            "  webzero <site.web> [port=8080]\n"
            "\n"
            "  The .web bundle contains the entire site.\n"
            "  Build bundles with: node tools/wz.js build ./my-site\n"
        );
        return 0;
    }

    bundle_path = argv[1];
    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "webzero: invalid port '%s'\n", argv[2]);
            return 1;
        }
    }

    /* Load bundle (mmap) */
    if (bundle_load(bundle_path, &g_bundle) != 0) return 1;

    /* Use port from bundle config if not overridden */
    if (argc < 3 && g_bundle.config.port != 0) {
        port = (int)g_bundle.config.port;
    }

    fprintf(stderr, "webzero: loaded '%s' (%zu bytes, %u assets, %u handlers)\n",
            bundle_path,
            g_bundle.file_size,
            g_bundle.config.asset_count,
            g_bundle.config.handler_count);

    /* Build routing trie from bundle */
    if (router_build(&g_bundle) != 0) {
        bundle_unload(&g_bundle);
        return 1;
    }

#ifdef WZ_DEBUG
    router_dump();
#endif

    /* Initialize platform (socket, epoll/IOCP) */
    if (platform_init(port, MAX_CONNS) != 0) {
        bundle_unload(&g_bundle);
        return 1;
    }

    fprintf(stderr, "webzero: arena size %zu bytes (%u conn slots)\n",
            sizeof(Arena), MAX_CONNS);

    /* Enter event loop — never returns until SIGINT/SIGTERM */
    platform_run(handle_request);

    /* Cleanup (rarely reached in production) */
    bundle_unload(&g_bundle);
    fprintf(stderr, "\nwebzero: shutdown complete\n");
    return 0;
}
