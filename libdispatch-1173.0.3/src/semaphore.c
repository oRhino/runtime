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

DISPATCH_WEAK // rdar://problem/8503746
long _dispatch_semaphore_signal_slow(dispatch_semaphore_t dsema);

#pragma mark -
#pragma mark dispatch_semaphore_t
//dispatch_semaphore 是 GCD 中最常见的操作，通常用于保证资源的多线程安全性和控制任务的并发数量。其本质实际上是基于 mach 内核的信号量接口来实现的
//dispatch_semaphore_t 是指向 dispatch_semaphore_s 结构体的指针

//用初始值（long value）创建新的计数信号量。
dispatch_semaphore_t
dispatch_semaphore_create(long value)
{
	dispatch_semaphore_t dsema; // 指向 dispatch_semaphore_s 结构体的指针

	// If the internal value is negative, then the absolute of the value is
	// equal to the number of waiting threads. Therefore it is bogus to
	// initialize the semaphore with a negative value.
	if (value < 0) {
		return DISPATCH_BAD_INPUT; // 如果 value 值小于 0，则直接返回 0
	}
    //// _dispatch_object_alloc 是为 dispatch_semaphore_s 申请空间，然后用 &OS_dispatch_semaphore_class 初始化，
	// &OS_dispatch_semaphore_class 设置了 dispatch_semaphore_t 的相关回调函数，如销毁函数 _dispatch_semaphore_dispose 等
	dsema = _dispatch_object_alloc(DISPATCH_VTABLE(semaphore),
			sizeof(struct dispatch_semaphore_s));
	dsema->do_next = DISPATCH_OBJECT_LISTLESS; // 表示链表的下一个节点
	dsema->do_targetq = _dispatch_get_default_queue(false); // 目标队列（从全局的队列数组 _dispatch_root_queues 中取默认队列）
	dsema->dsema_value = value; // 当前值（当前是初始值）
	_dispatch_sema4_init(&dsema->dsema_sema, _DSEMA4_POLICY_FIFO);
	dsema->dsema_orig = value; // 初始值
	return dsema;
}

//信号量的销毁函数。
void
_dispatch_semaphore_dispose(dispatch_object_t dou,
		DISPATCH_UNUSED bool *allow_free)
{
	dispatch_semaphore_t dsema = dou._dsema;

	//// 容错判断，如果当前 dsema_value 小于 dsema_orig，表示信号量还正在使用，不能进行销毁，如下代码会导致此 crash
	// dispatch_semaphore_t sema = dispatch_semaphore_create(1); // 创建 value = 1，orig = 1
	// dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER); // value = 0，orig = 1
	// sema = dispatch_semaphore_create(1); // 赋值导致原始 dispatch_semaphore_s 释放，但是此时 orig 是 1，value 是 0 则直接 crash

	if (dsema->dsema_value < dsema->dsema_orig) {
		DISPATCH_CLIENT_CRASH(dsema->dsema_orig - dsema->dsema_value,
				"Semaphore object deallocated while in use");
	}

	_dispatch_sema4_dispose(&dsema->dsema_sema, _DSEMA4_POLICY_FIFO);
}

size_t
_dispatch_semaphore_debug(dispatch_object_t dou, char *buf, size_t bufsiz)
{
	dispatch_semaphore_t dsema = dou._dsema;

	size_t offset = 0;
	offset += dsnprintf(&buf[offset], bufsiz - offset, "%s[%p] = { ",
			_dispatch_object_class_name(dsema), dsema);
	offset += _dispatch_object_debug_attr(dsema, &buf[offset], bufsiz - offset);
#if USE_MACH_SEM
	offset += dsnprintf(&buf[offset], bufsiz - offset, "port = 0x%x, ",
			dsema->dsema_sema);
#endif
	offset += dsnprintf(&buf[offset], bufsiz - offset,
			"value = %ld, orig = %ld }", dsema->dsema_value, dsema->dsema_orig);
	return offset;
}

DISPATCH_NOINLINE
long
_dispatch_semaphore_signal_slow(dispatch_semaphore_t dsema)
{
	_dispatch_sema4_create(&dsema->dsema_sema, _DSEMA4_POLICY_FIFO);
	////count 传 1，唤醒一条线程
	_dispatch_sema4_signal(&dsema->dsema_sema, 1);
	return 1;
}

//dispatch_semaphore_signal 发信号（增加）信号量。如果先前的值小于零，则此函数在返回之前唤醒等待的线程。如果线程被唤醒，此函数将返回非零值。否则，返回零。
long
dispatch_semaphore_signal(dispatch_semaphore_t dsema)
{
	//// 原子操作 dsema 的成员变量 dsema_value 的值加 1
	long value = os_atomic_inc2o(dsema, dsema_value, release);//对原子操作 +1 的封装。
	if (likely(value > 0)) {
		// 如果 value 大于 0 表示目前没有线程需要唤醒，直接 return 0
		return 0;
	}
	//// 如果过度释放，导致 value 的值一直增加到 LONG_MIN（溢出），则 crash
	if (unlikely(value == LONG_MIN)) {
		DISPATCH_CLIENT_CRASH(value,
				"Unbalanced call to dispatch_semaphore_signal()");
	}
	//// value 小于等于 0 时，表示目前有线程需要唤醒
	return _dispatch_semaphore_signal_slow(dsema);
}


DISPATCH_NOINLINE
static long
_dispatch_semaphore_wait_slow(dispatch_semaphore_t dsema,
		dispatch_time_t timeout)
{
	long orig;
    //// 为 &dsema->dsema_sema 赋值
	_dispatch_sema4_create(&dsema->dsema_sema, _DSEMA4_POLICY_FIFO);
	//// 如果 timeout 是一个特定时间的话调用 _dispatch_sema4_timedwait 进行 timeout 时间的等待
	switch (timeout) {
	default:
		if (!_dispatch_sema4_timedwait(&dsema->dsema_sema, timeout)) {
			break;
		}
		// Fall through and try to undo what the fast path did to
		// dsema->dsema_value
	case DISPATCH_TIME_NOW: //如果 timeout 参数是 DISPATCH_TIME_NOW
		orig = dsema->dsema_value;
		while (orig < 0) {
			//// dsema_value 加 1 抵消掉 dispatch_semaphore_wait 函数中的减 1 操作
			if (os_atomic_cmpxchgvw2o(dsema, dsema_value, orig, orig + 1,
					&orig, relaxed)) {
				//// 返回超时
				return _DSEMA4_TIMEOUT();
			}
		}
		// Another thread called semaphore_signal().
		// Fall through and drain the wakeup.
	//// 如果 timeout 参数是 DISPATCH_TIME_FOREVER 的话调用 _dispatch_sema4_wait 一直等待，直到得到 signal 信号
	case DISPATCH_TIME_FOREVER:
		_dispatch_sema4_wait(&dsema->dsema_sema);
		break;
	}
	return 0;
}

//等待（减少）信号量
long
dispatch_semaphore_wait(dispatch_semaphore_t dsema, dispatch_time_t timeout)
{
	//// 原子操作 dsema 的成员变量 dsema_value 的值减 1
	long value = os_atomic_dec2o(dsema, dsema_value, acquire);
	if (likely(value >= 0)) {
		return 0; //// 如果减 1 后仍然大于等于 0，则直接 return
	}
	//// 如果小于 0，则调用 _dispatch_semaphore_wait_slow 函数进行阻塞等待
	return _dispatch_semaphore_wait_slow(dsema, timeout);
}

#pragma mark - 组队列
#pragma mark dispatch_group_t

DISPATCH_ALWAYS_INLINE
static inline dispatch_group_t
_dispatch_group_create_with_count(uint32_t n)
{
	//typedef struct dispatch_group_s *dispatch_group_t;
	//分配内存空间 DISPATCH_VTABLE(name)层层嵌套的宏定义 最终为:OS_dispatch_##name##_class
	dispatch_group_t dg = _dispatch_object_alloc(DISPATCH_VTABLE(group),
			sizeof(struct dispatch_group_s));
	dg->do_next = DISPATCH_OBJECT_LISTLESS;
	//设置目标队列
	/*
	_dispatch_get_default_queue(overcommit) \
			_dispatch_root_queues[DISPATCH_ROOT_QUEUE_IDX_DEFAULT_QOS + \
					!!(overcommit)]._as_dq
	 */
	dg->do_targetq = _dispatch_get_default_queue(false);
	if (n) { // 0  表示不执行
		//os_atomic_store(&(p)->f, (v), m)
		//把v 存入dg.dg_bits
		os_atomic_store2o(dg, dg_bits,
				(uint32_t)-n * DISPATCH_GROUP_VALUE_INTERVAL, relaxed);
		//把1 存入do_ref_cnt
		os_atomic_store2o(dg, do_ref_cnt, 1, relaxed); // <rdar://22318411>
	}
	return dg;
}

dispatch_group_t
dispatch_group_create(void)
{
	return _dispatch_group_create_with_count(0);
}

dispatch_group_t
_dispatch_group_create_and_enter(void)
{ //传1
	return _dispatch_group_create_with_count(1);
}

void
_dispatch_group_dispose(dispatch_object_t dou, DISPATCH_UNUSED bool *allow_free)
{
	uint64_t dg_state = os_atomic_load2o(dou._dg, dg_state, relaxed);

	if (unlikely((uint32_t)dg_state)) {
		DISPATCH_CLIENT_CRASH((uintptr_t)dg_state,
				"Group object deallocated while in use");
	}
}

size_t
_dispatch_group_debug(dispatch_object_t dou, char *buf, size_t bufsiz)
{
	dispatch_group_t dg = dou._dg;
	uint64_t dg_state = os_atomic_load2o(dg, dg_state, relaxed);

	size_t offset = 0;
	offset += dsnprintf(&buf[offset], bufsiz - offset, "%s[%p] = { ",
			_dispatch_object_class_name(dg), dg);
	offset += _dispatch_object_debug_attr(dg, &buf[offset], bufsiz - offset);
	offset += dsnprintf(&buf[offset], bufsiz - offset,
			"count = %d, gen = %d, waiters = %d, notifs = %d }",
			_dg_state_value(dg_state), _dg_state_gen(dg_state),
			(bool)(dg_state & DISPATCH_GROUP_HAS_WAITERS),
			(bool)(dg_state & DISPATCH_GROUP_HAS_NOTIFS));
	return offset;
}

DISPATCH_NOINLINE
static long
_dispatch_group_wait_slow(dispatch_group_t dg, uint32_t gen,
		dispatch_time_t timeout)
{
	for (;;) {
		int rc = _dispatch_wait_on_address(&dg->dg_gen, gen, timeout, 0);
		if (likely(gen != os_atomic_load2o(dg, dg_gen, acquire))) {
			return 0;
		}
		if (rc == ETIMEDOUT) {
			return _DSEMA4_TIMEOUT();
		}
	}
}

long
dispatch_group_wait(dispatch_group_t dg, dispatch_time_t timeout)
{
	uint64_t old_state, new_state;

	os_atomic_rmw_loop2o(dg, dg_state, old_state, new_state, relaxed, {
		if ((old_state & DISPATCH_GROUP_VALUE_MASK) == 0) {
			os_atomic_rmw_loop_give_up_with_fence(acquire, return 0);
		}
		if (unlikely(timeout == 0)) {
			os_atomic_rmw_loop_give_up(return _DSEMA4_TIMEOUT());
		}
		new_state = old_state | DISPATCH_GROUP_HAS_WAITERS;
		if (unlikely(old_state & DISPATCH_GROUP_HAS_WAITERS)) {
			os_atomic_rmw_loop_give_up(break);
		}
	});

	return _dispatch_group_wait_slow(dg, _dg_state_gen(new_state), timeout);
}

DISPATCH_NOINLINE
static void
_dispatch_group_wake(dispatch_group_t dg, uint64_t dg_state, bool needs_release)
{
	uint16_t refs = needs_release ? 1 : 0; // <rdar://problem/22318411>

	if (dg_state & DISPATCH_GROUP_HAS_NOTIFS) {
		dispatch_continuation_t dc, next_dc, tail;

		// Snapshot before anything is notified/woken <rdar://problem/8554546>
		dc = os_mpsc_capture_snapshot(os_mpsc(dg, dg_notify), &tail);
		do {
			dispatch_queue_t dsn_queue = (dispatch_queue_t)dc->dc_data;
			next_dc = os_mpsc_pop_snapshot_head(dc, tail, do_next);
			_dispatch_continuation_async(dsn_queue, dc,
					_dispatch_qos_from_pp(dc->dc_priority), dc->dc_flags);
			_dispatch_release(dsn_queue);
		} while ((dc = next_dc));

		refs++;
	}

	if (dg_state & DISPATCH_GROUP_HAS_WAITERS) {
		_dispatch_wake_by_address(&dg->dg_gen);
	}

	//释放
	if (refs) _dispatch_release_n(dg, refs);
}

//手动标识一个任务块执行完毕
void
dispatch_group_leave(dispatch_group_t dg)
{
	// The value is incremented on a 64bits wide atomic so that the carry for
	// the -1 -> 0 transition increments the generation atomically.
	// 这个值在64位宽的原子上递增
	// 原子性 + 1
	uint64_t new_state, old_state = os_atomic_add_orig2o(dg, dg_state,
			DISPATCH_GROUP_VALUE_INTERVAL, release);
	uint32_t old_value = (uint32_t)(old_state & DISPATCH_GROUP_VALUE_MASK);

	if (unlikely(old_value == DISPATCH_GROUP_VALUE_1)) {
		old_state += DISPATCH_GROUP_VALUE_INTERVAL;
		do {
			new_state = old_state;
			if ((old_state & DISPATCH_GROUP_VALUE_MASK) == 0) {
				new_state &= ~DISPATCH_GROUP_HAS_WAITERS;
				new_state &= ~DISPATCH_GROUP_HAS_NOTIFS;
			} else {
				// If the group was entered again since the atomic_add above,
				// we can't clear the waiters bit anymore as we don't know for
				// which generation the waiters are for
				new_state &= ~DISPATCH_GROUP_HAS_NOTIFS;
			}
			if (old_state == new_state) break;
		} while (unlikely(!os_atomic_cmpxchgv2o(dg, dg_state,
				old_state, new_state, &old_state, relaxed)));
		//唤醒
		return _dispatch_group_wake(dg, old_state, true);
	}

	//levea的调用多于enter会crash
	if (unlikely(old_value == 0)) {
		DISPATCH_CLIENT_CRASH((uintptr_t)old_value,
				"Unbalanced call to dispatch_group_leave()");
	}
}

//手动标识要执行一个任务块
void
dispatch_group_enter(dispatch_group_t dg)
{
	// The value is decremented on a 32bits wide atomic so that the carry
	// for the 0 -> -1 transition is not propagated to the upper 32bits.
	//原子性 -1
	uint32_t old_bits = os_atomic_sub_orig2o(dg, dg_bits,
			DISPATCH_GROUP_VALUE_INTERVAL, acquire);
	uint32_t old_value = old_bits & DISPATCH_GROUP_VALUE_MASK;
	if (unlikely(old_value == 0)) {
		_dispatch_retain(dg); // <rdar://problem/22318411>
	}
	//处理后的值等于DISPATCH_GROUP_VALUE_MAX则crash
	if (unlikely(old_value == DISPATCH_GROUP_VALUE_MAX)) {
		DISPATCH_CLIENT_CRASH(old_bits,
				"Too many nested calls to dispatch_group_enter()");
	}
}

DISPATCH_ALWAYS_INLINE
static inline void
_dispatch_group_notify(dispatch_group_t dg, dispatch_queue_t dq,
		dispatch_continuation_t dsn)
{
	uint64_t old_state, new_state;
	dispatch_continuation_t prev;

	dsn->dc_data = dq;
	_dispatch_retain(dq);
    ///应该一些链表的操作,连接上任务block
	prev = os_mpsc_push_update_tail(os_mpsc(dg, dg_notify), dsn, do_next);
	if (os_mpsc_push_was_empty(prev)) _dispatch_retain(dg);
	os_mpsc_push_update_prev(os_mpsc(dg, dg_notify), prev, dsn, do_next);
	if (os_mpsc_push_was_empty(prev)) {
		os_atomic_rmw_loop2o(dg, dg_state, old_state, new_state, release, {
			new_state = old_state | DISPATCH_GROUP_HAS_NOTIFS;
			if ((uint32_t)old_state == 0) {
				//判断是否是0,所以notify在最开始的地方也是可以执行的,如果enter次数多于leave会得不到执行
				os_atomic_rmw_loop_give_up({
					//唤醒
					return _dispatch_group_wake(dg, new_state, false);
				});
			}
		});
	}
}

DISPATCH_NOINLINE
void
dispatch_group_notify_f(dispatch_group_t dg, dispatch_queue_t dq, void *ctxt,
		dispatch_function_t func)
{
	//封装任务执行block为dispatch_continuation_t
	dispatch_continuation_t dsn = _dispatch_continuation_alloc();
	_dispatch_continuation_init_f(dsn, dq, ctxt, func, 0, DC_FLAG_CONSUME);
	_dispatch_group_notify(dg, dq, dsn);
}

#ifdef __BLOCKS__
void
dispatch_group_notify(dispatch_group_t dg, dispatch_queue_t dq,
		dispatch_block_t db)
{
	//封装任务执行block为dispatch_continuation_t
	dispatch_continuation_t dsn = _dispatch_continuation_alloc();
	_dispatch_continuation_init(dsn, dq, db, 0, DC_FLAG_CONSUME);
	_dispatch_group_notify(dg, dq, dsn);
}
#endif

DISPATCH_ALWAYS_INLINE
static inline void
_dispatch_continuation_group_async(dispatch_group_t dg, dispatch_queue_t dq,
		dispatch_continuation_t dc, dispatch_qos_t qos)
{
	dispatch_group_enter(dg);
	dc->dc_data = dg;
	_dispatch_continuation_async(dq, dc, qos, dc->dc_flags);
}

DISPATCH_NOINLINE
void
dispatch_group_async_f(dispatch_group_t dg, dispatch_queue_t dq, void *ctxt,
		dispatch_function_t func)
{
	dispatch_continuation_t dc = _dispatch_continuation_alloc();
	uintptr_t dc_flags = DC_FLAG_CONSUME | DC_FLAG_GROUP_ASYNC;
	dispatch_qos_t qos;

	qos = _dispatch_continuation_init_f(dc, dq, ctxt, func, 0, dc_flags);
	_dispatch_continuation_group_async(dg, dq, dc, qos);
}

#ifdef __BLOCKS__
void
dispatch_group_async(dispatch_group_t dg, dispatch_queue_t dq,
		dispatch_block_t db)
{
	dispatch_continuation_t dc = _dispatch_continuation_alloc();
	uintptr_t dc_flags = DC_FLAG_CONSUME | DC_FLAG_GROUP_ASYNC;
	dispatch_qos_t qos;

	qos = _dispatch_continuation_init(dc, dq, db, 0, dc_flags);
	_dispatch_continuation_group_async(dg, dq, dc, qos);
}
#endif
