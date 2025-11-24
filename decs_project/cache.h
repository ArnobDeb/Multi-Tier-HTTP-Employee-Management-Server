// cache.h -- simple LRU cache API (string keys & values)
#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>
#include <stdatomic.h>
extern atomic_ulong CACHE_HITS;
extern atomic_ulong CACHE_MISSES;

typedef struct cache_t cache_t;

cache_t *cache_create(size_t capacity);
void cache_destroy(cache_t *c);

void cache_put(cache_t *c, const char *key, const char *value);
/*
 * cache_get returns a heap-allocated string (caller must free)
 */
char *cache_get(cache_t *c, const char *key);
void cache_delete(cache_t *c, const char *key);

#endif

