#include "stdlib.h"
#include "string.h"
#include "list.h"
#include "pool.h"

element_pool_t pool;

int pool_init(int size)
{
    memset(&pool, '\0', sizeof(pool));
    list_init(pool.free_list);
    list_init(pool.busy_list);

    pool.start_memory = (void*) malloc(size);
    if (pool.start_memory == NULL) {
        return ERR;
    }

    pool.memory_size = size;

    return OK;
}

static int get_index(void *element_addr)
{
    int index = ((char*) element_addr - (char*) pool.start_memory) / pool.element_size;
    return index;
}

int pool_init_element_pool(int entry_size, int entry_count)
{
    while (entry_size % 4 != 0) {
        entry_size++;
    }

    if (entry_size * entry_count > pool.memory_size) {
        return ERR;
    }

    pool.element_link = (node_t*) malloc(sizeof(node_t*) * entry_count);
    memset(pool.element_link, '\0', sizeof(node_t*) * entry_count);

    pool.element_size = entry_size;

    void* addr = pool.start_memory;
    int i;
    for (i = 0; i < entry_count; i++) {
        node_t* node = (node_t*) malloc(sizeof(node_t));
        if (node == NULL) {
            return ERR;
        }

        node->key = NULL;
        node->entry = addr;

        list_push_back(pool.free_list, node);

        int index = get_index(addr);
        node_t *link_node = pool.element_link + index;
        link_node = node;

        addr = (void*) ((char*) addr + entry_size);
    }

    return OK;
}

void* pool_get_element()
{
    node_t *node = list_pop_back(pool.free_list);
    if (node == NULL) {
        list_push_back(pool.busy_list, node);
    }

    return node;
}

int pool_free_element(void* element)
{
    int index = get_index(element);
    node_t *node = pool.element_link + index;
    if (node == NULL) {
        return ERR;
    }

    list_remove(pool.busy_list, node);
    list_push_back(pool.free_list, node);

    return OK;
}
