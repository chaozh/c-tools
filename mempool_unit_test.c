#include "util_type.h"

#define UNIV_DEBUG

#include "util_os.h"
#include "mempool.h"

#include <sys/sysinfo.h> 

#define array_length 200

int initial_pool_size = 256;
//for unit test
typedef struct {
    /** Mutex to protect access to the structure */
    pthread_mutex_t mutex;
    /** Name of the cache objects in this cache (provided by the caller) */
    char *name;
    /** List of pointers to available buffers in this cache */
    void **ptr;
    /** The size of each element in this cache */
    size_t bufsize;
    /** The capacity of the list of elements */
    int freetotal;
    /** The current number of free elements */
    int freecurr;
} cache_t;

void cache_init(void){
	struct sysinfo info;
	unsigned long ram_free;
	int user_available_mem;
	
	sysinfo(&info);
	ram_free = info.freeram / 1024000;
	user_available_mem = ram_free / 3;
	
	if(user_available_mem < 256)
		initial_pool_size = 64;
	else if(user_available_mem < 512)
		initial_pool_size = 128;
	else if(user_available_mem < 1024)
		initial_pool_size = 256;
	else
		initial_pool_size = 512;
}

int main(){
	mem_pool_t* mem_pool;
	cache_t* cache_test[array_length];
	ulong len;
	int i;

	cache_init();
	mem_pool = mem_pool_create(initial_pool_size * 1048576);
	mem_pool_print_info(mem_pool);

	len = sizeof(cache_t);
	for(i = 0; i< array_length; i++){
		printf("alloc cache[%d]\n", i);
		cache_test[i] = mem_area_alloc(&len, mem_pool);
		mem_pool_print_info(mem_pool);
	}

	for(i = 0; i< array_length / 2; i++){
		printf("free cache[%d]\n", i);
		mem_area_free(cache_test[i], mem_pool);
		mem_pool_print_info(mem_pool);
	}

	for(i = array_length / 2; i< array_length; i++){
		printf("free cache[%d]\n", i);
		mem_area_free(cache_test[i], mem_pool);
		mem_pool_print_info(mem_pool);
	}
		
	mem_pool_print_info(mem_pool);
	mem_pool_free(mem_pool);
	return 0;
}