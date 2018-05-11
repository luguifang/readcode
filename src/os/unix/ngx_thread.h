
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_THREAD_H_INCLUDED_
#define _NGX_THREAD_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#if (NGX_THREADS)

#define NGX_MAX_THREADS      128

#if (NGX_USE_RFORK)
#include <ngx_freebsd_rfork_thread.h>


#else /* use pthreads */

#include <pthread.h>

typedef pthread_t                    ngx_tid_t;

#define ngx_thread_self()            pthread_self()
#define ngx_log_tid                  (int) ngx_thread_self()

#if (NGX_FREEBSD) && !(NGX_LINUXTHREADS)
#define NGX_TID_T_FMT                "%p"
#else
#define NGX_TID_T_FMT                "%d"
#endif


typedef pthread_key_t                ngx_tls_key_t;

#define ngx_thread_key_create(key)   pthread_key_create(key, NULL)
#define ngx_thread_key_create_n      "pthread_key_create()"
#define ngx_thread_set_tls           pthread_setspecific
#define ngx_thread_set_tls_n         "pthread_setspecific()"
#define ngx_thread_get_tls           pthread_getspecific


#define NGX_MUTEX_LIGHT     0

typedef struct {
    pthread_mutex_t   mutex;
    ngx_log_t        *log;
} ngx_mutex_t;

typedef struct {
    pthread_cond_t    cond;
    ngx_log_t        *log;
} ngx_cond_t;

#define ngx_thread_sigmask     pthread_sigmask
#define ngx_thread_sigmask_n  "pthread_sigmask()"

#define ngx_thread_join(t, p)  pthread_join(t, p)

#define ngx_setthrtitle(n)



ngx_int_t ngx_mutex_trylock(ngx_mutex_t *m);
void ngx_mutex_lock(ngx_mutex_t *m);
void ngx_mutex_unlock(ngx_mutex_t *m);

#endif


#define ngx_thread_volatile   volatile


typedef struct {
    ngx_tid_t    tid;
    ngx_cond_t  *cv;
    ngx_uint_t   state;
} ngx_thread_t;

#define NGX_THREAD_FREE   1
#define NGX_THREAD_BUSY   2
#define NGX_THREAD_EXIT   3
#define NGX_THREAD_DONE   4

extern ngx_int_t              ngx_threads_n;
extern volatile ngx_thread_t  ngx_threads[NGX_MAX_THREADS];


typedef void *  ngx_thread_value_t;

ngx_int_t ngx_init_threads(int n, size_t size, ngx_cycle_t *cycle);
ngx_err_t ngx_create_thread(ngx_tid_t *tid,
    ngx_thread_value_t (*func)(void *arg), void *arg, ngx_log_t *log);

ngx_mutex_t *ngx_mutex_init(ngx_log_t *log, ngx_uint_t flags);
void ngx_mutex_destroy(ngx_mutex_t *m);


ngx_cond_t *ngx_cond_init(ngx_log_t *log);
void ngx_cond_destroy(ngx_cond_t *cv);
ngx_int_t ngx_cond_wait(ngx_cond_t *cv, ngx_mutex_t *m);
ngx_int_t ngx_cond_signal(ngx_cond_t *cv);


#else /* !NGX_THREADS */

#define ngx_thread_volatile

#define ngx_log_tid           0
#define NGX_TID_T_FMT         "%d"

#define ngx_mutex_trylock(m)  NGX_OK
#define ngx_mutex_lock(m)
#define ngx_mutex_unlock(m)

#define ngx_cond_signal(cv)

#define ngx_thread_main()     1

#endif


#endif /* _NGX_THREAD_H_INCLUDED_ */


/*
               linux 线程互斥锁 pthread_mutex_t
 nginx 互斥锁及锁的操作方法 封装 linux 线程互斥锁及操作方法
         
 为了保持多个线程同步 一般需要互斥锁来完成任务 互斥锁的使用过程主要设计到
 pthread_mutex_init，pthread_mutex_destory，pthread_mutex_lock，pthread_mutex_unlock
 这几个函数以完成锁的初始化，锁的销毁，上锁和释放锁操作。
 1 锁的创建
 锁是pthread_mutex_t结构可以使用静态初始化
 pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
 外锁可以用pthread_mutex_init函数动态初始化：
 int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t * attr);
 2 锁的属性
 互斥锁属性可以由pthread_mutexattr_init(pthread_mutexattr_t *mattr);来初始化，
 然后可以调用其他的属性设置方法来设置其属性
 互斥锁的范围：
 可以指定是该进程与其他进程的同步还是同一进程内不同的线程之间的同步。可以设置为PTHREAD_PROCESS_SHARE和PTHREAD_PROCESS_PRIVATE。默认是后者，表示进程内使用锁。可以使用int pthread_mutexattr_setpshared(pthread_mutexattr_t *mattr, int pshared)
 pthread_mutexattr_getshared(pthread_mutexattr_t *mattr,int *pshared)用来设置与获取锁的范围；
 互斥锁的类型：
 PTHREAD_MUTEX_TIMED_NP：这是缺省值，也就是普通锁。当一个线程加锁以后，其余请求锁的线程将形成一个等待队列，
 并在解锁后按优先级获得锁。这种锁策略保证了资源分配的公平性。
 PTHREAD_MUTEX_RECURSIVE_NP：嵌套锁，允许同一个线程对同一个锁成功获得多次，并通过多次unlock解锁。
 如果是不同线程请求，则在加锁线程解锁时重新竞争
 PTHREAD_MUTEX_ERRORCHECK_NP:检错锁，如果同一个线程请求同一个锁，则返回EDEADLK，
 否则与PTHREAD_MUTEX_TIMED_NP类型动作相同。这样就保证当不允许多次加锁时不会出现最简单情况下的死锁
 PTHREAD_MUTEX_ADAPTIVE_NP:适应锁，动作最简单的锁类型，仅等待解锁后重新竞争

可以用pthread_mutexattr_settype(pthread_mutexattr_t *attr , int type)
pthread_mutexattr_gettype(pthread_mutexattr_t *attr , int *type)获取或设置锁的类型
 

3 锁的释放
调用pthread_mutex_destory之后，可以释放锁占用的资源，但这有一个前提上锁当前是没有被锁的状态

4 锁操作
对锁的操作主要包括加锁 pthread_mutex_lock()、解锁pthread_mutex_unlock()和测试加锁 pthread_mutex_trylock()三个。
int pthread_mutex_lock(pthread_mutex_t *mutex)

int pthread_mutex_unlock(pthread_mutex_t *mutex)

int pthread_mutex_trylock(pthread_mutex_t *mutex)

pthread_mutex_trylock()语义与pthread_mutex_lock()类似，不同的是在锁已经被占据时返回EBUSY而不是挂起等待


*/




