
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SHMEM_H_INCLUDED_
#define _NGX_SHMEM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*--------nginx 共享内存------lgf*/


typedef struct {
    u_char      *addr; //指向共享内存的起始地址
    size_t       size; //共享内存的长度
    ngx_str_t    name; //共享内存的名称
    ngx_log_t   *log; //日志对象
    ngx_uint_t   exists;   /* unsigned  exists:1;  表示共享内存是否已经分配过的标志位，为1时表示已经存在*/
} ngx_shm_t;

/*操作ngx_shm_t结构体的方法有以下两个：ngx_shm_alloc用于分配新的共享内存，而ngx_shm_free用于释放已经存在的共享内存 ------lgf*/
ngx_int_t ngx_shm_alloc(ngx_shm_t *shm);
void ngx_shm_free(ngx_shm_t *shm);


#endif /* _NGX_SHMEM_H_INCLUDED_ */
