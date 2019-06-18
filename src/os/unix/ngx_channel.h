
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CHANNEL_H_INCLUDED_
#define _NGX_CHANNEL_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>



/*ngx_channel_t结构体是Nginx定义的master父进程与 worker子进程间的消息格式 ----lgf6.3*/

typedef struct {
     ngx_uint_t  command;	//消息命令
     ngx_pid_t   pid;		//发送方的进程id
     ngx_int_t   slot;		//表示发送命令方在ngx_processes进程数组间的序号
     ngx_fd_t    fd;		//通信的套接字句柄
} ngx_channel_t;


ngx_int_t ngx_write_channel(ngx_socket_t s, ngx_channel_t *ch, size_t size,
    ngx_log_t *log);
ngx_int_t ngx_read_channel(ngx_socket_t s, ngx_channel_t *ch, size_t size,
    ngx_log_t *log);
 /*使用ngx_add_channel_event方法把接收频道消息的套接字添加到epoll中了，当接收到父进程消息时子进程会通过epoll的
事件回调相应的handler方法来处理这个频道消息------lgf*/
ngx_int_t ngx_add_channel_event(ngx_cycle_t *cycle, ngx_fd_t fd,
    ngx_int_t event, ngx_event_handler_pt handler);
void ngx_close_channel(ngx_fd_t *fd, ngx_log_t *log);


#endif /* _NGX_CHANNEL_H_INCLUDED_ */
