/*
 * pool.h — Static memory pool for WebZero
 * Zero malloc during request serving. All memory lives in BSS.
 */
#ifndef WZ_POOL_H
#define WZ_POOL_H

#include <stdint.h>
#include <stddef.h>

#define POOL_SIZE      (4 * 1024 * 1024)  /* 4MB total arena */
#define MAX_CONNS      256
#define CONN_BUF_SIZE  8192
#define SCRATCH_SIZE   65536
#define MAX_TRIE_NODES 1024

/* Per-connection state */
typedef struct {
    int      fd;
    uint32_t buf_len;
    uint32_t bytes_sent;
    int32_t  asset_idx;
    int32_t  handler_idx;
    uint8_t  alive;     /* keep-alive flag */
    uint8_t  active;
    uint8_t  _pad[2];
} ConnState;

/* Trie node for path routing */
typedef struct {
    char     segment[32];
    uint16_t child_count;
    uint16_t children[8];   /* indices into trie array */
    int32_t  asset_idx;     /* -1 if not a leaf */
    int32_t  handler_idx;   /* -1 if static */
} TrieNode;

/* The single global arena — lives in BSS, zero-initialized */
typedef struct {
    uint8_t    conn_bufs[MAX_CONNS][CONN_BUF_SIZE];
    ConnState  conns[MAX_CONNS];
    uint8_t    scratch[SCRATCH_SIZE];
    TrieNode   trie[MAX_TRIE_NODES];
    uint32_t   trie_count;
} Arena;

extern Arena arena;

/* Scratch buffer helpers — single-use, reset per request */
void     pool_scratch_reset(void);
uint8_t *pool_scratch_alloc(size_t n);

#endif /* WZ_POOL_H */
