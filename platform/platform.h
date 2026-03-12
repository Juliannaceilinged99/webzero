/*
 * platform.h — Platform abstraction layer
 * Implemented by platform/linux.c and platform/windows.c
 */
#ifndef WZ_PLATFORM_H
#define WZ_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include "core/pool.h"

/* Callback type: called by the event loop for each request */
typedef void (*serve_fn)(ConnState *c, const uint8_t *buf, uint32_t len);

/*
 * Initialize the listening socket and I/O subsystem.
 * port     : TCP port to listen on
 * max_conn : max simultaneous connections (capped to MAX_CONNS)
 * Returns  : 0 on success, -1 on error
 */
int platform_init(int port, int max_conn);

/*
 * Enter the event loop. Blocks until the process receives SIGINT/SIGTERM.
 * handler  : called for each complete HTTP request chunk
 */
void platform_run(serve_fn handler);

/*
 * Send a memory buffer over the connection.
 * For static assets this calls sendfile()/TransmitFile() for zero-copy.
 * data     : pointer into mmap'd bundle
 * len      : byte count
 * Returns  : bytes sent, or -1 on error
 */
int platform_send_file(ConnState *c, const void *data, size_t len);

/*
 * Send a small in-memory buffer (headers, error pages).
 * Returns  : bytes sent, or -1 on error
 */
int platform_send(ConnState *c, const void *buf, size_t len);

/*
 * Close the connection and return the slot to the pool.
 */
void platform_close(ConnState *c);

/*
 * Return a monotonic millisecond timestamp (for keep-alive timeouts).
 */
uint64_t platform_now_ms(void);

#endif /* WZ_PLATFORM_H */
