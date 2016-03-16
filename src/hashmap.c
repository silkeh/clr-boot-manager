/*
 * This file is part of clr-boot-manager.
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hashmap.h"

#define INITIAL_SIZE 61

/* When we're "full", 70% */
#define FULL_FACTOR 0.7

/* Multiple to increase bucket size by */
#define INCREASE_FACTOR 4

/**
 * An bucket/chain within the hashmap
 */
typedef struct CbmHashmapEntry {
        void *hash;                             /**<The key for this item */
        void *value;                            /**<Value for this item */
        struct CbmHashmapEntry *next;         /**<Next item in the chain */
        bool occ;                               /**<Whether this bucket is occupied */
} CbmHashmapEntry;

/**
 * A CbmHashmap
 */
struct CbmHashmap {
        int size;                       /**<Current size of the hashmap */
        int next_size;                  /**<Next size at which we need to resize */
        int n_buckets;                  /**<Current number of buckets */
        CbmHashmapEntry *buckets;     /**<Stores our bucket chains */

        hash_create_func hash;         /**<Hash generation function */
        hash_compare_func compare;     /**<Key comparison function */
        hash_free_func key_free;        /**<Cleanup function for keys */
        hash_free_func value_free;      /**<Cleanup function for values */
};

/**
 * Iteration object
 */
typedef struct _CbmHashmapIter {
        int bucket;              /**<Current bucket position */
        CbmHashmap *map;        /**<Associated CbmHashmap */
        void *item;              /**<Current item in this iteration */
} _CbmHashmapIter;

static void cbm_hashmap_update_next_size(CbmHashmap *self);
static bool cbm_hashmap_resize(CbmHashmap *self);

static inline bool cbm_hashmap_maybe_resize(CbmHashmap *self)
{
        if (!self) {
                return false;
        }
        if (self->size >= self->next_size) {
                return true;
        }
        return false;
}

static CbmHashmap *cbm_hashmap_new_internal(hash_create_func create , hash_compare_func compare, hash_free_func key_free, hash_free_func value_free)
{
        CbmHashmap *map = NULL;
        CbmHashmapEntry *buckets = NULL;

        map = calloc(1, sizeof(CbmHashmap));
        if (!map) {
                return NULL;
        }

        buckets = calloc(INITIAL_SIZE, sizeof(CbmHashmapEntry));
        if (!buckets ) {
                free(map);
                return NULL;
        }
        map->buckets = buckets;
        map->n_buckets = INITIAL_SIZE;
        map->hash = create ? create : simple_hash;
        map->compare = compare ? compare : simple_compare;
        map->key_free = key_free;
        map->value_free = value_free;
        map->size = 0;

        cbm_hashmap_update_next_size(map);

        return map;
}

CbmHashmap *cbm_hashmap_new(hash_create_func create, hash_compare_func compare)
{
        return cbm_hashmap_new_internal(create, compare, NULL, NULL);
}

CbmHashmap *cbm_hashmap_new_full(hash_create_func create , hash_compare_func compare, hash_free_func key_free, hash_free_func value_free)
{
        return cbm_hashmap_new_internal(create, compare, key_free, value_free);
}

static inline unsigned cbm_hashmap_get_hash(CbmHashmap *self, const void *key)
{
        unsigned hash = self->hash(key);
        return hash;
}

static bool cbm_hashmap_insert_bucket(CbmHashmap *self, CbmHashmapEntry *buckets, int n_buckets, unsigned hash, const void *key, void *value)
{
        if (!self || !buckets) {
                return false;
        }

        CbmHashmapEntry *row = &(buckets[hash % n_buckets]);
        CbmHashmapEntry *head = NULL;
        CbmHashmapEntry *parent = head = row;
        bool can_replace = false;
        CbmHashmapEntry *tomb = NULL;
        int ret = 1;

        while (row) {
                if (!row->occ) {
                        tomb = row;
                }
                parent = row;
                if (row->occ && row->hash == key) {
                        if (self->compare(row->hash, key)) {
                                can_replace = true;
                                break;
                        }
                }
                row = row->next;
        }

        if (can_replace) {
                /* Replace existing allocations. */
                if (self->value_free) {
                        self->value_free(row->value);
                }
                if (self->key_free) {
                        self->key_free(row->hash);
                }
                ret = 0;
        } else if (tomb) {
                row = tomb;
        }

        if (!row) {
                row = calloc(1, sizeof(CbmHashmapEntry));
                if (!row) {
                        return -1;
                }
        }

        row->hash = (void*)key;
        row->value = value;
        row->occ = true;
        if (parent != row && parent) {
                parent->next = row;
        }

        return ret;
}

bool cbm_hashmap_put(CbmHashmap *self, const void *key, void *value)
{
        if (!self) {
                return false;
        }
        int inc;

        if (cbm_hashmap_maybe_resize(self)) {
                if (!cbm_hashmap_resize(self)) {
                        return false;
                }
        }
        unsigned hash = cbm_hashmap_get_hash(self, key);
        inc = cbm_hashmap_insert_bucket(self, self->buckets, self->n_buckets, hash, key, value);
        if (inc > 0) {
                self->size += inc;
                return true;
        } else {
                return false;
        }
}

static CbmHashmapEntry *cbm_hashmap_get_entry(CbmHashmap *self, const void *key)
{
        if (!self) {
                return NULL;
        }

        unsigned hash = cbm_hashmap_get_hash(self, key);
        CbmHashmapEntry *row = &(self->buckets[hash % self->n_buckets]);

        while (row) {
                if (self->compare(row->hash, key)) {
                        return row;
                }
                row = row->next;
        }
        return NULL;
}

void *cbm_hashmap_get(CbmHashmap *self, const void *key)
{
        if (!self) {
                return NULL;
        }

        CbmHashmapEntry *row = cbm_hashmap_get_entry(self, key);
        if (row) {
                return row->value;
        }
        return NULL;
}

static bool cbm_hashmap_remove_internal(CbmHashmap *self, const void *key, bool remove)
{
        if (!self) {
                return false;
        }
        CbmHashmapEntry *row = cbm_hashmap_get_entry(self, key);

        if (!row) {
                return false;
        }

        if (remove) {
                if (self->key_free) {
                        self->key_free(row->hash);
                }
                if (self->value_free) {
                        self->value_free(row->value);
                }
        }
        self->size -= 1;
        row->hash = NULL;
        row->value = NULL;
        row->occ = false;

        return true;
}

bool cbm_hashmap_steal(CbmHashmap *self, const void *key)
{
        return cbm_hashmap_remove_internal(self, key, false);
}

bool cbm_hashmap_remove(CbmHashmap *self, const void *key)
{
        return cbm_hashmap_remove_internal(self, key, true);
}

bool cbm_hashmap_contains(CbmHashmap *self, const void *key)
{
        return (cbm_hashmap_get(self, key)) != NULL;
}

static inline void cbm_hashmap_free_bucket(CbmHashmap *self, CbmHashmapEntry *bucket, bool nuke)
{
        if (!self) {
                return;
        }
        CbmHashmapEntry *tmp = bucket;
        CbmHashmapEntry *bk = bucket;
        CbmHashmapEntry *root = bucket;

        while (tmp) {
                bk = NULL;
                if (tmp->next) {
                        bk = tmp->next;
                }

                if (tmp->occ && nuke) {
                        if (self->key_free) {
                                self->key_free(tmp->hash);
                        }
                        if (self->value_free) {
                                self->value_free(tmp->value);
                        }
                }
                if (tmp != root) {
                        free(tmp);
                }
                tmp = bk;
        }
}

void cbm_hashmap_free(CbmHashmap *self)
{
        if (!self) {
                return;
        }
        for (int i = 0; i < self->n_buckets; i++) {
                CbmHashmapEntry *row = &(self->buckets[i]);
                cbm_hashmap_free_bucket(self, row, true);
        }
        if (self->buckets) {
                free(self->buckets);
        }

        free(self);

}

static void cbm_hashmap_update_next_size(CbmHashmap *self)
{
        if (!self) {
                return;
        }
        self->next_size = (int) (self->n_buckets * FULL_FACTOR);
}

int cbm_hashmap_size(CbmHashmap *self)
{
        if (!self) {
                return -1;
        }
        return self->size;
}

static bool cbm_hashmap_resize(CbmHashmap *self)
{
        if (!self || !self->buckets) {
                return false;
        }

        CbmHashmapEntry *old_buckets = self->buckets;
        CbmHashmapEntry *new_buckets = NULL;
        CbmHashmapEntry *entry = NULL;
        int incr;

        int old_size, new_size;
        int items = 0;

        new_size = old_size = self->n_buckets;
        new_size *= INCREASE_FACTOR;

        new_buckets = calloc(new_size, sizeof(CbmHashmapEntry));
        if (!new_buckets) {
                return false;
        }

        for (int i = 0; i < old_size; i++) {
                entry = &(old_buckets[i]);
                while (entry) {
                        if (entry->occ) {
                                unsigned hash = cbm_hashmap_get_hash(self, entry->hash);
                                if ((incr = cbm_hashmap_insert_bucket(self, new_buckets, new_size, hash, entry->hash, entry->value)) > 0) {
                                        items += incr;
                                } else {
                                        /* Likely a memory issue */
                                        goto failure;
                                }
                        }
                        entry = entry->next;
                }
        }
        /* Successfully resized - do this separately because we need to gaurantee old data is preserved */
        for (int i = 0; i < old_size; i++) {
                cbm_hashmap_free_bucket(self, &(old_buckets[i]), false);
        }

        free(old_buckets);
        self->n_buckets = new_size;
        self->size = items;
        self->buckets = new_buckets;

        cbm_hashmap_update_next_size(self);
        return true;

failure:
        for (int i = 0; i < new_size; i++) {
                cbm_hashmap_free_bucket(self, &(new_buckets[i]), true);
        }
        free(new_buckets);
        return false;
}


void cbm_hashmap_iter_init(CbmHashmap *map, CbmHashmapIter *citer)
{
        _CbmHashmapIter *iter = NULL;
        if (!map || !citer) {
                return;
        }
        iter = (_CbmHashmapIter*)citer;
        _CbmHashmapIter it = {
               .bucket = -1,
               .map = map,
               .item = NULL,
        };
        *iter = it;
}

bool cbm_hashmap_iter_next(CbmHashmapIter *citer, void **key, void **value)
{
        _CbmHashmapIter *iter = NULL;
        CbmHashmapEntry *item = NULL;

        if (!citer) {
                return false;
        }

        iter = (_CbmHashmapIter*)citer;
        if (!iter->map) {
                return false;
        }

        item = iter->item;

        for (;;) {
                if (iter->bucket >= iter->map->n_buckets) {
                        if (item && !item->next) {
                                return false;
                        }
                }
                if (!item) {
                        iter->bucket++;
                        if (iter->bucket > iter->map->n_buckets-1) {
                                return false;
                        }
                        item = &(iter->map->buckets[iter->bucket]);
                }
                if (item && item->occ) {
                        goto success;
                }
                item = item->next;
        }
        return false;

success:
        iter->item = item->next;
        if (key) {
                *key = item->hash;
        }
        if (value) {
                *value = item->value;
        }

        return true;
}   

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */
