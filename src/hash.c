/*
 * hash.c
 *
 *  Created on: May 24, 2015
 *      Author: goyuan
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hash.h"

#define HASH_BIT 16

#define MAX_BUCKETS 0xffffffff

#define GOLDEN_RATIO_PRIME_32 0x9e370001UL

typedef struct to_find_node_t {
    const void* key;
    LIBCACHE_CMP_KEY* kcmp;
} to_find_node_t;

static u32 get_bits(u32 val)
{
    u32 count = 1;
    while (val >>= 1 != 0) {
        count++;
    }
    return count;
}

static u32 get_bucket_number(u32 val)
{
    if (val >= 32) {
        return MAX_BUCKETS;
    } else {
        return pow(2, val) - 1;
    }
}

static inline u32 hash_32(u32 val, u32 bits)
{
    u32 hash = val * GOLDEN_RATIO_PRIME_32;
    return hash >> (32 - bits);
}

static u32 key_to_hash(hash_t* hash, const void* key)
{

    u32 value = hash->k2num(key);
    return hash_32(value, hash->bits);
}

static int find_node(node_t* node, void* usr_data)
{
    to_find_node_t* to_find_node = (to_find_node_t*) usr_data;
    if (node == NULL) {
        DEBUG_ERROR("input parameter node is null.");
        return -1;
    }
    hash_data_t* hd = (hash_data_t*) node->usr_data;
    return to_find_node->kcmp(to_find_node->key, hd->key);
}

static void free_node(node_t* node)
{
    if (node == NULL) {
        DEBUG_ERROR("input parameter %s is null.", "node");
        return;
    }
    hash_data_t* hd = (hash_data_t*) node->usr_data;
    if (hd != NULL) {
        if (hd->key != NULL) {
            free(hd->key);
        }
        free(hd);
    }
    free(node);
    return;
}

void* hash_init(int max_entry, int key_size, LIBCACHE_CMP_KEY* key_cmp, LIBCACHE_KEY_TO_NUMBER* key_to_num)
{
    hash_t* hash = (hash_t*) malloc(sizeof(hash_t));
    if (hash == NULL) {
        DEBUG_ERROR("%s init failed: ouf of memory!", "hash");
        return NULL;
    }
    hash->bits = get_bits(max_entry);
    hash->buckets_count = get_bucket_number(hash->bits);

    hash->bucket_list = (bucket_t*) malloc((hash->buckets_count + 1) * sizeof(bucket_t));
    hash->entry_count = 0;
    hash->key_size = key_size;
    hash->kcmp = key_cmp;
    hash->k2num = key_to_num;

    int i = 0;
    while (i <= hash->buckets_count) {
        hash->bucket_list[i].list_count = 0;
        hash->bucket_list[i].list = NULL;
        i++;
    }
    return hash;
}

void* hash_add(void* hash_table, const void* key, void* cache_node)
{
    if (hash_table == NULL || key == NULL) {
        DEBUG_ERROR("input parameter %s %s is null.",
                    (NULL == hash_table) ? "hash_table" : "",
                    (NULL == key) ? "key" : "");
        return NULL;
    }
    hash_t* hash = (hash_t*) hash_table;
    u32 hash_code = key_to_hash(hash, key);
    if (hash_code > hash->buckets_count) {
        DEBUG_ERROR("hash key is invalid: %d", hash_code);
        return NULL;
    }

    node_t* node = (node_t*) malloc(sizeof(node_t));
    if (node == NULL) {
        DEBUG_ERROR("%s get memory failed, out of memory!", "hash");
        return NULL;
    }
    node->usr_data = (hash_data_t*) malloc(sizeof(hash_data_t));
    hash_data_t* hd = (hash_data_t*) node->usr_data;
    hd->key = malloc(hash->key_size);
    memset(hd->key, 0, hash->key_size);
    memcpy(hd->key, key, hash->key_size);

    hd->cache_node_ptr = cache_node;
    node->next_node = NULL;
    node->previous_node = NULL;
    bucket_t* bucket = &(hash->bucket_list[hash_code]);
    if (bucket->list == NULL) {
        bucket->list = (list_t*) malloc(sizeof(list_t));
        bucket->list_count = 0;
        list_init(bucket->list);
    }
    list_push_back(bucket->list, node);

    bucket->list_count++;
    hash->entry_count++;
    return node;
}

int hash_del(void* hash_table, const void* key, void* hash_node)
{
    if (hash_table == NULL || hash_node == NULL || key == NULL) {
        DEBUG_ERROR("input parameter %s %s %s is null.",
                    (NULL == hash_table) ? "hash_table" : "",
                    (NULL == key) ? "key" : "",
                    (NULL == hash_node) ? "hash_node" : "");
        return -1;
    }
    hash_t* hash = (hash_t*) hash_table;

    u32 hash_code = key_to_hash(hash, key);
    if (hash_code > hash->buckets_count) {
        DEBUG_ERROR("hash key is invalid: %d", hash_code);
        return -1;
    }

    bucket_t* bucket = &(hash->bucket_list[hash_code]);
    if (bucket->list == NULL) {
        DEBUG_ERROR("%s is NULL.", "hash_list");
        return -1;
    } else {
        node_t* node = (node_t*) hash_node;
        list_remove(bucket->list, node);
        free_node(node);
    }
    bucket->list_count--;
    hash->entry_count--;
    return 0;
}

void* hash_find(void* hash_table, const void* key)
{
    if (hash_table == NULL || key == NULL) {
        DEBUG_ERROR("input parameter %s %s is null.",
                    (NULL == hash_table) ? "hash_table" : "",
                    (NULL == key) ? "key" : "");
        return NULL;
    }

    hash_t *hash = (hash_t*) hash_table;
    u32 hash_code = key_to_hash(hash, key);
    if (hash_code > hash->buckets_count) {
        DEBUG_ERROR("hash key is invalid: %d", hash_code);
        return NULL;
    }
    bucket_t* bucket = &(hash->bucket_list[hash_code]);
    if (bucket->list == NULL) {
        DEBUG_ERROR("%s is NULL.", "hash_list");
        return NULL;
    } else {
        to_find_node_t to_find_node;
        to_find_node.key = key;
        to_find_node.kcmp = hash->kcmp;
        node_t* node = list_foreach_with_usr_data(bucket->list, find_node, (void*) &to_find_node);
        if (node == NULL) {
            DEBUG_ERROR(" %s can't find the key.", "list_foreach_with_usr_data");
            return NULL;
        }
        return node;
    }
}

int hash_get_count(const void* hash_table)
{
    if (hash_table == NULL) {
        DEBUG_ERROR("input parameter %s is null.", "hash_table");
        return -1;
    }
    const hash_t* hash = (const hash_t*) hash_table;
    return hash->entry_count;
}

static void hash_release(void* hash_table, int is_destroy)
{
    if (hash_table == NULL) {
        DEBUG_ERROR("input parameter %s is null.", "hash_table");
        return;
    }
    hash_t* hash = (hash_t*) hash_table;
    int i = 0;
    for (i = 0; i <= hash->buckets_count; i++) {
        bucket_t* bucket = &(hash->bucket_list[i]);
        if (bucket->list != NULL) {
            list_clear(bucket->list, free_node);
            free(bucket->list);
            bucket->list = NULL;
            bucket->list_count = 0;
        }
    }
    if (is_destroy) {
        free(hash->bucket_list);
        free(hash);
    } else {
        hash->entry_count = 0;
    }
}

void hash_free(void* hash)
{
    hash_release(hash, FALSE);
}

void hash_destroy(void* hash)
{
    hash_release(hash, TRUE);
}
