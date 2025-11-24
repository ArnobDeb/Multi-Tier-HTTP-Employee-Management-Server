// cache.c -- a simple LRU cache with hash table and doubly-linked list
#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

atomic_ulong CACHE_HITS = 0;
atomic_ulong CACHE_MISSES = 0;

typedef struct node {
    char *key;
    char *val;
    struct node *prev, *next;
    struct node *hnext; // for hashtable chaining
} node_t;

struct cache_t {
    size_t cap;
    size_t size;
    node_t *head, *tail; // LRU list: head most recent
    node_t **htable;
    size_t hsize;
    pthread_mutex_t lock;
};

static unsigned long hash_str(const char *s) {
    unsigned long h = 5381;
    int c;
    while ((c = *s++)) h = ((h << 5) + h) + c;
    return h;
}

cache_t *cache_create(size_t capacity) {
    cache_t *c = calloc(1, sizeof(*c));
    c->cap = capacity;
    c->size = 0;
    c->hsize = capacity * 2 + 1;
    c->htable = calloc(c->hsize, sizeof(node_t*));
    pthread_mutex_init(&c->lock, NULL);
    return c;
}

static void detach(cache_t *c, node_t *n) {
    if (!n) return;
    if (n->prev) n->prev->next = n->next;
    else c->head = n->next;
    if (n->next) n->next->prev = n->prev;
    else c->tail = n->prev;
    n->prev = n->next = NULL;
}

static void attach_head(cache_t *c, node_t *n) {
    n->prev = NULL;
    n->next = c->head;
    if (c->head) c->head->prev = n;
    c->head = n;
    if (!c->tail) c->tail = n;
}

void cache_put(cache_t *c, const char *key, const char *value) {
    pthread_mutex_lock(&c->lock);
    unsigned long h = hash_str(key) % c->hsize;
    node_t *cur = c->htable[h];
    while (cur) {
        if (strcmp(cur->key, key) == 0) {
            // update
            free(cur->val);
            cur->val = strdup(value);
            detach(c, cur);
            attach_head(c, cur);
            pthread_mutex_unlock(&c->lock);
            return;
        }
        cur = cur->hnext;
    }
    // new node
    node_t *n = calloc(1, sizeof(*n));
    n->key = strdup(key);
    n->val = strdup(value);
    // insert into ht
    n->hnext = c->htable[h];
    c->htable[h] = n;
    // attach list
    attach_head(c, n);
    c->size++;
    // evict if necessary
    if (c->size > c->cap) {
        // remove tail
        node_t *rem = c->tail;
        detach(c, rem);
        // remove from hash table
        unsigned long hh = hash_str(rem->key) % c->hsize;
        node_t **pp = &c->htable[hh];
        while (*pp && *pp != rem) pp = &((*pp)->hnext);
        if (*pp) *pp = rem->hnext;
        free(rem->key);
        free(rem->val);
        free(rem);
        c->size--;
    }
    pthread_mutex_unlock(&c->lock);
}

char *cache_get(cache_t *c, const char *key) {
    pthread_mutex_lock(&c->lock);
    unsigned long h = hash_str(key) % c->hsize;
    node_t *cur = c->htable[h];
    while (cur) {
        if (strcmp(cur->key, key) == 0) {
            // Cache HIT
            atomic_fetch_add(&CACHE_HITS, 1);

            // move to head
            detach(c, cur);
            attach_head(c, cur);
            char *out = strdup(cur->val);
            pthread_mutex_unlock(&c->lock);
            return out;
        }
        cur = cur->hnext;
    }
     // Cache MISS
    atomic_fetch_add(&CACHE_MISSES, 1);
    pthread_mutex_unlock(&c->lock);
    return NULL;
}

void cache_delete(cache_t *c, const char *key) {
    pthread_mutex_lock(&c->lock);
    unsigned long h = hash_str(key) % c->hsize;
    node_t **pp = &c->htable[h];
    while (*pp) {
        if (strcmp((*pp)->key, key) == 0) {
            node_t *rem = *pp;
            *pp = rem->hnext;
            detach(c, rem);
            free(rem->key);
            free(rem->val);
            free(rem);
            c->size--;
            pthread_mutex_unlock(&c->lock);
            return;
        }
        pp = &((*pp)->hnext);
    }
    pthread_mutex_unlock(&c->lock);
}

void cache_destroy(cache_t *c) {
    if (!c) return;
    for (size_t i = 0; i < c->hsize; ++i) {
        node_t *cur = c->htable[i];
        while (cur) {
            node_t *nx = cur->hnext;
            free(cur->key);
            free(cur->val);
            free(cur);
            cur = nx;
        }
    }
    free(c->htable);
    pthread_mutex_destroy(&c->lock);
    free(c);
}

