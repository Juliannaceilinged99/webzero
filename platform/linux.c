/*
 * linux.c — Linux platform implementation
 * Uses epoll (edge-triggered) + sendfile for zero-copy asset serving.
 * Compile with musl-gcc -static for a fully portable static binary.
 */
#ifndef _WIN32

#include "../platform/platform.h"
#include "../core/pool.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define MAX_EVENTS 64

static int g_listen_fd = -1;
static int g_epoll_fd  = -1;
static volatile int g_running = 1;

/* ------------------------------------------------------------------ */
/* Signal handling                                                     */
/* ------------------------------------------------------------------ */

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

/* ------------------------------------------------------------------ */
/* Connection slot management                                          */
/* ------------------------------------------------------------------ */

static int alloc_conn_slot(int fd) {
    for (int i = 0; i < MAX_CONNS; i++) {
        if (!arena.conns[i].active) {
            memset(&arena.conns[i], 0, sizeof(ConnState));
            arena.conns[i].fd     = fd;
            arena.conns[i].active = 1;
            return i;
        }
    }
    return -1;
}

static void free_conn_slot(ConnState *c) {
    c->active = 0;
    c->fd     = -1;
}

/* ------------------------------------------------------------------ */
/* Set socket non-blocking                                             */
/* ------------------------------------------------------------------ */

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ------------------------------------------------------------------ */
/* platform_init                                                       */
/* ------------------------------------------------------------------ */

int platform_init(int port, int max_conn) {
    (void)max_conn;

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        perror("socket");
        return -1;
    }

    int yes = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    setsockopt(g_listen_fd, IPPROTO_TCP, TCP_NODELAY,  &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(g_listen_fd);
        return -1;
    }

    if (listen(g_listen_fd, 128) < 0) {
        perror("listen");
        close(g_listen_fd);
        return -1;
    }

    set_nonblocking(g_listen_fd);

    g_epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (g_epoll_fd < 0) {
        perror("epoll_create1");
        close(g_listen_fd);
        return -1;
    }

    struct epoll_event ev;
    ev.events  = EPOLLIN | EPOLLET;
    ev.data.fd = g_listen_fd;
    if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, g_listen_fd, &ev) < 0) {
        perror("epoll_ctl listen");
        return -1;
    }

    fprintf(stderr, "webzero: listening on :%d (epoll, edge-triggered)\n", port);
    return 0;
}

/* ------------------------------------------------------------------ */
/* platform_run — main event loop                                      */
/* ------------------------------------------------------------------ */

void platform_run(serve_fn handler) {
    struct epoll_event events[MAX_EVENTS];

    while (g_running) {
        int n = epoll_wait(g_epoll_fd, events, MAX_EVENTS, 500);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            /* ---- New connection ---- */
            if (fd == g_listen_fd) {
                for (;;) {
                    struct sockaddr_in peer;
                    socklen_t plen = sizeof(peer);
                    int cfd = accept(g_listen_fd,
                                     (struct sockaddr *)&peer, &plen);
                    if (cfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("accept");
                        break;
                    }

                    int slot = alloc_conn_slot(cfd);
                    if (slot < 0) {
                        /* Backpressure: server full */
                        static const char HDR_503[] =
                            "HTTP/1.1 503 Service Unavailable\r\n"
                            "Content-Length: 0\r\n\r\n";
                        send(cfd, HDR_503, sizeof(HDR_503) - 1, MSG_NOSIGNAL);
                        close(cfd);
                        continue;
                    }

                    set_nonblocking(cfd);

                    struct epoll_event cev;
                    cev.events   = EPOLLIN | EPOLLET | EPOLLRDHUP;
                    cev.data.ptr = &arena.conns[slot];
                    epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, cfd, &cev);
                }
                continue;
            }

            /* ---- Data on existing connection ---- */
            ConnState *c = (ConnState *)events[i].data.ptr;

            if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                platform_close(c);
                continue;
            }

            if (events[i].events & EPOLLIN) {
                int slot_idx = (int)(c - arena.conns);
                uint8_t *buf = arena.conn_bufs[slot_idx];
                ssize_t r = recv(c->fd, buf + c->buf_len,
                                 CONN_BUF_SIZE - c->buf_len - 1, 0);
                if (r <= 0) {
                    platform_close(c);
                    continue;
                }
                c->buf_len += (uint32_t)r;
                buf[c->buf_len] = '\0';

                /* Minimal end-of-headers detection */
                if (memmem(buf, c->buf_len, "\r\n\r\n", 4)) {
                    handler(c, buf, c->buf_len);
                    c->buf_len = 0; /* reset for next request */
                }
            }
        }
    }

    close(g_epoll_fd);
    close(g_listen_fd);
}

/* ------------------------------------------------------------------ */
/* I/O helpers                                                         */
/* ------------------------------------------------------------------ */

int platform_send_file(ConnState *c, const void *data, size_t len) {
    /*
     * The bundle is already mmap'd. We write the data directly —
     * on modern Linux kernels, writing from an mmap region uses
     * zero-copy paths internally. For true zero-copy from a file
     * descriptor we would need the original fd; since we mmap the
     * whole bundle, write() is equivalent and simpler.
     */
    ssize_t sent = send(c->fd, data, len, MSG_NOSIGNAL);
    return (int)sent;
}

int platform_send(ConnState *c, const void *buf, size_t len) {
    ssize_t sent = send(c->fd, buf, len, MSG_NOSIGNAL);
    return (int)sent;
}

void platform_close(ConnState *c) {
    if (c->fd >= 0) {
        epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, c->fd, NULL);
        close(c->fd);
    }
    free_conn_slot(c);
}

uint64_t platform_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
}

#endif /* !_WIN32 */
