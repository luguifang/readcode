
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


#if (NGX_HAVE_ATOMIC_OPS)


ngx_int_t
ngx_shmtx_create(ngx_shmtx_t *mtx, void *addr, u_char *name)
{
    mtx->lock = addr;

	/*注意，当spin值为-1时，表示不能使用信号量，这时直接返回成功*/
    if (mtx->spin == (ngx_uint_t) -1) {
        return NGX_OK;
    }

    mtx->spin = 2048;	//spin值默认为2048

//是否同时使用信号量
#if (NGX_HAVE_POSIX_SEM)

	/*信号量sem初始化为0， 用于进程间的通信----lgf6.5*/
    if (sem_init(&mtx->sem, 1, 0) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                      "sem_init() failed");
    } else {
        mtx->semaphore = 1;// 在信号量初始化成功后，设置semaphore标志位为1
    }

#endif

    return NGX_OK;
}


void
ngx_shmtx_destory(ngx_shmtx_t *mtx)
{
#if (NGX_HAVE_POSIX_SEM)
	/*当这把锁的spin值不为(ngx_uint_t) -1时，且初始化信号量成功，semaphore标志位才为1*/
    if (mtx->semaphore) {
        if (sem_destroy(&mtx->sem) == -1) { //销毁信号量
            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                          "sem_destroy() failed");
        }
    }

#endif
}


ngx_uint_t
ngx_shmtx_trylock(ngx_shmtx_t *mtx)
{
    ngx_atomic_uint_t  val;
	/*取出lock锁的值，通过判断它是否为非负数来确定锁状态*/
    val = *mtx->lock;
	/*如果val为0或者正数，则说明没有进程持有锁，这时调用ngx_atomic_cmp_set方法将lock锁改为负数，表示当前进程持有了互斥锁*/
    return ((val & 0x80000000) == 0
            && ngx_atomic_cmp_set(mtx->lock, val, val | 0x80000000));
}



/*给信号量加锁*/

void
ngx_shmtx_lock(ngx_shmtx_t *mtx)
{
    ngx_uint_t         i, n;
    ngx_atomic_uint_t  val;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, "shmtx lock");
	//没有拿到锁之前是不会跳出循环的
    for ( ;; ) {
		/*lock值是当前的锁状态。注意，lock一般是在共享内存中的，它可能会时刻变化，而
	val是当前进程的栈中变量，下面代码的执行中它可能与lock值不一致*/
        val = *mtx->lock;
		/*如果val为非负数，则说明锁未被持有。下面试图通过修改lock值为负数来持有锁*/
        if ((val & 0x80000000) == 0
            && ngx_atomic_cmp_set(mtx->lock, val, val | 0x80000000))
        {
        	/*在成功地将lock值由原先的val改为非负数后，表示成功地持有了锁，ngx_shmtx_lock方法结束*/
            return;
        }
		/*仅在多处理器状态下spin值才有意义，否则PAUSE指令是不会执行的*/
        if (ngx_ncpu > 1) {
			// 循环执行PAUSE，检查锁是否已经释放
            for (n = 1; n < mtx->spin; n <<= 1) {
				/*随着长时间没有获得到锁，将会执行更多次PAUSE才会检查锁*/
                for (i = 0; i < n; i++) {
                    ngx_cpu_pause();	// 对于多处理器系统，执行ngx_cpu_pause可以降低功耗
                }

                val = *mtx->lock;

                if ((val & 0x80000000) == 0
                    && ngx_atomic_cmp_set(mtx->lock, val, val | 0x80000000))
                {
                    return;
                }
            }
        }
//支持信号量时才继续执行
#if (NGX_HAVE_POSIX_SEM)
		// semaphore标志位为1才使用信号量
        if (mtx->semaphore) {
            val = *mtx->lock;	// 重新获取一次可能在共享内存中的lock原子变量

			/*如果lock值为负数，则lock值加上1*/
            if ((val & 0x80000000)
                && ngx_atomic_cmp_set(mtx->lock, val, val + 1))
            {
            	/*检查信号量sem的值，如果sem值为正数，则
				sem值减1，表示拿到了信号量互斥锁，同时
				sem_wait方法返回0。如果sem值为
				0或者负数，则当前进程进入睡眠状态，等待其他进程使用
				ngx_shmtx_unlock方法释放锁（等待
				sem信号量变为正数），到时
				Linux内核会重新调度当前进程，继续检查
				sem值是否为正，重复以上流程*/
                ngx_log_debug1(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                               "shmtx wait %XA", val);

                while (sem_wait(&mtx->sem) == -1) {
                    ngx_err_t  err;

                    err = ngx_errno;

                    if (err != NGX_EINTR) {	// 当EINTR信号出现时，表示sem_wait只是被打断，并不是出错
                        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, err,
                                   "sem_wait() failed while waiting on shmtx");
                        break;
                    }
                }

                ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                               "shmtx awoke");
            }

            continue;
        }

#endif

        ngx_sched_yield();
    }
}


void
ngx_shmtx_unlock(ngx_shmtx_t *mtx)
{
    ngx_atomic_uint_t  val, old, wait;

    if (mtx->spin != (ngx_uint_t) -1) {
        ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, "shmtx unlock");
    }
	//试图循环重置lock值为正数，此时务必将互斥锁释放
    for ( ;; ) {
		
        old = *mtx->lock;	//由共享内存中的lock原子变量取出锁状态
        wait = old & 0x7fffffff;	// 通过把最高位置为0，将lock变为正数
        val = wait ? wait - 1 : 0;	//如果变为正数的lock不是0，则减去1
		// 将lock锁的值设为非负数val
        if (ngx_atomic_cmp_set(mtx->lock, old, val)) {
            break;
        }
    }

#if (NGX_HAVE_POSIX_SEM)
	/*如果lock锁原先的值为0，也就是说，并没有让某个进程持有锁，这时直接返回；或者，
	semaphore标志位为0，表示不需要使用信号量，也立即返回*/
    if (wait == 0 || !mtx->semaphore) {
        return;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                   "shmtx wake %XA", old);
	/*通过sem_post将信号量sem加1，表示当前进程释放了信号量互斥锁，通知其他进程的sem_wait继续执行*/
    if (sem_post(&mtx->sem) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                      "sem_post() failed while wake shmtx");
    }

#endif
}


#else

/*ngx_shmtx_create方法需要确保ngx_shmtx_t结构体中的fd是可用的，它的成功执行是使用互斥锁的先决条件*/
ngx_int_t
ngx_shmtx_create(ngx_shmtx_t *mtx, void *addr, u_char *name)
{
    if (mtx->name) {

        if (ngx_strcmp(name, mtx->name) == 0) {
            mtx->name = name;
            return NGX_OK;
        }

        ngx_shmtx_destory(mtx);
    }
	/*按照name指定的路径创建并打开这个文件-----lgf6.8*/
    mtx->fd = ngx_open_file(name, NGX_FILE_RDWR, NGX_FILE_CREATE_OR_OPEN,
                            NGX_FILE_DEFAULT_ACCESS);

    if (mtx->fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_EMERG, ngx_cycle->log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", name);
        return NGX_ERROR;
    }
	/*由于只需要这个文件在内核中的INODE信息，所以可以把文件删除，只要fd可用就行*/
    if (ngx_delete_file(name) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                      ngx_delete_file_n " \"%s\" failed", name);
    }

    mtx->name = name;

    return NGX_OK;
}


void
ngx_shmtx_destory(ngx_shmtx_t *mtx)
{
	/*关闭ngx_shmtx_t结构体中的fd句柄 ----lgf6.8*/
    if (ngx_close_file(mtx->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", mtx->name);
    }
}


ngx_uint_t
ngx_shmtx_trylock(ngx_shmtx_t *mtx)
{
    ngx_err_t  err;
	/*ngx_trylock_fd方法实现非阻塞互斥锁的获取*/
    err = ngx_trylock_fd(mtx->fd);

    if (err == 0) {
        return 1;
    }

    if (err == NGX_EAGAIN) {
        return 0;
    }

#if __osf__ /* Tru64 UNIX */

    if (err == NGX_EACCESS) {
        return 0;
    }

#endif

    ngx_log_abort(err, ngx_trylock_fd_n " %s failed", mtx->name);

    return 0;
}


void
ngx_shmtx_lock(ngx_shmtx_t *mtx)
{
    ngx_err_t  err;
	/*ngx_lock_fd方法返回0时表示成功地持有锁，返回-1时表示出现错误*/
    err = ngx_lock_fd(mtx->fd);

    if (err == 0) {
        return;
    }

    ngx_log_abort(err, ngx_lock_fd_n " %s failed", mtx->name);
}


void
ngx_shmtx_unlock(ngx_shmtx_t *mtx)
{
    ngx_err_t  err;

    err = ngx_unlock_fd(mtx->fd);

    if (err == 0) {
        return;
    }

    ngx_log_abort(err, ngx_unlock_fd_n " %s failed", mtx->name);
}

#endif
