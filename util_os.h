#ifndef UTIL_OS_H
#define UTIL_OS_H

#include<util_type.h>
#include <sys/time.h>
#include <pthread.h>
//debug
#ifdef UNIV_DEBUG
//for debug state
#define ut_d(EXPR)	do {EXPR;} while (0)
//for debug assert
#else
#define ut_d(EXPR)
#endif

//time
int ut_usectime(ulong* ms, ulong* sec);
//mutex
#define FAST_MUTEX_INIT NULL

typedef struct mutex_struct mutex_t;
struct mutex_struct{
	pthread_mutex_t os_mutex;
	//waiters count
#ifdef UNIV_DEBUG
	pthread_t thread_id;
	const char* mutex_name;
	ulong	count_using;
	ulong	count_os_yield; //count of os_wait
	ulonglong spent_time; //os_wait timer msec
	ulonglong max_spent_time;
#endif	
};
void mutex_create(
	mutex_t* mutex
#ifdef UNIV_DEBUG
	,const char* cmutex_name
#endif
);
void mutex_destroy(mutex_t* mutex);
void mutex_enter(mutex_t* mutex);
void mutex_exit(mutex_t* mutex);

//mem
void ut_memset(byte* ptr, ulong n);
void* ut_malloc_low(ulong n, int set_to_zero);
void* ut_malloc(ulong n);
void ut_free(void *ptr);

#endif