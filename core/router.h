/*
 * router.h — Binary trie path router
 * O(depth) lookup where depth = number of path segments (typically 1-3).
 * No malloc, no hashing. Operates on the static arena trie array.
 */
#ifndef WZ_ROUTER_H
#define WZ_ROUTER_H

#include <stdint.h>
#include "pool.h"
#include "bundle.h"

/* Route lookup result */
typedef struct {
    int32_t asset_idx;    /* >= 0 if static file, otherwise -1 */
    int32_t handler_idx;  /* >= 0 if dynamic handler, otherwise -1 */
    int     found;        /* 0 = 404, 1 = match */
} RouteMatch;

/*
 * Build the in-memory trie from the route table section of the bundle.
 * Populates arena.trie[]. Called once at startup.
 */
int router_build(const Bundle *b);

/*
 * Look up a URL path in the trie.
 * path: null-terminated string like "/about/team"
 * Returns RouteMatch with the matching asset or handler index.
 */
RouteMatch router_lookup(const char *path);

/*
 * Dump trie contents to stderr (debug builds only).
 */
void router_dump(void);

#endif /* WZ_ROUTER_H */
