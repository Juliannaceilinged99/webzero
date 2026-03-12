/*
 * router.c — Binary trie router
 * Builds and traverses a static trie for O(depth) path lookups.
 * Trie nodes live in arena.trie[] — no malloc.
 */
#include "router.h"
#include "pool.h"
#include "bundle.h"

#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/*
 * Advance *p past the next path segment, writing it into seg (max 31 chars).
 * Returns the length of the segment, 0 if end of path.
 * Segments are split on '/'.
 */
static int next_segment(const char **p, char *seg) {
    /* skip leading slashes */
    while (**p == '/') (*p)++;
    if (**p == '\0' || **p == '?') return 0;

    int i = 0;
    while (**p != '\0' && **p != '/' && **p != '?' && i < 31) {
        seg[i++] = **p;
        (*p)++;
    }
    seg[i] = '\0';
    return i;
}

/*
 * Find a child of node_idx whose segment matches seg.
 * Returns child index, or -1 if not found.
 */
static int find_child(uint16_t node_idx, const char *seg) {
    const TrieNode *node = &arena.trie[node_idx];
    for (int i = 0; i < node->child_count; i++) {
        uint16_t ci = node->children[i];
        if (strncmp(arena.trie[ci].segment, seg, 32) == 0) {
            return (int)ci;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Bundle route table format (on-disk)                                 */
/* ------------------------------------------------------------------ */

/*
 * On-disk trie node (packed, stored in ROUTE TABLE section):
 *   char     segment[32]
 *   uint16_t child_count
 *   uint16_t children[8]
 *   int32_t  asset_idx
 *   int32_t  handler_idx
 * Total: 32 + 2 + 16 + 4 + 4 = 58 bytes — but we pad to 64 for alignment.
 */
#define DISK_TRIE_NODE_SIZE 64

typedef struct {
    char     segment[32];
    uint16_t child_count;
    uint16_t children[8];
    int32_t  asset_idx;
    int32_t  handler_idx;
    uint8_t  _pad[6];
} __attribute__((packed)) DiskTrieNode;

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int router_build(const Bundle *b) {
    const BundleHeader *hdr = (const BundleHeader *)b->base;
    uint32_t count = b->config.route_node_count;

    if (count > MAX_TRIE_NODES) {
        fprintf(stderr, "webzero: trie too large (%u nodes, max %d)\n",
                count, MAX_TRIE_NODES);
        return -1;
    }

    const DiskTrieNode *disk = (const DiskTrieNode *)(b->base + hdr->route_table_offset);

    for (uint32_t i = 0; i < count; i++) {
        TrieNode *n = &arena.trie[i];
        memcpy(n->segment, disk[i].segment, 32);
        n->child_count = disk[i].child_count;
        for (int j = 0; j < 8; j++) {
            n->children[j] = disk[i].children[j];
        }
        n->asset_idx   = disk[i].asset_idx;
        n->handler_idx = disk[i].handler_idx;
    }

    arena.trie_count = count;
    return 0;
}

RouteMatch router_lookup(const char *path) {
    RouteMatch result = { -1, -1, 0 };

    if (arena.trie_count == 0) return result;

    /* Node 0 is always the root "/" */
    uint16_t current = 0;
    const char *p    = path;
    char seg[32];

    /* Root match */
    while (next_segment(&p, seg) > 0) {
        int ci = find_child(current, seg);
        if (ci < 0) {
            /* Try wildcard child ("*") */
            ci = find_child(current, "*");
            if (ci < 0) return result; /* 404 */
        }
        current = (uint16_t)ci;
    }

    const TrieNode *node = &arena.trie[current];
    if (node->asset_idx < 0 && node->handler_idx < 0) {
        return result; /* intermediate node, not a leaf */
    }

    result.asset_idx   = node->asset_idx;
    result.handler_idx = node->handler_idx;
    result.found       = 1;
    return result;
}

void router_dump(void) {
#ifdef WZ_DEBUG
    fprintf(stderr, "=== Router Trie (%u nodes) ===\n", arena.trie_count);
    for (uint32_t i = 0; i < arena.trie_count; i++) {
        const TrieNode *n = &arena.trie[i];
        fprintf(stderr, "[%3u] \"%s\" children=%u asset=%d handler=%d\n",
                i, n->segment, n->child_count,
                n->asset_idx, n->handler_idx);
    }
    fprintf(stderr, "==============================\n");
#endif
}
