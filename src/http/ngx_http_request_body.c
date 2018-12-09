
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static void ngx_http_read_client_request_body_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_do_read_client_request_body(ngx_http_request_t *r);
static ngx_int_t ngx_http_write_request_body(ngx_http_request_t *r,
    ngx_chain_t *body);
static ngx_int_t ngx_http_read_discarded_request_body(ngx_http_request_t *r);
static ngx_int_t ngx_http_test_expect(ngx_http_request_t *r);


/*
 * on completion ngx_http_read_client_request_body() adds to
 * r->request_body->bufs one or two bufs:
 *    *) one memory buf that was preread in r->header_in;
 *    *) one memory or file buf that contains the rest of the body
 */

ngx_int_t
ngx_http_read_client_request_body(ngx_http_request_t *r,
    ngx_http_client_body_handler_pt post_handler)
{
    size_t                     preread;
    ssize_t                    size;
    ngx_buf_t                 *b;
    ngx_chain_t               *cl, **next;
    ngx_temp_file_t           *tf;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;
	/*原始请求的引用计数加1 luguifang*/
    r->main->count++;


	/*检查请求ngx_http_request_t结构体中的request_body成员，如果它已经被分配过了，证明
	已经读取过HTTP包体了，不需要再次读取一遍，再检查请求ngx_http_request_t结构体中的discard_body标志位，如果discard_body为1，则证明曾经执行过
	丢弃包体的方法，现在包体正在被丢弃中luguifang*/
    if (r->request_body || r->discard_body) {
		/*直接执行各HTTP模块提供的post_handler回调方法*/
        post_handler(r);
        return NGX_OK;
    }

    if (ngx_http_test_expect(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
	/*分配请求的ngx_http_request_t结构体中的request_body成员（之前request_body是NULL空指针），准备接收包体luguifang*/
    rb = ngx_pcalloc(r->pool, sizeof(ngx_http_request_body_t));
    if (rb == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->request_body = rb;

    if (r->headers_in.content_length_n < 0) {
        post_handler(r);
        return NGX_OK;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

	/*检查请求的content-length头部，如果指定了包体长度的content-length字段小于或等于
0，当然不用继续接收包体，luguifang*/

    if (r->headers_in.content_length_n == 0) {

        if (r->request_body_in_file_only) {
            tf = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
            if (tf == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            tf->file.fd = NGX_INVALID_FILE;
            tf->file.log = r->connection->log;
            tf->path = clcf->client_body_temp_path;
            tf->pool = r->pool;
            tf->warn = "a client request body is buffered to a temporary file";
            tf->log_level = r->request_body_file_log_level;
            tf->persistent = r->request_body_in_persistent_file;
            tf->clean = r->request_body_in_clean_file;

            if (r->request_body_file_group_access) {
                tf->access = 0660;
            }

            rb->temp_file = tf;

            if (ngx_create_temp_file(&tf->file, tf->path, tf->pool,
                                     tf->persistent, tf->clean, tf->access)
                != NGX_OK)
            {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
        }

        post_handler(r);

        return NGX_OK;
    }
	/*如果content-length大于0，则意味着继续执行，但HTTP模块定义的post_handler方法不会知道在哪一次事件的触发中会被回调，所以先把它
	设置到request_body结构体的post_handler成员中luguifang*/
    rb->post_handler = post_handler;

    /*
     * set by ngx_pcalloc():
     *
     *     rb->bufs = NULL;
     *     rb->buf = NULL;
     *     rb->rest = 0;
     */
	/*接收HTTP头部的流程中，是有可能接收到HTTP包体的，检查在header_in缓冲区中已经接收到的包体长度luguifang*/
	/*
	body:
	
	buf-chains:|--buf--|--buf--|--buf--|--buf-|
		 	0x0001                            0x000f

	buf: |----------data----------|
		 pos     				last
		 start =0x0001
		 end = 0x000f
	
	luguifang*/
    preread = r->header_in->last - r->header_in->pos;

    if (preread) {

        /* there is the pre-read part of the request body */

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http client request body preread %uz", preread);

        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        b->temporary = 1;
        b->start = r->header_in->pos;
        b->pos = r->header_in->pos;
        b->last = r->header_in->last;
        b->end = r->header_in->end;

        rb->bufs = ngx_alloc_chain_link(r->pool);
        if (rb->bufs == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        rb->bufs->buf = b;
        rb->bufs->next = NULL;

        rb->buf = b;
		/*判断接受http头部过程中是否接收到了完整的包体 luguifang*/
        if ((off_t) preread >= r->headers_in.content_length_n) {

            /* the whole request body was pre-read */

            r->header_in->pos += (size_t) r->headers_in.content_length_n;
            r->request_length += r->headers_in.content_length_n;
            b->last = r->header_in->pos;

            if (r->request_body_in_file_only) {
                if (ngx_http_write_request_body(r, rb->bufs) != NGX_OK) {
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }
            }

            post_handler(r);

            return NGX_OK;
        }

        /*
         * to not consider the body as pipelined request in
         * ngx_http_set_keepalive()
         */
        r->header_in->pos = r->header_in->last;

        r->request_length += preread;

        rb->rest = r->headers_in.content_length_n - preread;
		/*检查header_in缓冲区里的剩余空闲空间是否可以存放下全部的包体（content-length头部指定）luguifang*/
        if (rb->rest <= (off_t) (b->end - b->last)) {

            /* the whole request body may be placed in r->header_in */

            rb->to_write = rb->bufs;

            r->read_event_handler = ngx_http_read_client_request_body_handler;

            return ngx_http_do_read_client_request_body(r);
        }
		// header_in 中的剩余内存不够，需要单独分配用于存储包体的内存 luguifang
        next = &rb->bufs->next;

    } else {
        b = NULL;
        rb->rest = r->headers_in.content_length_n;
        next = &rb->bufs;
    }

    size = clcf->client_body_buffer_size;
    size += size >> 2;

    if (rb->rest < size) {
        size = (ssize_t) rb->rest;

        if (r->request_body_in_single_buf) {
            size += preread;
        }

    } else {
        size = clcf->client_body_buffer_size;

        /* disable copying buffer for r->request_body_in_single_buf */
        b = NULL;
    }

    rb->buf = ngx_create_temp_buf(r->pool, size);
    if (rb->buf == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cl->buf = rb->buf;
    cl->next = NULL;

    if (b && r->request_body_in_single_buf) {
        size = b->last - b->pos;
        ngx_memcpy(rb->buf->pos, b->pos, size);
        rb->buf->last += size;

        next = &rb->bufs;
    }

    *next = cl;

    if (r->request_body_in_file_only || r->request_body_in_single_buf) {
        rb->to_write = rb->bufs;

    } else {
        rb->to_write = rb->bufs->next ? rb->bufs->next : rb->bufs;
    }

	/*设置请求ngx_http_request_t结构体的read_event_handler成员为
	ngx_http_read_client_request_body_handler方法，它意味着如果epoll再次检测到可读事件或者
	读事件的定时器超时，HTTP框架将调用ngx_http_read_client_request_body_handler方法处理 ----luguifang*/
    r->read_event_handler = ngx_http_read_client_request_body_handler;

    return ngx_http_do_read_client_request_body(r);
}


static void
ngx_http_read_client_request_body_handler(ngx_http_request_t *r)
{
    ngx_int_t  rc;
	/*首先检查连接上读事件的timeout标志位，如果为1，则表示接收HTTP包体超时，这
	时把连接ngx_connection_t结构体上的timeout标志位也置为1，同时调用
	ngx_http_finalize_request方法结束请求，并发送408超时错误码------luguifang*/
    if (r->connection->read->timedout) {
        r->connection->timedout = 1;
        ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }
	/*调用ngx_http_do_read_client_request_body方法接收包体，检测这个
	方法的返回值，如果它大于300，那么一定表示希望返回错误码*/
    rc = ngx_http_do_read_client_request_body(r);

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        ngx_http_finalize_request(r, rc);
    }
}


static ngx_int_t
ngx_http_do_read_client_request_body(ngx_http_request_t *r)
{
    size_t                     size;
    ssize_t                    n;
    ngx_buf_t                 *b;
    ngx_connection_t          *c;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;
    rb = r->request_body;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http read client request body");

    for ( ;; ) {
        for ( ;; ) {
			/*请求的request_body 的buf 缓冲区是否已满，若满则写入文件*/
            if (rb->buf->last == rb->buf->end) {

                if (ngx_http_write_request_body(r, rb->to_write) != NGX_OK) {
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }
				/*复用缓冲区 将last 赋值为start*/
                rb->to_write = rb->bufs->next ? rb->bufs->next : rb->bufs;
                rb->buf->last = rb->buf->start;
            }

            size = rb->buf->end - rb->buf->last;

            if ((off_t) size > rb->rest) {
                size = (size_t) rb->rest;
            }
			/*调用封装了recv的方法从套接字缓冲区中读取包体到缓冲区中----luguifang*/
            n = c->recv(c, rb->buf->last, size);

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                           "http client request body recv %z", n);

            if (n == NGX_AGAIN) {
                break;
            }

            if (n == 0) {
                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                              "client closed prematurely connection");
            }
			/*recv方法返回错误，或者客户端主动关闭了连接，设置ngx_http_request_t结构体的error标志位为1，同时返回
			NGX_HTTP_BAD_REQUEST错误码*/
            if (n == 0 || n == NGX_ERROR) {
                c->error = 1;
                return NGX_HTTP_BAD_REQUEST;
            }
			/*根据接收到的TCP流长度，修改缓冲区参数----luguifang*/
            rb->buf->last += n;
            rb->rest -= n;
            r->request_length += n;

            if (rb->rest == 0) {
                break;
            }

            if (rb->buf->last < rb->buf->end) {
                break;
            }
        }

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                       "http client request body rest %O", rb->rest);
		//接受到完整包
        if (rb->rest == 0) {
            break;
        }
		/*检查缓冲区是否可读，如果不可读，则将读
		事件添加到事件模块，等待可读事件发生时，事件框架可以再次调度到这个方法接收包体。
		这一步是调用ngx_add_timer方法将读事件添加到定时器中，超时时间以nginx.conf文件中的
		client_body_timeout配置项参数为准*/
        if (!c->read->ready) {
            clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
            ngx_add_timer(c->read, clcf->client_body_timeout);
			/*调用ngx_handle_read_event方法将读事件添加到epoll等事件收集器中，同时
			ngx_http_do_read_client_request_body方法结束，返回NGX_AGAIN。------luguifang*/
            if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            return NGX_AGAIN;
        }
    }
	// 此处以后是接受到完整包体的处理，做收尾工作
	/*检查是否接收HTTP包体超时了，要把读事件从定时器中取出，防止不必要的定时器触发。这一
	步会检查读事件的timer_set标志位，如果为1，则调用ngx_del_timer方法把读事件从定时器中移除luguifang*/
    if (c->read->timer_set) {
        ngx_del_timer(c->read);
    }
	/*如果缓冲区中还有未写入文件的内容，调用ngx_http_write_request_body方法把最后
	的包体内容也写入文件luguifang*//
    if (rb->temp_file || r->request_body_in_file_only) {

        /* save the last part */

        if (ngx_http_write_request_body(r, rb->to_write) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        b->in_file = 1;
        b->file_pos = 0;
        b->file_last = rb->temp_file->file.offset;
        b->file = &rb->temp_file->file;

        if (rb->bufs->next) {
            rb->bufs->next->buf = b;

        } else {
            rb->bufs->buf = b;
        }
    }

    if (rb->bufs->next
        && (r->request_body_in_file_only || r->request_body_in_single_buf))
    {
        rb->bufs = rb->bufs->next;
    }
	/*read_event_handler设为ngx_http_block_reading方法，表示连接上再有读事件将不做任何处
理*/
    r->read_event_handler = ngx_http_block_reading;
	/*执行HTTP模块提供的post_handler回调方法后，ngx_http_do_read_client_request_body方法结束，返回NGX_OK*/
    rb->post_handler(r);

    return NGX_OK;
}


static ngx_int_t
ngx_http_write_request_body(ngx_http_request_t *r, ngx_chain_t *body)
{
    ssize_t                    n;
    ngx_temp_file_t           *tf;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    rb = r->request_body;

    if (rb->temp_file == NULL) {
        tf = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
        if (tf == NULL) {
            return NGX_ERROR;
        }

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        tf->file.fd = NGX_INVALID_FILE;
        tf->file.log = r->connection->log;
        tf->path = clcf->client_body_temp_path;
        tf->pool = r->pool;
        tf->warn = "a client request body is buffered to a temporary file";
        tf->log_level = r->request_body_file_log_level;
        tf->persistent = r->request_body_in_persistent_file;
        tf->clean = r->request_body_in_clean_file;

        if (r->request_body_file_group_access) {
            tf->access = 0660;
        }

        rb->temp_file = tf;
    }

    n = ngx_write_chain_to_temp_file(rb->temp_file, body);

    /* TODO: n == 0 or not complete and level event */

    if (n == NGX_ERROR) {
        return NGX_ERROR;
    }

    rb->temp_file->offset += n;

    return NGX_OK;
}


ngx_int_t
ngx_http_discard_request_body(ngx_http_request_t *r)
{
    ssize_t       size;
    ngx_event_t  *rev;

    if (r != r->main || r->discard_body) {
        return NGX_OK;
    }

    if (ngx_http_test_expect(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    rev = r->connection->read;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0, "http set discard body");

    if (rev->timer_set) {
        ngx_del_timer(rev);
    }

    if (r->headers_in.content_length_n <= 0 || r->request_body) {
        return NGX_OK;
    }

    size = r->header_in->last - r->header_in->pos;

    if (size) {
        if (r->headers_in.content_length_n > size) {
            r->header_in->pos += size;
            r->headers_in.content_length_n -= size;

        } else {
            r->header_in->pos += (size_t) r->headers_in.content_length_n;
            r->headers_in.content_length_n = 0;
            return NGX_OK;
        }
    }

    r->read_event_handler = ngx_http_discarded_request_body_handler;

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_read_discarded_request_body(r) == NGX_OK) {
        r->lingering_close = 0;

    } else {
        r->count++;
        r->discard_body = 1;
    }

    return NGX_OK;
}


void
ngx_http_discarded_request_body_handler(ngx_http_request_t *r)
{
    ngx_int_t                  rc;
    ngx_msec_t                 timer;
    ngx_event_t               *rev;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;
    rev = c->read;

    if (rev->timedout) {
        c->timedout = 1;
        c->error = 1;
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (r->lingering_time) {
        timer = (ngx_msec_t) (r->lingering_time - ngx_time());

        if (timer <= 0) {
            r->discard_body = 0;
            r->lingering_close = 0;
            ngx_http_finalize_request(r, NGX_ERROR);
            return;
        }

    } else {
        timer = 0;
    }

    rc = ngx_http_read_discarded_request_body(r);

    if (rc == NGX_OK) {
        r->discard_body = 0;
        r->lingering_close = 0;
        ngx_http_finalize_request(r, NGX_DONE);
        return;
    }

    /* rc == NGX_AGAIN */

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        c->error = 1;
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (timer) {

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        timer *= 1000;

        if (timer > clcf->lingering_timeout) {
            timer = clcf->lingering_timeout;
        }

        ngx_add_timer(rev, timer);
    }
}


static ngx_int_t
ngx_http_read_discarded_request_body(ngx_http_request_t *r)
{
    size_t   size;
    ssize_t  n;
    u_char   buffer[NGX_HTTP_DISCARD_BUFFER_SIZE];

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http read discarded body");

    for ( ;; ) {
        if (r->headers_in.content_length_n == 0) {
            r->read_event_handler = ngx_http_block_reading;
            return NGX_OK;
        }

        if (!r->connection->read->ready) {
            return NGX_AGAIN;
        }

        size = (r->headers_in.content_length_n > NGX_HTTP_DISCARD_BUFFER_SIZE) ?
                   NGX_HTTP_DISCARD_BUFFER_SIZE:
                   (size_t) r->headers_in.content_length_n;

        n = r->connection->recv(r->connection, buffer, size);

        if (n == NGX_ERROR) {
            r->connection->error = 1;
            return NGX_OK;
        }

        if (n == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        if (n == 0) {
            return NGX_OK;
        }

        r->headers_in.content_length_n -= n;
    }
}


static ngx_int_t
ngx_http_test_expect(ngx_http_request_t *r)
{
    ngx_int_t   n;
    ngx_str_t  *expect;

    if (r->expect_tested
        || r->headers_in.expect == NULL
        || r->http_version < NGX_HTTP_VERSION_11)
    {
        return NGX_OK;
    }

    r->expect_tested = 1;

    expect = &r->headers_in.expect->value;

    if (expect->len != sizeof("100-continue") - 1
        || ngx_strncasecmp(expect->data, (u_char *) "100-continue",
                           sizeof("100-continue") - 1)
           != 0)
    {
        return NGX_OK;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "send 100 Continue");

    n = r->connection->send(r->connection,
                            (u_char *) "HTTP/1.1 100 Continue" CRLF CRLF,
                            sizeof("HTTP/1.1 100 Continue" CRLF CRLF) - 1);

    if (n == sizeof("HTTP/1.1 100 Continue" CRLF CRLF) - 1) {
        return NGX_OK;
    }

    /* we assume that such small packet should be send successfully */

    return NGX_ERROR;
}
