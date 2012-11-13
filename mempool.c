#include <stdio.h>
#include <stdlib.h>

#include "util_type.h"
#include "util_os.h"
#include "mempool.h"

#define MEM_AREA_COUNT 64
#define MEM_AREA_MIN_SIZE 2 * MEM_AREA_EXTRA_SIZE
#define MEM_AREA_FREE 1


/** Data structure for a memory pool. The space is allocated using the buddy
algorithm, where free list i contains areas of size 2 to power i. declare as
static private*/
struct mem_pool_struct{
	byte* 	buf;
	ulong 	size;
	ulong 	reserved;
	mutex_t mutex;
	list_head_t free_list[MEM_AREA_COUNT];
	ulong count[MEM_AREA_COUNT];
	//mem_area_t*  free_list[MEM_AREA_COUNT];
};

mem_pool_t* mem_comm_pool = NULL;

static inline ulong mem_area_get_size(mem_area_t* area){
	return (area->size_and_free & ~MEM_AREA_FREE);
}

static inline void mem_area_set_size(mem_area_t* area, ulong size){
	area->size_and_free = (area->size_and_free & MEM_AREA_FREE) | size;
}
//@return TRUE if free 
//we can use the last bit for every block size alloc in 2 exp
static inline ibool mem_area_get_free(mem_area_t* area){
	return (area->size_and_free & MEM_AREA_FREE);
}

static inline void mem_area_set_free(mem_area_t* area, int free_flag){
	area->size_and_free = (area->size_and_free & ~MEM_AREA_FREE) | free_flag;
}

mem_pool_t* mem_pool_create(ulong size){
	mem_pool_t* pool;
	mem_area_t* area;
	ulong i, used;
	
	pool = ut_malloc(sizeof(mem_pool_t)); 
	
	//we do not set mem to zero in the pool
	pool->buf = ut_malloc_low(size, FALSE); 
	pool->size = size;
	
	mutex_create(&pool->mutex);
	for(i = 0; i < MEM_AREA_COUNT; i++){
		INIT_LIST_HEAD(&pool->free_list[i]);
		pool->count[i] = 0;
		//pool->free_list[i] = NULL;
	}
	
	used = 0;
	while( size - used >= MEM_AREA_MIN_SIZE ){
		i = ut_2_log(size -used);
		if (ut_2_exp(i) > size -used){
			i--;
		}
		area = (mem_area_t*)(pool->buf + used);
		mem_area_set_size(area, ut_2_exp(i));
		mem_area_set_free(area, TRUE);

		ut_memset((void*)area + MEM_AREA_EXTRA_SIZE, ut_2_exp(i) - MEM_AREA_EXTRA_SIZE);
		
		list_add(&area->free_list, &pool->free_list[i]);
		//mem_area_list_add(pool->free_list[i], area);
		pool->count[i]++;
		used += ut_2_exp(i);
	}
	assert(size >= used);
	
	pool->reserved = 0;
	return pool;
}

void mem_pool_free(mem_pool_t *pool){
	//dont need to release mutex
	ut_free(pool->buf);
	ut_free(pool);
}

static int mem_pool_fill_free_list(ulong n, mem_pool_t* pool){
	mem_area_t* area;
	mem_area_t* area2;
	mem_area_t* tmp;
	int ret;

#ifdef UNIV_DEBUG	
	assert(mutex_own(&pool->mutex));
#endif
	
	if(n >= MEM_AREA_COUNT - 1)
		return FALSE;
	//try to get a larger block
	//area = list_entry(pool->free_list[n + 1].next, mem_area_t, free_list);
	if (list_empty(&pool->free_list[n + 1])) {
		assert(list_count(&pool->free_list[n + 1]) == pool->count[n + 1]);//
		if( pool->count[n + 1] > 0 ){
			//error
		}
		ret = mem_pool_fill_free_list(n + 1, pool);
		
		if(ret == FALSE)
			return FALSE;
			
		//area = list_entry(pool->free_list[n + 1].next, mem_area_t, free_list);
		//area = pool->free_list[n + 1];
	}
	area = list_entry(pool->free_list[n + 1].next, mem_area_t, free_list);
	
	if ( pool->count[n + 1] == 0){
		//error
	}

	list_del_init(&area->free_list);
	pool->count[n + 1]--;

	//new area
	area2 = (mem_area_t*)((byte*)area + ut_2_exp(n));
	ut_memset((void*)area2, MEM_AREA_EXTRA_SIZE);
	mem_area_set_size(area2, ut_2_exp(n));
	mem_area_set_free(area2, TRUE);
	list_add(&area2->free_list, &pool->free_list[n]);
	pool->count[n]++;
	//mem_area_list_add(pool->free_list[n], area2);
	
	//rest area
	mem_area_set_size(area, ut_2_exp(n));
	list_add(&area->free_list, &pool->free_list[n]);
	pool->count[n]++;
	return TRUE;
	//mem_area_list_add(pool->free_list[n], area);
}

void* mem_area_alloc(ulong* psize, mem_pool_t *pool){
	mem_area_t* area;
	ulong size, n;
	int ret;
	
	size = *psize;
	n = ut_2_log(ut_max(size + MEM_AREA_EXTRA_SIZE, MEM_AREA_MIN_SIZE));//origin size plus extra 
	
	mutex_enter(&pool->mutex);
	//area = list_entry(pool->free_list[n].next, mem_area_t, free_list);
	//area = pool->free_list[n];
	if(list_empty(&pool->free_list[n])){
		ret = mem_pool_fill_free_list(n, pool);
		if(ret == FALSE){//fail to alloc from pool, alloc from os
			mutex_exit(&pool->mutex);
			return ut_malloc(size);
		}
		//area = list_entry(pool->free_list[n].next, mem_area_t, free_list);
		//area = pool->free_list[n];
	}
	area = list_entry(pool->free_list[n].next, mem_area_t, free_list);
	//check if used
	if(!mem_area_get_free(area)){
		//error
	}
	assert(mem_area_get_size(area) == ut_2_exp(n));
	mem_area_set_free(area,FALSE);
	list_del_init(&area->free_list);
	pool->count[n]--;

	pool->reserved += mem_area_get_size(area);
	mutex_exit(&pool->mutex);
	//sub struct size
	*psize = ut_2_exp(n) - MEM_AREA_EXTRA_SIZE;
	ut_memset((MEM_AREA_EXTRA_SIZE + (byte*)area), *psize);
	return (void*)(MEM_AREA_EXTRA_SIZE + (byte*)area);
}

static inline mem_area_t* mem_area_get_buddy(mem_area_t* area, ulong size, mem_pool_t* pool){
	mem_area_t* buddy;
	if(((byte*)area - pool->buf) % (2 * size) == 0){
		//buddy is in a higher address
		buddy = (mem_area_t*)((byte*)area + size);
		if(((byte*)buddy - pool->buf) + size > pool->size)
			//buddy is not wholly contained in the pool
			buddy = NULL;
	}else{
		//buddy is in a lower address
		buddy = (mem_area_t*)((byte*)area - size);
	}
	return buddy;
}

void mem_area_free(void* ptr, mem_pool_t* pool){
	mem_area_t* area;
	mem_area_t* buddy;
	void* new_ptr;
	ulong size;
	ulong n;
	//alloc from os
	if( (byte*)ptr < pool->buf || (byte*)ptr >= pool->buf + pool->size ){
		ut_free(ptr);
		return;
	}
	
	area = (mem_area_t*)((byte*)ptr - MEM_AREA_EXTRA_SIZE);
	if(mem_area_get_free(area)){
		//error
	}
	
	size = mem_area_get_size(area);
	ut_memset(ptr, size - MEM_AREA_EXTRA_SIZE);

	if(size == 0){
		//error
	}
	buddy = mem_area_get_buddy(area, size, pool);
	n = ut_2_log(size);
	
	mutex_enter(&pool->mutex);
	if(buddy && mem_area_get_free(buddy) && 
		size == mem_area_get_size(buddy)){
		//get buddy in free list, merge them at first, then free 
		if((byte*)buddy < (byte*)area){ // buddy in front 
			new_ptr = ((byte*)buddy) + MEM_AREA_EXTRA_SIZE;
			mem_area_set_size(buddy, size * 2);
			mem_area_set_free(buddy, FALSE);
		}else { //buddy after
			new_ptr = ptr;
			mem_area_set_size(area, size * 2);
		}
		
		list_del_init(&buddy->free_list);
		pool->count[n]--;
		pool->reserved += ut_2_exp(n);//why not using size?
		mutex_exit(&pool->mutex);//be careful
		
		mem_area_free(new_ptr, pool);
		return;
	}else{
		//just free
		list_add(&area->free_list,&pool->free_list[n]);
		pool->count[n]++;
		//mem_area_list_add(pool->free_list[n], area);
		mem_area_set_free(area, TRUE);
		assert(pool->reserved >= size);
		pool->reserved -= size;
	}
	mutex_exit(&pool->mutex);
	
}

ulong mem_pool_get_reserved(mem_pool_t* pool){
	ulong reserved;
	mutex_enter(&pool->mutex);
	reserved = pool->reserved;
	mutex_exit(&pool->mutex);
	return reserved;
}

static inline int mem_pool_validate(mem_pool_t* pool){
	mem_area_t*	area;
	mem_area_t*	buddy;
	int		free_space;
	int		i;

	mutex_enter(&pool->mutex);
	free_space = 0;
	for(i = 0; i<MEM_AREA_COUNT; i++){
		list_for_each_entry(area, &pool->free_list[i], free_list){
			if(!list_empty(&pool->free_list[i])){
				assert(mem_area_get_free(area));
				assert(mem_area_get_size(area) == ut_2_exp(i));

				buddy = mem_area_get_buddy(area, ut_2_exp(i), pool);

				assert(!buddy || !mem_area_get_free(buddy)
				     || (ut_2_exp(i) != mem_area_get_size(buddy)));

				free_space += ut_2_exp(i);
			}
		}
	}
	assert(free_space + pool->reserved == pool->size);
	mutex_exit(&pool->mutex);
	return 0;
}

#define MiB 1048576
void mem_pool_print_info(mem_pool_t* pool){
	mem_area_t* area;
	int i;

	mem_pool_validate(pool);

	printf("------------------------------------------------\n");
	mutex_enter(&pool->mutex);
	printf("Mempool:total size %lu MB; used size %lu B; \n",pool->size / MiB, pool->reserved );
	for(i = 0; i < MEM_AREA_COUNT; i++){
		if( pool->count[i] > 0 ){
			area = list_entry(pool->free_list[i].next, mem_area_t, free_list);
			printf("Memarea %d:area size %lu/%lu B; free count %lu; ", i, ut_2_exp(i), mem_area_get_size(area), pool->count[i] );
			printf("\n");
		}
	}
	mutex_exit(&pool->mutex);
	printf("------------------------------------------------\n");
}