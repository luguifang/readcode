
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_EVENT_CONNECT_H_INCLUDED_
#define _NGX_EVENT_CONNECT_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#define NGX_PEER_KEEPALIVE           1
#define NGX_PEER_NEXT                2
#define NGX_PEER_FAILED              4


typedef struct ngx_peer_connection_s  ngx_peer_connection_t;

typedef ngx_int_t (*ngx_event_get_peer_pt)(ngx_peer_connection_t *pc,
    void *data);
/*当使用长连接与上游服务器通信时，可通过该方法由连接池中获取一个新连接*/

typedef void (*ngx_event_free_peer_pt)(ngx_peer_connection_t *pc, void *data,
    ngx_uint_t state);
/*当使用长连接与上游服务器通信时，通过该方法将使用完毕的连接*/

#if (NGX_SSL)

typedef ngx_int_t (*ngx_event_set_peer_session_pt)(ngx_peer_connection_t *pc,
    void *data);
typedef void (*ngx_event_save_peer_session_pt)(ngx_peer_connection_t *pc,
    void *data);
#endif


struct ngx_peer_connection_s {
    ngx_connection_t                *connection;
	/*重用ngx_connection_t 部分成员*/

    struct sockaddr                 *sockaddr;
	/*远端服务器的网络地址结构*/
    socklen_t                        socklen;
	/*sockaddr地址长度*/
    ngx_str_t                       *name;
	/*远端服务器的名称*/

    ngx_uint_t                       tries;
	/* 表示在连接一个远端服务器时，当前连接出现异常失败后可以重试的次数，也就是允许的最多失败次数*/

    ngx_event_get_peer_pt            get;
	/*获取连接的方法，如果使用长连接构成的连接池，那么必须要实现get 方法*/
    ngx_event_free_peer_pt           free;
	/* 与get 方法对应的释放连接的方法*/
    void                            *data;
	/*data指针仅用于和上面get、free 方法配合传递参数，他的具体含义与实现get、free方法的模块相关*/

#if (NGX_SSL)
    ngx_event_set_peer_session_pt    set_session;
    ngx_event_save_peer_session_pt   save_session;
#endif

#if (NGX_THREADS)
    ngx_atomic_t                    *lock;
#endif

    ngx_addr_t                      *local;//本机地址信息

    int                              rcvbuf;//套接字接受缓冲区大小

    ngx_log_t                       *log;//日志对象

    unsigned                         cached:1;//标志为1时上面的connection连接已经缓存

                                     /* ngx_connection_log_error_e */
    unsigned                         log_error:2;
	/* 与ngx_connection_t 里的log_error 意义是相同的，区别在于这里的log_error 只有两位，只能表达4 种错误，
	NGX_ERROR_IGNORE_EINVAL 错误无法表达*/
};


ngx_int_t ngx_event_connect_peer(ngx_peer_connection_t *pc);
ngx_int_t ngx_event_get_peer(ngx_peer_connection_t *pc, void *data);


#endif /* _NGX_EVENT_CONNECT_H_INCLUDED_ */
