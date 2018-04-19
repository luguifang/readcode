
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CONNECTION_H_INCLUDED_
#define _NGX_CONNECTION_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_listening_s  ngx_listening_t;

struct ngx_listening_s {
    ngx_socket_t        fd; /* socket套接字句柄*/

    struct sockaddr    *sockaddr; /*监听地址*/
    socklen_t           socklen;    /* size of sockaddr */
    size_t              addr_text_max_len; /* 存储IP地址字符串addr_text最大长度*/
    ngx_str_t           addr_text;

    int                 type; /*套接字类型 tcp/udp*/

    int                 backlog; /*TCP实现监听时的backlog队列，它表示允许正在通过三次握手建立TCP连接但还没有任何进程开始处理的连接最大个数*/
    int                 rcvbuf; /*内核中对于这个套接字的接收缓冲区大小*/
    int                 sndbuf; /*内核中对于这个套接字的发送缓冲区大小*/

    /* handler of accepted connection */
    ngx_connection_handler_pt   handler; /*当新的TCP连接成功建立后的处理方法*/

    void               *servers;  /* servers指针，它更多是作为一个保留指针，目前主要用于HTTP或者mail等模块，用于保存当前监听端口对应着的所有主机名 */

    ngx_log_t           log; /*可用的日志对象的指针*/
    ngx_log_t          *logp; /*可用的日志对象的指针*/

    size_t              pool_size; /*如果为新的TCP连接创建内存池，则内存池的初始大小应该是pool_size*/
    /* should be here because of the AcceptEx() preread */
    size_t              post_accept_buffer_size;
    /* should be here because of the deferred accept */
    ngx_msec_t          post_accept_timeout;/*TCP_DEFER_ACCEPT选项将在建立
											TCP连接成功且接收到用户的请求数据后，才向对监听套接字感兴趣的进程发送事件通知，而连接建立成功后，如果
											post_accept_timeout秒后仍然没有收到的用户数据，则内核直接丢弃连接
											*/

    ngx_listening_t    *previous; /*组成单链表*/
    ngx_connection_t   *connection; /*当前监听句柄对应着的ngx_connection_t结构体*/

    unsigned            open:1;/*标志位，为
								1则表示在当前监听句柄有效，且执行
								ngx_init_cycle时不关闭监听端口，为
								0时则正常关闭。该标志位框架代码会自动设置
								*/
    unsigned            remain:1; /*标志位，为1表示使用已有的ngx_cycle_t来初始化新的
									ngx_cycle_t结构体时，不关闭原先打开的监听端口，这对运行中升级程序很有用，
									remain为0时，表示正常关闭曾经打开的监听端口。该标志位框架代码会自动设置，参见
									ngx_init_cycle方法*/
    unsigned            ignore:1; /*标志位，为1时表示跳过设置当前ngx_listening_t结构体中的套接字，为
									0时正常初始化套接字。该标志位框架代码会自动设置*/

    unsigned            bound:1;       /* already bound 表示是否已经绑定*/
    unsigned            inherited:1;   /* 表示当前监听句柄是否来自前一个进程（如升级Nginx程序），如果为
										1，则表示来自前一个进程。一般会保留之前已经设置好的套接字，不做改变 */
    unsigned            nonblocking_accept:1; /*暂未使用*/
    unsigned            listen:1; /*标志位，为1时表示当前结构体对应的套接字已经监听*/
    unsigned            nonblocking:1; /*表示套接字是否阻塞，目前该标志位没有意义*/
    unsigned            shared:1;    /* shared between threads or processes 目前该标志位没有意义*/
    unsigned            addr_ntop:1; /*标志位，为1时表示Nginx会将网络地址转变为字符串形式的地址*/

#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)
    unsigned            ipv6only:2;
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT)
    unsigned            deferred_accept:1;
    unsigned            delete_deferred:1;
    unsigned            add_deferred:1;
#ifdef SO_ACCEPTFILTER
    char               *accept_filter;
#endif
#endif
#if (NGX_HAVE_SETFIB)
    int                 setfib;
#endif

};


typedef enum {
     NGX_ERROR_ALERT = 0,
     NGX_ERROR_ERR,
     NGX_ERROR_INFO,
     NGX_ERROR_IGNORE_ECONNRESET,
     NGX_ERROR_IGNORE_EINVAL
} ngx_connection_log_error_e;


typedef enum {
     NGX_TCP_NODELAY_UNSET = 0,
     NGX_TCP_NODELAY_SET,
     NGX_TCP_NODELAY_DISABLED
} ngx_connection_tcp_nodelay_e;


typedef enum {
     NGX_TCP_NOPUSH_UNSET = 0,
     NGX_TCP_NOPUSH_SET,
     NGX_TCP_NOPUSH_DISABLED
} ngx_connection_tcp_nopush_e;


#define NGX_LOWLEVEL_BUFFERED  0x0f
#define NGX_SSL_BUFFERED       0x01


struct ngx_connection_s {
    void               *data;
	/*连接未使用时 data成员充当连接池中空闲连接链表中的next指针。当连接被使用时data的意义有使用他的模块决定
	HTTP框架中data指向ngx_http_request_t 请求*/
    ngx_event_t        *read; //读事件
    ngx_event_t        *write; //写事件

    ngx_socket_t        fd; //套接字句柄

    ngx_recv_pt         recv; //直接接受网络流的方法
    ngx_send_pt         send; //直接发送网络字符流方法
    ngx_recv_chain_pt   recv_chain; //以ngx_chain_t 链表为参数来接受网络字符流的方法
    ngx_send_chain_pt   send_chain;//以ngx_chain_t 链表为参数来发送网络字符流的方法

    ngx_listening_t    *listening; //连接对应的ngx_listening_t 监听对象，此连接由listening 监听端口的事件建立

    off_t               sent; //该连接已经发送的字节数

    ngx_log_t          *log;//log 对象

    ngx_pool_t         *pool;
	/*内存池 ，一般在accept 一个新连接时 会创建一个内存池，而在连接结束时销毁内存池
	这里所说的连接是指成功建立的tcp连接 所有的ngx_connection_t 结构体都是预先分配的，这个内存池的大小将由上面的listening 
	监听对象中的poll_size成员决定*/

    struct sockaddr    *sockaddr;//连接客户端的sockaddr结构体
    socklen_t           socklen; //结构体长度
    ngx_str_t           addr_text;// 字符串IP

#if (NGX_SSL)
    ngx_ssl_connection_t  *ssl;
#endif

    struct sockaddr    *local_sockaddr;//本机监听端口对应的sockaddr结构体 也就是listening 监听对象中的sockaddr 成员

    ngx_buf_t          *buffer;//用于接受、缓存客户端发来的字符流，每个事件消费模块可以自由决定从连接池中分配多大的空间给buffer

    ngx_queue_t         queue;
	/*用来将当前连接以双向链表元素的形似和添加到ngx_cycle_t 核心结构体的reusable_connections_queue 双向链表中
    表示可以重用的连接*/

    ngx_atomic_uint_t   number;
	/*连接使用次数 ngx_connection_t 每次建立一条客户端的连接或者主动向后端服务器发起连接 该字段都会+1*/
    ngx_uint_t          requests; //处理的请求次数
	

    unsigned            buffered:8;
	/*缓存中的业务类型 任何事件消费模块都可以自定义需要的标志位，buffered 有8位最多可以表示8中不同的业务
	buffered 的低四位要慎用在实际发送响应的ngx_http_write_filter_module过滤模块中，低4位标志位为1则意味着
	Nginx会一直认为有HTTP模块还需要处理这个请求，必须等待HTTP模块将低4位全置为0才会正常结束请求*/

    unsigned            log_error:3;     /* ngx_connection_log_error_e */
	/*该连接记录日志的级别 占用3位 目前只定义了5个值 由ngx_connection_log_error_e 枚举类型定义*/

    unsigned            single_connection:1;
	/*1：独立连接 从客户端发起的连接 2：依靠其他连接行为而建立起来的非独立连接 upstream 机制向后端服务器建立起来的连接*/
    unsigned            unexpected_eof:1;//值为1 不期待字符流结束 目前无意义
	
    unsigned            timedout:1;//1：表示连接超时
    unsigned            error:1;	//1：连接处理过程出现错误
    unsigned            destroyed:1;
	/*标志位 为1时表示TCP连接已经销毁，ngx_connection_t结构体仍然存在，但其对应的套接字、内存池等已经不可用*/

    unsigned            idle:1;//标志位 1时表示连接处于空闲状态，如keepalive 请求中两次请求之间的状态
    unsigned            reusable:1;//标志位 1时表示连接可重用 和queue配合使用
    unsigned            close:1;//标志位 1 表示连接已经关闭

    unsigned            sendfile:1;//标志位，为1时表示正在将文件数据发往连接的另一端
    unsigned            sndlowat:1;
	/*标志位，如果为1，则表示只有在连接套接字对应的发送缓冲区必须满足最低设置的大小阈值时，事件驱动模块才会分发该事件。
	这与上文介绍过的ngx_handle_write_event方法中的lowat参数是对应的*/
    unsigned            tcp_nodelay:2;   /* ngx_connection_tcp_nodelay_e */
	/*标志位，表示如何使用TCP的 nodelay特性 值为ngx_connection_tcp_nodelay_e定义的枚举类型*/
    unsigned            tcp_nopush:2;    /* ngx_connection_tcp_nopush_e */
	/*标志位，表示如何使用TCP的nopush特性 值在ngx_connection_tcp_nopush_e 枚举中定义*/

#if (NGX_HAVE_IOCP)
    unsigned            accept_context_updated:1;
#endif

#if (NGX_HAVE_AIO_SENDFILE)
    unsigned            aio_sendfile:1;
	/*标志位，为1时表示使用异步I/O的方式将磁盘上文件发送给网络连接的另一端*/
    ngx_buf_t          *busy_sendfile;
	/*使用异步I/O方式发送的文件，busy_sendfile缓冲区保存待发送文件的信息*/
#endif

#if (NGX_THREADS)
    ngx_atomic_t        lock;
#endif
};


ngx_listening_t *ngx_create_listening(ngx_conf_t *cf, void *sockaddr,
    socklen_t socklen);
ngx_int_t ngx_set_inherited_sockets(ngx_cycle_t *cycle);
ngx_int_t ngx_open_listening_sockets(ngx_cycle_t *cycle);
void ngx_configure_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_connection(ngx_connection_t *c);
ngx_int_t ngx_connection_local_sockaddr(ngx_connection_t *c, ngx_str_t *s,
    ngx_uint_t port);
ngx_int_t ngx_connection_error(ngx_connection_t *c, ngx_err_t err, char *text);

ngx_connection_t *ngx_get_connection(ngx_socket_t s, ngx_log_t *log);
void ngx_free_connection(ngx_connection_t *c);

void ngx_reusable_connection(ngx_connection_t *c, ngx_uint_t reusable);

#endif /* _NGX_CONNECTION_H_INCLUDED_ */
