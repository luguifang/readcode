
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static ngx_int_t ngx_http_write_filter_init(ngx_conf_t *cf);


static ngx_http_module_t  ngx_http_write_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_write_filter_init,            /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL,                                  /* merge location configuration */
};


ngx_module_t  ngx_http_write_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_write_filter_module_ctx,     /* module context */
    NULL,                                  /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


ngx_int_t
ngx_http_write_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    off_t                      size, sent, nsent, limit;
    ngx_uint_t                 last, flush;
    ngx_msec_t                 delay;
    ngx_chain_t               *cl, *ln, **ll, *chain;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;
	/*检查请求是否出错 luguifang*/
    if (c->error) {
        return NGX_ERROR;
    }

    size = 0;
    flush = 0;
    last = 0;
    ll = &r->out;

    /* find the size, the flush point and the last link of the saved chain */
	/*找到请求的ngx_http_request_t结构体中存放的等待发送的缓冲区链表out，遍历这个
	ngx_chain_t类型的缓冲区链表，计算出out缓冲区共占用了多大的字节数*/
    for (cl = r->out; cl; cl = cl->next) {
        ll = &cl->next;

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, c->log, 0,
                       "write old buf t:%d f:%d %p, pos %p, size: %z "
                       "file: %O, size: %z",
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);

#if 1
        if (ngx_buf_size(cl->buf) == 0 && !ngx_buf_special(cl->buf)) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "zero size buf in writer "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary,
                          cl->buf->recycled,
                          cl->buf->in_file,
                          cl->buf->start,
                          cl->buf->pos,
                          cl->buf->last,
                          cl->buf->file,
                          cl->buf->file_pos,
                          cl->buf->file_last);

            ngx_debug_point();
            return NGX_ERROR;
        }
#endif

        size += ngx_buf_size(cl->buf);

        if (cl->buf->flush || cl->buf->recycled) {
            flush = 1;
        }

        if (cl->buf->last_buf) {
            last = 1;
        }
    }

    /* add the new chain to the existent one */
	/*将in中的缓冲区加入到out链表的末尾，并计算out缓冲区共占用多大的字节数*/
    for (ln = in; ln; ln = ln->next) {
        cl = ngx_alloc_chain_link(r->pool);
        if (cl == NULL) {
            return NGX_ERROR;
        }

        cl->buf = ln->buf;
        *ll = cl;
        ll = &cl->next;

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, c->log, 0,
                       "write new buf t:%d f:%d %p, pos %p, size: %z "
                       "file: %O, size: %z",
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);

#if 1
        if (ngx_buf_size(cl->buf) == 0 && !ngx_buf_special(cl->buf)) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "zero size buf in writer "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary,
                          cl->buf->recycled,
                          cl->buf->in_file,
                          cl->buf->start,
                          cl->buf->pos,
                          cl->buf->last,
                          cl->buf->file,
                          cl->buf->file_pos,
                          cl->buf->file_last);

            ngx_debug_point();
            return NGX_ERROR;
        }
#endif

        size += ngx_buf_size(cl->buf);

        if (cl->buf->flush || cl->buf->recycled) {
            flush = 1;
        }

        if (cl->buf->last_buf) {
            last = 1;
        }
    }

    *ll = NULL;

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http write filter: l:%d f:%d s:%O", last, flush, size);

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    /*
     * avoid the output if there are no last buf, no flush point,
     * there are the incoming bufs and the size of all bufs
     * is smaller than "postpone_output" directive
     */
	/*
	检查缓冲区中每个ngx_buf_t块的3个标志位：flush、
recycled、last_buf，如果这3个标志位同时为0（即待发送的out链表中没有一个缓冲区表示响
应已经结束或需要立刻发送出去），而且本次要发送的缓冲区in虽然不为空，但以上两步骤
中计算出的待发送响应的大小又小于配置文件中的postpone_output参数，那么说明当前的缓
冲区是不完整的且没有必要立刻发送，返回NGX_OK--------luguifang*/
    if (!last && !flush && in && size < (off_t) clcf->postpone_output) {
        return NGX_OK;
    }
	/*首先检查连接上写事件的标志位delayed，如果delayed为1，则表示这一次的epoll调度中
	请求仍需要减速，是不可以发送响应的，delayed为1指明了响应需要延迟发送*/
    if (c->write->delayed) {
		/*将客户端对应的ngx_connection_t结构体中的buffered标志位放上
		NGX_HTTP_WRITE_BUFFERED宏，同时返回NGX_AGAIN，这是在告诉HTTP框架out缓冲
		区中还有响应等待发送----luguifang*/
        c->buffered |= NGX_HTTP_WRITE_BUFFERED;
        return NGX_AGAIN;
    }

    if (size == 0 && !(c->buffered & NGX_LOWLEVEL_BUFFERED)) {
        if (last) {
            r->out = NULL;
            c->buffered &= ~NGX_HTTP_WRITE_BUFFERED;

            return NGX_OK;
        }

        if (flush) {
            do {
                r->out = r->out->next;
            } while (r->out);

            c->buffered &= ~NGX_HTTP_WRITE_BUFFERED;

            return NGX_OK;
        }

        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "the http output chain is empty");

        ngx_debug_point();

        return NGX_ERROR;
    }


	/*检查ngx_http_request_t结构体中的
	limit_rate发送响应的速率，如果limit_rate为0，表示这个请求不需要限制发送速度
	如果limit_rate大于0，则说明发送响应的速度不能超过limit_rate指定的速度
	limit_rate表示每秒可以发送的字节数，超过这个数字就需要限速；然而，限
速这个动作必须是在发送了limit_rate_after字节的响应后才能生效（对于小响应包的优化设
计）-------luguifang*/
    if (r->limit_rate) {
		// 计算本次可以发送的limit字节响应
        limit = r->limit_rate * (ngx_time() - r->start_sec + 1)
                - (c->sent - clcf->limit_rate_after);
		/*如果limit小于或等于0，它表示这个连接上的发送响应速度已经超出了limit_rate配置项的限制
		本次不可以继续发送*/
        if (limit <= 0) {
			//将连接上写事件的delayed标志位置为1	
            c->write->delayed = 1;
			//将写事件加入定时器中，其中超时时间要根据limit来计算
            ngx_add_timer(c->write,
                          (ngx_msec_t) (- limit * 1000 / r->limit_rate + 1));

            c->buffered |= NGX_HTTP_WRITE_BUFFERED;

            return NGX_AGAIN;
        }

        if (clcf->sendfile_max_chunk
            && (off_t) clcf->sendfile_max_chunk < limit)
        {
            limit = clcf->sendfile_max_chunk;
        }

    } else {
        limit = clcf->sendfile_max_chunk;
    }

    sent = c->sent;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http write filter limit %O", limit);
	//通过和其他限制值比较取小后，放送limit 字节响应-----luguifang
    chain = c->send_chain(c, r->out, limit);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http write filter %p", chain);

    if (chain == NGX_CHAIN_ERROR) {
        c->error = 1;
        return NGX_ERROR;
    }
	//跟上部类似计算limit值
    if (r->limit_rate) {

        nsent = c->sent;

        if (clcf->limit_rate_after) {

            sent -= clcf->limit_rate_after;
            if (sent < 0) {
                sent = 0;
            }

            nsent -= clcf->limit_rate_after;
            if (nsent < 0) {
                nsent = 0;
            }
        }

        delay = (ngx_msec_t) ((nsent - sent) * 1000 / r->limit_rate);

        if (delay > 0) {
            limit = 0;
            c->write->delayed = 1;
            ngx_add_timer(c->write, delay);
        }
    }

    if (limit
        && c->write->ready
        && c->sent - sent >= limit - (off_t) (2 * ngx_pagesize))
    {
        c->write->delayed = 1;
        ngx_add_timer(c->write, 1);
    }

	/*重置ngx_http_request_t结构体的out缓冲区，把已经发送成功的缓冲区归还给内存
池。如果out链表中还有剩余的没有发送出去的缓冲区，则添加到out链表头部*/
    for (cl = r->out; cl && cl != chain; /* void */) {
        ln = cl;
        cl = cl->next;
        ngx_free_chain(r->pool, ln);
    }

    r->out = chain;

    if (chain) {
        c->buffered |= NGX_HTTP_WRITE_BUFFERED;
        return NGX_AGAIN;
    }

    c->buffered &= ~NGX_HTTP_WRITE_BUFFERED;

    if ((c->buffered & NGX_LOWLEVEL_BUFFERED) && r->postponed == NULL) {
        return NGX_AGAIN;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_write_filter_init(ngx_conf_t *cf)
{
    ngx_http_top_body_filter = ngx_http_write_filter;

    return NGX_OK;
}
