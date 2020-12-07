/*
 * Copyright (c) 2008-2010 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

#ifndef __DISPATCH_ONCE__
#define __DISPATCH_ONCE__

#ifndef __DISPATCH_INDIRECT__
#error "Please #include <dispatch/dispatch.h> instead of this file directly."
#include <dispatch/base.h> // for HeaderDoc
#endif

DISPATCH_ASSUME_NONNULL_BEGIN

__BEGIN_DECLS

/*!
 * @typedef dispatch_once_t dispatch_once_t 仅是 long 的别名。
 *
 * @abstract
 * A predicate for use with dispatch_once(). It must be initialized to zero.
 * Note: static and global variables default to zero.
 * 与 dispatch_once 一起使用的谓词，必须将其初始化为零。注意：静态和全局变量默认为零。
 
 */
DISPATCH_SWIFT3_UNAVAILABLE("Use lazily initialized globals instead")
typedef intptr_t dispatch_once_t;

#if defined(__x86_64__) || defined(__i386__) || defined(__s390x__)
#define DISPATCH_ONCE_INLINE_FASTPATH 1
#elif defined(__APPLE__)
#define DISPATCH_ONCE_INLINE_FASTPATH 1
#else
#define DISPATCH_ONCE_INLINE_FASTPATH 0
#endif

/*!
 * @function dispatch_once
 *
 * @abstract 一次只能执行一次块（block）。
 * Execute a block once and only once.
 *
 * @param predicate 指向 dispatch_once_t 的指针，用于测试该 block 是否已完成。（这里我们常使用 static onceToken; 静态和全局变量默认为零。）
 * A pointer to a dispatch_once_t that is used to test whether the block has
 * completed or not.
 *
 * @param block 该 block 全局仅执行一次。
 * The block to execute once.
 *
 * @discussion 在使用或测试由该块初始化的任何变量之前，请始终调用 dispatch_once。
 * Always call dispatch_once() before using or testing any variables that are
 * initialized by the block.
 */
#ifdef __BLOCKS__
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
DISPATCH_SWIFT3_UNAVAILABLE("Use lazily initialized globals instead")
void
dispatch_once(dispatch_once_t *predicate,
		DISPATCH_NOESCAPE dispatch_block_t block);

//在目前的 Mac iPhone 主流机器，（或者 apple 的主流平台下）下此值应该都是 1，那么将使用如下的内联 _dispatch_once。
//__builtin_expect 这个指令是 GCC 引入的，作用是允许程序员将最有可能执行的分支告诉编译器，这个指令的写法为：__builtin_expect(EXP, N)，意思是：EXP == N 的概率很大，然后 CPU 会预取该分支的指令，这样 CPU 流水线就会很大概率减少了 CPU 等待取指令的耗时，从而提高 CPU 的效率。
// dispatch_compiler_barrier 内存屏障。
#if DISPATCH_ONCE_INLINE_FASTPATH
DISPATCH_INLINE DISPATCH_ALWAYS_INLINE DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
DISPATCH_SWIFT3_UNAVAILABLE("Use lazily initialized globals instead")
void
_dispatch_once(dispatch_once_t *predicate,
		DISPATCH_NOESCAPE dispatch_block_t block)
{
	if (DISPATCH_EXPECT(*predicate, ~0l) != ~0l) {
		dispatch_once(predicate, block);
	} else {
		dispatch_compiler_barrier();
	}
	DISPATCH_COMPILER_CAN_ASSUME(*predicate == ~0l);
}
#undef dispatch_once
#define dispatch_once _dispatch_once
#endif
#endif // DISPATCH_ONCE_INLINE_FASTPATH

API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_NONNULL3 DISPATCH_NOTHROW
DISPATCH_SWIFT3_UNAVAILABLE("Use lazily initialized globals instead")
void
dispatch_once_f(dispatch_once_t *predicate, void *_Nullable context,
		dispatch_function_t function);

#if DISPATCH_ONCE_INLINE_FASTPATH
DISPATCH_INLINE DISPATCH_ALWAYS_INLINE DISPATCH_NONNULL1 DISPATCH_NONNULL3
DISPATCH_NOTHROW
DISPATCH_SWIFT3_UNAVAILABLE("Use lazily initialized globals instead")
void
_dispatch_once_f(dispatch_once_t *predicate, void *_Nullable context,
		dispatch_function_t function)
{ //// DISPATCH_EXPECT(*predicate, ~0l) 表示很大概率 *predicate 的值是 ~0l，并返回 *predicate 的值
	if (DISPATCH_EXPECT(*predicate, ~0l) != ~0l) {
		//// 当 *predicate 等于 0 时，调用 dispatch_once 函数
		dispatch_once_f(predicate, context, function);
	} else {
		//// 否则，执行这里仅是 
		dispatch_compiler_barrier();
	}
	DISPATCH_COMPILER_CAN_ASSUME(*predicate == ~0l);
}
#undef dispatch_once_f
#define dispatch_once_f _dispatch_once_f
#endif // DISPATCH_ONCE_INLINE_FASTPATH

__END_DECLS

DISPATCH_ASSUME_NONNULL_END

#endif
