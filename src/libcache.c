#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include "libcache.h"
#include "libcache_def.h"
#include "libpool.h"
#include "hash.h"

typedef struct libcache_node_usr_data_t
{
    void* key;
    node_t* hash_node_ptr;
    void* pool_element_ptr;
    uint32_t lock_counter;
}libcache_node_usr_data_t;

typedef struct libcache_t
{
    void* pool;
    void* hash_table;
    list_t* list;
    size_t entry_size;
    size_t key_size;
    libcache_scale_t max_entry_number;
    LIBCACHE_FREE_MEMORY* free_memory;
    LIBCACHE_FREE_ENTRY* free_entry;
}libcache_t;

/*
 *  @brief libcache_create    creates a cache object
 *
 *  @param max_entry_number      maximum entry number that this cache is able to store.
 *  @param entry_size            size of an entry, bytes
 *  @param key_size              size of a key, bytes
 *  @param allocate_memory       function to allocate memory for this cache object, e.g. malloc().
 *  @param free_memory           function to free whole cache object, e.g. free().
 *  @param free_entry            function to free entry and key, it can be NULL if there isn't any resource to release.
 *  @param cmp_key               function to compare two keys, e.g. hash table, avl tree can use it.
 *  @param key_to_number         function to translate key to a number, e.g. hash table can use it.
 *  @return                      pointer of a cache object.
 */
void* libcache_create(
        libcache_scale_t max_entry_number,
        size_t entry_size,
        size_t key_size,
        LIBCACHE_ALLOCATE_MEMORY* allocate_memory,
        LIBCACHE_FREE_MEMORY* free_memory,
        LIBCACHE_FREE_ENTRY* free_entry,
        LIBCACHE_CMP_KEY* cmp_key,
        LIBCACHE_KEY_TO_NUMBER* key_to_number)
{
    if (allocate_memory == NULL || free_memory == NULL) {
        DEBUG_ERROR("argument %s and %s can not be NULL.", "allocate_memory", "free_memory");
        return NULL;
    }

    size_t pool_element_size = entry_size + key_size;
    while (0 != pool_element_size % 4) {
        pool_element_size++;
    }

    libcache_t* libcache = (libcache_t*)malloc(sizeof(libcache_t));

    pool_attr_t pool_attr[] = {{entry_size, max_entry_number}};
    size_t large_mem_size = pool_caculate_total_length(POOL_TYPE_MAX, pool_attr);

    void *large_memory = allocate_memory(large_mem_size);
    libcache->pool = pools_init(large_memory, large_mem_size, POOL_TYPE_MAX, pool_attr);
    // libcache->pool = pool_init(entry_size * max_entry_number, allocate_memory, free_memory);
    // return_t init_result = pool_init_element_pool(libcache->pool, entry_size, max_entry_number);

    libcache->hash_table = hash_init(max_entry_number, key_size, cmp_key, key_to_number);

    libcache->list = (list_t*)malloc(sizeof(list_t));
    list_init(libcache->list);

    libcache->entry_size = entry_size;
    libcache->key_size = key_size;
    libcache->max_entry_number = max_entry_number;
    libcache->free_memory = free_memory;
    libcache->free_entry = free_entry;

    if (NULL == libcache->pool || NULL == libcache->hash_table) {
        hash_free(libcache->hash_table);
        free(libcache->list);
        free(libcache);
        libcache = NULL;
        DEBUG_ERROR("init %s failed.", (NULL == libcache->pool) ? "pool" : "hash");
    }

    return libcache;
}

/*
 *  @brief libcache_lookup   To look up an cache entry with a given key.
 *
 *  @param libcache          cache object, cannot be NULL.
 *  @param key               key, cannot be NULL.
 *  @param dst_entry         a copy of entry that fetch by key. it could be NULL.
 *  @return NULL             didn't find out such entry with the key.
 *          pointer          points to an entry with the key.
 *  NOTE:  The entry in cache will be locked if dst_entry is NULL, one entry can be locked many times.
 *         libcache_unlock_entry should be called to unlock the entry when the entry is not being used this time.
 */
void* libcache_lookup(void* libcache, const void* key, void* dst_entry)
{
    libcache_t* libcache_ptr = (libcache_t*)libcache;
    if (NULL == libcache_ptr) {
        DEBUG_ERROR("input parameter %s is null", "libcache");
        return NULL;
    }

    if (NULL == key) {
        DEBUG_ERROR("input parameter %s is null", "key");
        return NULL;
    }

    void* return_value = NULL;

    do {
        // Note: find the entry according to key
        node_t* hash_node = (node_t*)hash_find(libcache_ptr->hash_table, key);
        if (NULL == hash_node) {
            break;
        }

        node_t* libcache_node = (node_t*)((hash_data_t*)hash_node->usr_data)->cache_node_ptr;
        if (NULL == libcache_node) {
            break;
        }

        if (NULL == dst_entry) {
            // TODO: lock should be added here
            ((libcache_node_usr_data_t*)libcache_node->usr_data)->lock_counter++;

            return_value = ((libcache_node_usr_data_t*)libcache_node->usr_data)->pool_element_ptr;
        } else {
            // Note: copy into dst_entry and return NULL, no lock added too
            memcpy(dst_entry, ((libcache_node_usr_data_t*)libcache_node->usr_data)->pool_element_ptr, libcache_ptr->entry_size);
            return_value = dst_entry;
        }

        // Note: put the newest found node in front of list
        list_remove(libcache_ptr->list, libcache_node);
        list_push_front(libcache_ptr->list, libcache_node);

    } while(0);

    return return_value;
}

/*
 *  @brief libcache_get_unlock_node  find unlocked node in list.
 *
 *  @param node             node in list.
 *  @return 0: traversing over; 1: continue traversing
 */
static int libcache_get_unlock_node(node_t *node) {
    return (((libcache_node_usr_data_t*)node->usr_data)->lock_counter) ? 1 : 0;
}

/*
 *  @brief libcache_add         attempts to add an entry with a given key.
 *
 *  @param libcache             cache object, cannot be NULL.
 *  @param key                  key, cannot be NULL.
 *  @param src_entry            entry with an expected value to add.
 *  @return NULL                could not add the entry because an entry with the same key is existing.
 *          pointer             points to an entry with the key, so user can write value to it.
 *  NOTE:   The entry in cache will be locked if src_entry is NULL, one entry can be locked many times.
 *          libcache_unlock_entry should be called to unlock the e ntry when the entry is not being used this time.
 */
void* libcache_add(void * libcache, const void* key, const void* src_entry)
{
    libcache_t* libcache_ptr = (libcache_t*)libcache;
    if (NULL == libcache_ptr) {
        DEBUG_ERROR("input parameter %s is null", "libcache");
        return NULL;
    }

    if (NULL == key) {
        DEBUG_ERROR("input parameter %s is null", "key");
        return NULL;
    }

    void* return_value = NULL;

    // Note: find node, if node isn't existed and add it
    do {
        // Note: find node from hash by key, so not add the data
        node_t* hash_node = (node_t*)hash_find(libcache_ptr->hash_table, key);
        if (NULL != hash_node) {
            DEBUG_INFO("the key is existed in cache");
            break;
        }

        node_t* libcache_list_unlock_node = NULL;
        int is_node_new_created = FALSE;

        // Note: if cache pool is full, check unlocked node in libcache list back
        if (libcache_ptr->max_entry_number <= libcache_ptr->list->total_nodes) {
            // Note: if no unlocked node in libcache list, return directly
            DEBUG_INFO("the cache is full, try to swap old data out");
            libcache_list_unlock_node = list_reverse_foreach(libcache_ptr->list, libcache_get_unlock_node);
            if (NULL == libcache_list_unlock_node) {
                DEBUG_INFO("all data are in use, swap failed!");
                break;
            } else { // Note: if have unlocked node in libcache list
                DEBUG_INFO("swap data successfully!");
                list_remove(libcache_ptr->list, libcache_list_unlock_node);
                libcache_list_unlock_node->next_node = NULL;
                libcache_list_unlock_node->previous_node = NULL;
                libcache_node_usr_data_t* cache_data = (libcache_node_usr_data_t*) libcache_list_unlock_node->usr_data;

                hash_del(libcache_ptr->hash_table, cache_data->key, cache_data->hash_node_ptr);
                memset(cache_data->key, 0, libcache_ptr->key_size);
            }
        } else { // Note: if cache pool is not full, create new node
            libcache_list_unlock_node = (node_t*)malloc(sizeof(node_t));

            libcache_list_unlock_node->usr_data = malloc(sizeof(libcache_node_usr_data_t));
            ((libcache_node_usr_data_t*)libcache_list_unlock_node->usr_data)->key = malloc(libcache_ptr->key_size);
            is_node_new_created = TRUE;
        }

        // Note: update node data
        libcache_node_usr_data_t* libcache_node_usr_data = (libcache_node_usr_data_t*)libcache_list_unlock_node->usr_data;
        if (is_node_new_created) {
            libcache_node_usr_data->pool_element_ptr = pool_get_element(libcache_ptr->pool, POOL_TYPE_DATA);
        }

        if (NULL != src_entry) {
             memcpy(libcache_node_usr_data->pool_element_ptr, src_entry, libcache_ptr->entry_size);
        }
        memcpy(libcache_node_usr_data->key, key, libcache_ptr->key_size);
        libcache_node_usr_data->lock_counter = 0;

        // Note: push node in front of libcache list
        list_push_front(libcache_ptr->list, libcache_list_unlock_node);

        // Note: add node into pool element
        if (is_node_new_created) {
            if (OK != pool_set_reserved_pointer(libcache_node_usr_data->pool_element_ptr, (void*)libcache_list_unlock_node)) {
                DEBUG_ERROR("add data into cache failed");
                // free resource
                list_pop_front(libcache_ptr->list);
                pool_free_element(libcache_ptr->pool, POOL_TYPE_DATA, libcache_node_usr_data->pool_element_ptr);

                free(libcache_list_unlock_node->usr_data);
                free(libcache_list_unlock_node);
                break;
            }
        }

        // Note: add node into hash
        libcache_node_usr_data->hash_node_ptr = hash_add(libcache_ptr->hash_table, key, libcache_list_unlock_node);

        // Note: the entry in cache will be locked if src_entry is NULL
        if (NULL == src_entry) {
            libcache_node_usr_data->lock_counter++;
        }

        return_value = libcache_node_usr_data->pool_element_ptr;
    } while (0);

    return return_value;
}

/*
 *  @brief libcache_delete_by_key attempts to delete an entry with a given key.
 *
 *  @param libcache               cache object, cannot be NULL.
 *  @param key                    key, cannot be NULL.
 *  @return
 *          LIBCACHE_NOT_FOUND    entry wasn't found.
 *          LIBCACHE_LOCKED       the entry was unable to deleted because it's locked.
 *          LIBCACHE_SUCCESS      the entry was deleted successfully.
 */
libcache_ret_t  libcache_delete_by_key(void * libcache, const void* key)
{
    libcache_t* libcache_ptr = (libcache_t*)libcache;
    if (NULL == libcache_ptr) {
        DEBUG_ERROR("input parameter %s is null", "libcache");
        return LIBCACHE_FAILURE;
    }

    if (NULL == key) {
        DEBUG_ERROR("input parameter %s is null", "key");
        return LIBCACHE_FAILURE;
    }

    libcache_ret_t return_value = LIBCACHE_SUCCESS;
    do {
        node_t* hash_node = (node_t*)hash_find(libcache_ptr->hash_table, key);
        if (NULL == hash_node) {
            return_value = LIBCACHE_NOT_FOUND;
            break;
        }

        // Note: if the entry is locked, just return
        node_t* libcache_node = (node_t*)((hash_data_t*)hash_node->usr_data)->cache_node_ptr;
        libcache_node_usr_data_t* libcache_node_usr_data = (libcache_node_usr_data_t*)libcache_node->usr_data;
         if (libcache_node_usr_data->lock_counter > 0) {
             return_value = LIBCACHE_LOCKED;
             break;
         }

        // Note: delete node from hash
        (void) hash_del(libcache_ptr->hash_table, key, libcache_node_usr_data->hash_node_ptr);

        // Note: delete node from pool
        pool_free_element(libcache_ptr->pool, POOL_TYPE_DATA, libcache_node_usr_data->pool_element_ptr);

        // Note: delete node from list
        list_remove(libcache_ptr->list, libcache_node);

        // Note: free node resource
        free(libcache_node_usr_data);
        free(libcache_node);

        return_value = LIBCACHE_SUCCESS;
    } while(0);

    return return_value;
}

/*
 *  @brief libcache_delete_entry   attempts to delete an entry.
 *
 *  @param libcache                cache object, cannot be NULL.
 *  @param entry                   entry (returned by libcache_lookup/libcache_add) in the cache.
 *  @return
*           LIBCACHE_NOT_FOUND     entry wasn't found.
*           LIBCACHE_LOCKED        entry was unable to deleted while it's locked.
 *          LIBCACHE_SUCCESS       entry was deleted successfully.
 */
libcache_ret_t  libcache_delete_entry(void * libcache, void* entry)
{
    libcache_t* libcache_ptr = (libcache_t*)libcache;
    if (NULL == libcache_ptr) {
        DEBUG_ERROR("input parameter %s is null", "libcache");
        return LIBCACHE_FAILURE;
    }

    libcache_ret_t return_value = LIBCACHE_FAILURE;


    do {
        // Note: judge whether entry is existed in cache
        node_t* libcache_node = pool_get_reserved_pointer(entry);
        if (libcache_node == NULL) {
            return_value = LIBCACHE_NOT_FOUND;
            break;
        }

        // Note: judge whether entry is locked
        libcache_node_usr_data_t* libcache_node_usr_data = (libcache_node_usr_data_t*)libcache_node->usr_data;
         if (libcache_node_usr_data->lock_counter > 0) {
             return_value = LIBCACHE_LOCKED;
             break;
         }

        hash_data_t* hash_data = (hash_data_t*) (libcache_node_usr_data->hash_node_ptr->usr_data);
        return_value = libcache_delete_by_key(libcache_ptr, hash_data->key);
    } while(0);

    return return_value;
}

/*
 *  @brief libcache_unlock_entry    attempts to unlock an entry.
 *
 *  @param libcache                 cache object, cannot be NULL.
 *  @param entry                    entry (returned by libcache_lookup/libcache_add) in the cache.
 *  @return
 *          LIBCACHE_UNLOCKED       the entry is already unlocked,
 *                                  this indicates unpaired locking & unlocking issue happened.
 *          LIBCACHE_SUCCESS        the entry was unlocked successfully once.
 */
libcache_ret_t libcache_unlock_entry(void * libcache, void* entry)
{
    libcache_t* libcache_ptr = (libcache_t*)libcache;
    if (NULL == libcache_ptr) {
        DEBUG_ERROR("input parameter %s is null", "libcache");
        return LIBCACHE_FAILURE;
    }

    if (entry == NULL) {
        DEBUG_ERROR("input parameter %s is null", "entry");
        return LIBCACHE_FAILURE;
    }

    libcache_ret_t return_value = LIBCACHE_FAILURE;

    node_t* libcache_node = pool_get_reserved_pointer(entry);

    if (NULL == libcache_node) {
        return_value = LIBCACHE_NOT_FOUND;
    } else {
        // Note: unlock entry
        libcache_node_usr_data_t* libcache_node_usr_data = (libcache_node_usr_data_t*)libcache_node->usr_data;
        if (libcache_node_usr_data->lock_counter == 0) {
            return_value = LIBCACHE_UNLOCKED;
        } else {
            libcache_node_usr_data->lock_counter--;
            return_value = LIBCACHE_SUCCESS;
        }
    }

    return return_value;
}

/*
 *  @brief libcache_get_max_entry_number    gets a capacity of the maximum number of entries this cache can store.
 *
 *  @param libcache                         cache object, cannot be NULL.
 *  @return
 *         the number
 */
libcache_scale_t libcache_get_max_entry_number(const void * libcache)
{
    const libcache_t* libcache_ptr = (const libcache_t*)libcache;
    if (NULL == libcache_ptr) {
        DEBUG_ERROR("input parameter %s is null", "libcache");
        return LIBCACHE_FAILURE;
    }
    return libcache_ptr->max_entry_number;
}

/*
 *  @brief libcache_get_entry_number         gets the number of entries this cache stores.
 *
 *  @param libcache                          cache object, cannot be NULL.
 *  @return
 *         the number
 */
libcache_scale_t libcache_get_entry_number(const void * libcache)
{
    const libcache_t* libcache_ptr = (const libcache_t*)libcache;
    if (NULL == libcache_ptr) {
        DEBUG_ERROR("input parameter %s is null", "libcache");
        return LIBCACHE_FAILURE;
    }

    return hash_get_count(libcache_ptr->hash_table);
}

/*
 *  @brief libcache_clean         attempts to delete all entries.
 *
 *  @param libcache               cache object, cannot be NULL.
 *  @return
 *      LIBCACHE_LOCKED           this operation aborted while some entries were locked.
 *      LIBCACHE_SUCCESS          all entries were deleted successfully,
 *                                now the cache is empty as fresh as just created.
 */
libcache_ret_t libcache_clean(void * libcache)
{
    libcache_t* libcache_ptr = (libcache_t*)libcache;
    if (NULL == libcache_ptr) {
        DEBUG_ERROR("input parameter %s is null", "libcache");
        return LIBCACHE_FAILURE;
    }

    node_t* libcache_node = NULL;
    while (NULL != (libcache_node = list_pop_front(libcache_ptr->list))) {
        libcache_node_usr_data_t* libcache_node_usr_data = (libcache_node_usr_data_t*)libcache_node->usr_data;

        // TODO: void hash_clean(void* hash)

        return_t ret = pool_free_element(libcache_ptr->pool, POOL_TYPE_DATA, libcache_node_usr_data->pool_element_ptr);
        if (ret != OK) {
            DEBUG_ERROR("pool_free_element failed, ret = %d", ret);
            return LIBCACHE_FAILURE;
        }

        // Note: remove node of list
        free(libcache_node_usr_data);
        free(libcache_node);
        libcache_node = NULL;
    }

    return LIBCACHE_SUCCESS;
}

/*
 *  @brief libcache_destroy         attempts to free all entries, then destroy this cache.
 *
 *  @param libcache                 cache object, cannot be NULL.
 *  @return
 *      LIBCACHE_LOCKED             this operation aborted while some entries were locked.
 *      LIBCACHE_SUCCESS            all entries were deleted successfully, then cache was also destroyed after that.
 */
libcache_ret_t libcache_destroy(void * libcache)
{
    libcache_t* libcache_ptr = (libcache_t*)libcache;
    if (NULL == libcache_ptr) {
        DEBUG_ERROR("input parameter %s is null", "libcache");
        return LIBCACHE_FAILURE;
    }

    node_t* libcache_node = NULL;
    while (NULL != (libcache_node = list_pop_front(libcache_ptr->list))) {
        libcache_node_usr_data_t* libcache_node_usr_data = (libcache_node_usr_data_t*)libcache_node->usr_data;

        if (libcache_ptr->free_entry != NULL) {
            hash_data_t* hash_data = (hash_data_t*) (libcache_node_usr_data->hash_node_ptr->usr_data);
            libcache_ptr->free_entry(hash_data->key, libcache_node_usr_data->pool_element_ptr);
        }

        // Note: remove node of list
        free(libcache_node_usr_data);
        free(libcache_node);
        libcache_node = NULL;
    }
    // TODO: void hash_destroy(void* hash)

    libcache_ptr->free_memory(libcache_ptr->pool);

    hash_destroy(libcache_ptr->hash_table);
    free(libcache_ptr->list);
    free(libcache_ptr);

    return LIBCACHE_SUCCESS;
}
