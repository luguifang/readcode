
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#if (NGX_SMP)
#define NGX_SMP_LOCK  "lock;"
#else
#define NGX_SMP_LOCK
#endif


/*
 * "cmpxchgl  r, [m]":
 *
 *     if (eax == [m]) {
 *         zf = 1;
 *         [m] = r;
 *     } else {
 *         zf = 0;
 *         eax = [m];
 *     }
 *
 *
 * The "r" means the general register.
 * The "=a" and "a" are the %eax register.
 * Although we can return result in any register, we use "a" because it is
 * used in cmpxchgl anyway.  The result is actually in %al but not in %eax,
 * however, as the code is inlined gcc can test %al as well as %eax,
 * and icc adds "movzbl %al, %eax" by itself.
 *
 * The "cc" means that flags were changed.
 */


/*
说明一下上述代码，在嵌入汇编语言的输入部分，"m"(*lock)表示*lock变量
是在内存中，操作*lock时直接通过内存（不使用寄存器）处理，而"a"(old)表示把old变量写
入eax寄存器中，"r"(set)表示把set变量写入通用寄存器中，这些都是在为cmpxchgl语句做准
备。“cmpxchgl%3,%1”相当于“cmpxchglset*lock”（含义参照上面介绍过的伪代码）。这3行汇
编语句的意思如下：首先锁住总线防止多核的并发执行，接着判断原子变量*lock与old值是
否相等，若相等，则把*lock值设为set，同时设res为1，方法返回；若不相等，则设res为0，
方法返回
----lgf 6.2
*/





static ngx_inline ngx_atomic_uint_t
ngx_atomic_cmp_set(ngx_atomic_t *lock, ngx_atomic_uint_t old,
    ngx_atomic_uint_t set)
{
    u_char  res;
	/*嵌入汇编语言------lgf6.2*/
    __asm__ volatile (

         NGX_SMP_LOCK /*多核架构下首先要锁住总线---lgf6.2*/
    "    cmpxchgl  %3, %1;   "  /*将*lock的值与寄存器eax中的old 值相比 如果相等则置*lock值为set----lgf6.2*/
    "    sete      %0;       "
	/*cmpxchgl的比较若是相等，则把zf标志位1写入res变量，否则res为0 ----lgf6.2*/

    : "=a" (res) : "m" (*lock), "a" (old), "r" (set) : "cc", "memory");

    return res;
}


/*
 * "xaddl  r, [m]":
 *
 *     temp = [m];
 *     [m] += r;
 *     r = temp;
 *
 *
 * The "+r" means the general register.
 * The "cc" means that flags were changed.
 */


#if !(( __GNUC__ == 2 && __GNUC_MINOR__ <= 7 ) || ( __INTEL_COMPILER >= 800 ))

/*
 * icc 8.1 and 9.0 compile broken code with -march=pentium4 option:
 * ngx_atomic_fetch_add() always return the input "add" value,
 * so we use the gcc 2.7 version.
 *
 * icc 8.1 and 9.0 with -march=pentiumpro option or icc 7.1 compile
 * correct code.
 */

static ngx_inline ngx_atomic_int_t
ngx_atomic_fetch_add(ngx_atomic_t *value, ngx_atomic_int_t add)
{
    __asm__ volatile (

         NGX_SMP_LOCK
    "    xaddl  %0, %1;   "

    : "+r" (add) : "m" (*value) : "cc", "memory");

    return add;
}


#else

/*
 * gcc 2.7 does not support "+r", so we have to use the fixed
 * %eax ("=a" and "a") and this adds two superfluous instructions in the end
 * of code, something like this: "mov %eax, %edx / mov %edx, %eax".
 */

static ngx_inline ngx_atomic_int_t
ngx_atomic_fetch_add(ngx_atomic_t *value, ngx_atomic_int_t add)
{
    ngx_atomic_uint_t  old;

    __asm__ volatile (

         NGX_SMP_LOCK
    "    xaddl  %2, %1;   "

    : "=a" (old) : "m" (*value), "a" (add) : "cc", "memory");

    return old;
}

#endif


/*
 * on x86 the write operations go in a program order, so we need only
 * to disable the gcc reorder optimizations
 */

#define ngx_memory_barrier()    __asm__ volatile ("" ::: "memory")

/* old "as" does not support "pause" opcode */
#define ngx_cpu_pause()         __asm__ (".byte 0xf3, 0x90")
