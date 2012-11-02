#ifndef MEMPOOL_H
#define MEMPOOL_H

#include "list.h"
typedef struct list_head list_head_t;

typedef struct mem_area_struct mem_area_t;
typedef struct mem_pool_struct mem_pool_t;

struct mem_area_struct{
	ulong size_and_free; //may be default
	list_head_t free_list;
	//for buddy oprations a single list can't be used
	//mem_area_t* next; 
};

#define MEM_AREA_EXTRA_SIZE sizeof(struct mem_area_struct)

mem_pool_t* mem_pool_create(ulong size);
void mem_pool_free(mem_pool_t *mem_pool);
void* mem_area_alloc(ulong* psize, mem_pool_t* pool);
void mem_area_free(void* ptr, mem_pool_t* pool);
ulong mem_pool_get_reserved(mem_pool_t *pool);
void mem_pool_print_info(mem_pool_t* pool);

#endif