#include "util_type.h"
#include "util_os.h"
#include "mempool.h"

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
	list_head_t free_list[64];
	ulong count[64];
#ifdef UNIV_DEBUG
	ulong free_count[64];
#endif
	//mem_area_t*  free_list[64];
};

mem_pool_t* mem_comm_pool = NULL;

static inline void mem_area_get_size(mem_area_t* area){
	return (area->size_and_free & ~MEM_AREA_FREE);
}

static inline void mem_area_set_size(mem_area_t* area, ulong size){
	area->size_and_free = (area->size_and_free & MEM_AREA_FREE) | size;
}
//@return TRUE if free 
//we can use the last bit for every block size alloc in 2 exp
static inline void mem_area_get_free(mem_area_t* area){
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
	for(i = 0; i < 64; i++){
		list_init(&pool->free_list[i]);
		pool->count[i] = 0;
#ifdef UNIV_DEBUG
		pool->free_count[i] = 0;
#endif
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
		
		list_add(&area->free_list, &pool->free_list[i]);
		//mem_area_list_add(pool->free_list[i], area);
		pool->count[i]++;
#ifdef UNIV_DEBUG
		pool->free_count[i]++;
#endif
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
	
	assert(mutex_own(&pool->mutex));
	
	if(n > 63)
		return FALSE;
	//try to get a larger block
	area = 	list_entry(&pool->free_list[n + 1], mem_area_t, free_list);
	//area = pool->free_list[n + 1];
	if (area == NULL) {
		assert(list_count(&pool->free_list[n + 1]) == pool->count[n + 1]);//
		if( pool->count[n + 1] > 0 ){
			//error
		}
		ret = mem_pool_fill_free_list(i + 1, pool);
		
		if(ret == FALSE)
			return FALSE;
			
		area = list_entry(&pool->free_list[n + 1], mem_area_t, free_list);
		//area = pool->free_list[n + 1];
	}
	
	if ( pool->count[n + 1]) == 0){
		//error
	}

	list_del_init(&area->free_list, &pool->free_list[n + 1]);
	pool->count[n + 1]--;
#ifdef UNIV_DEBUG
	pool->free_count[n + 1]--;
#endif

	//new area
	area2 = (mem_area_t*)((byte*)area + ut_2_exp(n));
	ut_memset((void*)area2, MEM_AREA_EXTRA_SIZE);
	mem_area_set_size(area2, ut_2_exp(n));
	mem_area_set_free(area2, TRUE);
	list_add(&area2->free_list, &pool->free_list[n]);
	pool->count[n]++;
#ifdef UNIV_DEBUG
	pool->free_count[n]++;
#endif
	//mem_area_list_add(pool->free_list[n], area2);
	
	//rest area
	mem_area_set_size(area, ut_2_exp(n));
	list_add(&area->free_list, &pool->free_list[n]);
	pool->count[n]++;
#ifdef UNIV_DEBUG
	pool->free_count[n]++;
#endif
	//mem_area_list_add(pool->free_list[n], area);
}

void* mem_area_alloc(ulong* psize, mem_pool_t *pool){
	mem_area_t* area;
	ulong size, n;
	int ret;
	
	size = *psize;
	n = ut_2_log(ut_max(size + MEM_AREA_EXTRA_SIZE, MEM_AREA_MIN_SIZE));
	
	mutex_enter(&pool->mutex);
	area = list_entry(&pool->free_list[n], mem_area_t, free_list);
	//area = pool->free_list[n];
	if(area == NULL){
		ret = mem_pool_fill_free(n, pool);
		if(ret == FALSE){//fail to alloc from pool, alloc from os
			mutex_exit(&pool->mutex);
			return ut_malloc(size);
		}
		area = list_entry(&pool->free_list[n], mem_area_t, free_list);
		//area = pool->free_list[n];
	}
	//check if used
	if(!mem_area_get_free(area)){
		//error
	}
	assert(mem_area_get_size(pool) == ut_2_exp(n));
	mem_area_set_free(area,FALSE);
#ifdef UNIV_DEBUG
	pool->free_count[n]--;
#endif
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
	if(byte*)ptr < pool->buf || (byte*)ptr >= pool->buf + pool->size){
		ut_free(ptr);
		return;
	}
	
	area = (mem_area_t*)((byte*)ptr - MEM_AREA_EXTRA_SIZE);
	if(mem_area_get_free(area)){
		//error
	}
	
	size = mem_area_get_size(area);
	if(size == 0){
		//error
	}
	buddy = mem_area_get_buddy(area, size, pool);
	n = ut_2_log(size);
	
	mutex_enter(&pool->mutex);
	if(buddy && mem_area_get_free(buddy) && 
		(size == mem_area_get_size(buddy)){
		//get buddy in free list, merge them at first, then free 
		if((byte*)buddy < (byte*)area){ // buddy in front 
			new_ptr = ((byte*)buddy) + MEM_AREA_EXTRA_SIZE;
			mem_area_set_size(new_ptr, size * 2);
			mem_area_set_free(new_ptr, FALSE);
		}else { //buddy after
			new_ptr = ptr;
			mem_area_set_size(new_ptr, size * 2);
		}
		
		list_del_init(&buddy->free_list);
		pool->count[n]--;
#ifdef UNIV_DEBUG
		pool->free_count[n]--;
#endif
		pool->reserved += ut_2_exp(n);//why not using size?
		mutex_exit(&pool->mutex);//be careful
		
		mem_area_free(new_ptr, pool);
		return;
	}else{
		//just free
		list_add(&area->free_list,&pool->free_list[n]);
		pool->count[n]++;
#ifdef UNIV_DEBUG
		pool->free_count[n]++;
#endif
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

#define MiB 1048576
void mem_pool_print_info(mem_pool_t* pool){
	int i;

	mutex_enter(&pool->mutex);
	printf("------------------------------------------------\n");
	printf("Mempool:total size %lu; used size %lu; \n",pool->size / MiB, pool->reserved / MiB );
	for(i = 0; i < 64; i++){
		if( pool->count[i] > 0 ){
			printf("Memarea %d:area size %lu; total count %lu; ", i, ut_2_exp(i) / MiB, pool->count[i] / MiB );
#ifdef UNIV_DEBUG
			printf("free count %lu; used count %lu;",pool->free_count[i] / MiB, (pool->count[i] - pool->free_count[i]) / MiB);
#endif
			printf("\n");
		}
	}
	printf("------------------------------------------------\n");
	mutex_exit(&pool->mutex);
}