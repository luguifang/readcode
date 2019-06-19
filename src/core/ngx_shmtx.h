
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SHMTX_H_INCLUDED_
#define _NGX_SHMTX_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct {
#if (NGX_HAVE_ATOMIC_OPS)
    ngx_atomic_t  *lock;	//原子变量锁
#if (NGX_HAVE_POSIX_SEM)
    ngx_uint_t     semaphore; //semaphore为1时表示获取锁将可能使用到的信号量
    sem_t          sem;	//sem就是信号量锁
#endif
#else
    ngx_fd_t       fd;	//使用文件锁时fd表示使用的文件句柄
    u_char        *name;	//name表示文件名
#endif
    ngx_uint_t     spin;	//自旋次数，表示在自旋状态下等待其他处理器执行结果中释放锁的时间。由文件锁实现时，spin没有任何意义
} ngx_shmtx_t;




/*通过封装文件锁和原子操作实现的5个高层次的互斥锁操作方法--------lgf6.8*/

ngx_int_t ngx_shmtx_create(ngx_shmtx_t *mtx, void *addr, u_char *name);
/*初始化互斥锁：
  参数mtx 表示要操作的ngx_shmtx_t 类型的互斥锁; 但互斥锁由原子变量实现时参数addr表示要操作的
  原子变量锁，当互斥锁由文件实现时参数addr没有任何意义。参数name仅当互斥锁由文件实现时才有意义
  它表示文件所在路径和文件名
*/


void ngx_shmtx_destory(ngx_shmtx_t *mtx);
/*销毁互斥锁*/


ngx_uint_t ngx_shmtx_trylock(ngx_shmtx_t *mtx);
/*无阻塞地尝试获取互斥锁，返回1表示获取锁成功，返回0表示获取锁失败*/


void ngx_shmtx_lock(ngx_shmtx_t *mtx);
/*以阻塞进程的方式获取互斥锁，在方法返回时就已经持有互斥锁*/
void ngx_shmtx_unlock(ngx_shmtx_t *mtx);
/*释放互斥锁*/


#endif /* _NGX_SHMTX_H_INCLUDED_ */
