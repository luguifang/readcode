
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_PALLOC_H_INCLUDED_
#define _NGX_PALLOC_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * NGX_MAX_ALLOC_FROM_POOL should be (ngx_pagesize - 1), i.e. 4095 on x86.
 * On Windows NT it decreases a number of locked pages in a kernel.
 */
#define NGX_MAX_ALLOC_FROM_POOL  (ngx_pagesize - 1)

#define NGX_DEFAULT_POOL_SIZE    (16 * 1024)

#define NGX_POOL_ALIGNMENT       16
#define NGX_MIN_POOL_SIZE                                                     \
    ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t)),            \
              NGX_POOL_ALIGNMENT)


typedef void (*ngx_pool_cleanup_pt)(void *data);

typedef struct ngx_pool_cleanup_s  ngx_pool_cleanup_t;

struct ngx_pool_cleanup_s {
    ngx_pool_cleanup_pt   handler;
    void                 *data;/*ngx_pool_cleanup_add 方法的size>0 时data 不为NULL，此时改写data指向的内存 用于为handler 指向的方法传递必要的参数*/
    ngx_pool_cleanup_t   *next;//ngx_pool_cleanup_add  方法设置next 成员 用于将当前ngx_pool_cleanup_t 添加到ngx_pool_t的cleanup 链表中
};


typedef struct ngx_pool_large_s  ngx_pool_large_t;

struct ngx_pool_large_s {
    ngx_pool_large_t     *next;
    void                 *alloc;
};


typedef struct {
    u_char               *last; //未分配的空闲内存首地址
    u_char               *end;// 小块内存池尾部
    ngx_pool_t           *next;
    ngx_uint_t            failed; //剩余空间不足时 failed ++ failed 大于4 后 ngx__pool_s 的current 将移动至下一个小块内存池
} ngx_pool_data_t;


struct ngx_pool_s {
    ngx_pool_data_t       d; //描述小块内存，当分配小块内存时剩余的预分配空间不足时再分配一个ngx_pool_t 会通过d中next成员构成单链表
    size_t                max;//大小内存的分配界限
    ngx_pool_t           *current;// 多个小块内存池构成链表时 current指向分配内存时遍历的第一个小块内存池
    ngx_chain_t          *chain;
    ngx_pool_large_t     *large;//大块内存直接在进程的堆中分配，为了能够在销毁内存池时同时释放大块内存就把每一次分配的大块内存通过ngx_pool_large_t 组成单链表挂在large成员上
    ngx_pool_cleanup_t   *cleanup;//所有待清理的资源以 ngx_pool_cleanup_t 对象构成单链表挂在 cleanup成员上
    ngx_log_t            *log;// 内存池执行中输出日志的对象
};


typedef struct {
    ngx_fd_t              fd;
    u_char               *name;
    ngx_log_t            *log;
} ngx_pool_cleanup_file_t;


void *ngx_alloc(size_t size, ngx_log_t *log);
void *ngx_calloc(size_t size, ngx_log_t *log);

ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log);
void ngx_destroy_pool(ngx_pool_t *pool);
void ngx_reset_pool(ngx_pool_t *pool);

void *ngx_palloc(ngx_pool_t *pool, size_t size);
void *ngx_pnalloc(ngx_pool_t *pool, size_t size);
void *ngx_pcalloc(ngx_pool_t *pool, size_t size);
void *ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment);
ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p);


ngx_pool_cleanup_t *ngx_pool_cleanup_add(ngx_pool_t *p, size_t size);
void ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd);
void ngx_pool_cleanup_file(void *data);
void ngx_pool_delete_file(void *data);


#endif /* _NGX_PALLOC_H_INCLUDED_ */
