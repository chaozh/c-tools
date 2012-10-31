#include<util_os.h>
//time
int ut_usectime(ulong* ms, ulong* sec){
	struct timeeval tv;
	int ret;
	ret = gettimeofday(&tv);
	if(ret != -1){
		*ms = (ulong)tv.tv_ms;
		*sec = (ulong)tv.tv_sec;
	}
	return ret;
}

//mutex
void mutex_create(
	mutex_t* mutex
#ifdef UNIV_DEBUG
	,const char* cmutex_name
#endif
	)
{
	pthread_mutex_init(&mutex->os_mutex,FAST_MUTEX_INIT);
#ifdef UNIV_DEBUG
	mutex->mutex_name = cmutex_name;
	mutex->count_using = 0;
	mutex->count_os_yield = 0;
	mutex->spent_time = 0;
	mutex->max_spent_time = 0;
#endif
}
void mutex_destroy(mutex_t* mutex){
	//assert(mutex->waiters == 0)
	pthread_mutex_destroy(&mutex);
}
void mutex_enter(mutex_t* mutex){
	int ret;
	ut_d(mutex->count_using++);
	ret = pthread_mutex_trylock(&mutex->os_mutex);
	if(ret == 0){
		//succeed
#ifdef UNIV_DEBUG
		ut_d(mutex->thread_id = pthread_self());
#endif
		return;
	}else{
		//spin wait
#ifdef UNIV_DEBUG
		ulonglong start_time, finish_time, diff_time;
		ulong ms, sec;
		mutex->count_os_yield++;
		//deal with time
		ut_usectime(&ms, &sec);
		start_time= (ulonglong)sec * 1000000 + ms;
#endif
		pthread_mutex_lock(&mutex->os_mutex);
#ifdef UNIV_DEBUG
		//deal with time
		ut_usectime(&ms, &sec);
		finish_time= (ulonglong)sec * 1000000 + ms;
		diff_time = finish_time - start_time;
		mutex->spent_time += diff_time;
		if( diff_time > mutex->max_spent_time )
			mutex->max_spent_time = diff_time;
#endif
	}
}
void mutex_exit(mutex_t* mutex){
#ifdef UNIV_DEBUG
	ut_d(mutex->thread_id = (pthred_t_id)NULL);
#endif
	pthread_mutex_unlock(&mutex->os_mutex);
}

//mem
void ut_memset(byte* ptr, ulong n){
	memset(ptr, '\0', n);
}
void* ut_malloc_low(ulong n, int set_to_zero){
	void* ret;
	ret = malloc(n);
	assert(ret); //
	if(set_to_zero)
		memset(ret, '\0', n);
	return ret;
}
void* ut_malloc(ulong n){
	return ut_malloc_low(n, FALSE)
}
void ut_free(void *ptr){
	if(ptr == NULL)
		return;
	else
	    free(ptr);
		return;
}