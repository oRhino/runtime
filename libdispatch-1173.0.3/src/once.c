/*
 * Copyright (c) 2008-2013 Apple Inc. All rights reserved.
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

#include "internal.h"

#undef dispatch_once
#undef dispatch_once_f


#ifdef __BLOCKS__
//入口
void
dispatch_once(dispatch_once_t *val, dispatch_block_t block)
{
	dispatch_once_f(val, block, _dispatch_Block_invoke(block));
}
#endif

#if DISPATCH_ONCE_INLINE_FASTPATH
#define DISPATCH_ONCE_SLOW_INLINE inline DISPATCH_ALWAYS_INLINE
#else
#define DISPATCH_ONCE_SLOW_INLINE DISPATCH_NOINLINE
#endif // DISPATCH_ONCE_INLINE_FASTPATH

DISPATCH_NOINLINE
static void
_dispatch_once_callout(dispatch_once_gate_t l, void *ctxt,
		dispatch_function_t func)
{
	///block调用执行 _dispatch_trace_client_callout
	_dispatch_client_callout(ctxt, func);
	/// 广播唤醒阻塞的线程
	_dispatch_once_gate_broadcast(l);
}

DISPATCH_NOINLINE
void
dispatch_once_f(dispatch_once_t *val, void *ctxt, dispatch_function_t func)
{
	// 把val转换为dispatch_once_gate_t类型
	dispatch_once_gate_t l = (dispatch_once_gate_t)val;

#if !DISPATCH_ONCE_INLINE_FASTPATH || DISPATCH_ONCE_USE_QUIESCENT_COUNTER
    // 原子性获取l->dgo_once 的值
	uintptr_t v = os_atomic_load(&l->dgo_once, acquire);
	if (likely(v == DLOCK_ONCE_DONE)) {
		// 如果v等于DLOCK_ONCE_DONE，表示任务已经执行过了，直接return（likely大概率是)
		return;
	}
#if DISPATCH_ONCE_USE_QUIESCENT_COUNTER
    //如果任务执行后，标志位设置失败了,标志位:DLOCK_FAILED_TRYLOCK_BIT，
	//则走到_dispatch_once_mark_done_if_quiesced函数，再次进行原子存储，将标识符置为DLOCK_ONCE_DONE
	if (likely(DISPATCH_ONCE_IS_GEN(v))) {
		return _dispatch_once_mark_done_if_quiesced(l, v);
	}
#endif
#endif
    //	_dispatch_once_gate_tryenter 函数当 l（l->dgo_once）值是 NULL（0）时返回 YES，否则返回 NO.
	if (_dispatch_once_gate_tryenter(l)) {
		/// 表示还没有执行过则进入 if 开始执行提交的函数
		return _dispatch_once_callout(l, ctxt, func);
	}
    /// 如果此时有任务正在执行，再次进来一个任务2，则通过_dispatch_once_wait函数让任务2进入无限次等待,当 func 执行完后 _dispatch_once_callout 内部会发出广播唤醒阻塞线程
	return _dispatch_once_wait(l);
}
