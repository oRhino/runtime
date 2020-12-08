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

#pragma mark - 信号量
#pragma mark dispatch_semaphore_t
//dispatch_semaphore 是 GCD 中最常见的操作，通常用于保证资源的多线程安全性和控制任务的并发数量。
//其本质实际上是基于mach内核的信号量接口来实现的
//dispatch_semaphore_t 是指向 dispatch_semaphore_s 结构体的指针

//用初始值（long value）创建信号量
dispatch_semaphore_t
dispatch_semaphore_create(long value)
{
	dispatch_semaphore_t dsema; // 指向 dispatch_semaphore_s 结构体的指针

	// If the internal value is negative, then the absolute of the value is
	// equal to the number of waiting threads. Therefore it is bogus to
	// initialize the semaphore with a negative value.
	if (value < 0) {
		return DISPATCH_BAD_INPUT; // 如果value值小于 0，则直接返回0
	}
    // _dispatch_object_alloc 是为 dispatch_semaphore_s 申请空间，然后用 &OS_dispatch_semaphore_class 初始化，
	// &OS_dispatch_semaphore_class 设置了 dispatch_semaphore_t 的相关回调函数，如销毁函数 _dispatch_semaphore_dispose 等
	dsema = _dispatch_object_alloc(DISPATCH_VTABLE(semaphore),
			sizeof(struct dispatch_semaphore_s));
	dsema->do_next = DISPATCH_OBJECT_LISTLESS; // 表示链表的下一个节点
	dsema->do_targetq = _dispatch_get_default_queue(false); // 目标队列（从全局的队列数组 _dispatch_root_queues 中取默认队列）
	dsema->dsema_value = value; // 当前值（当前是初始值）
	_dispatch_sema4_init(&dsema->dsema_sema, _DSEMA4_POLICY_FIFO);//初始化系统信号量<不同平台不同>
	dsema->dsema_orig = value; // 初始值
	return dsema;
}

//信号量的销毁函数
void
_dispatch_semaphore_dispose(dispatch_object_t dou,
		DISPATCH_UNUSED bool *allow_free)
{
	dispatch_semaphore_t dsema = dou._dsema;

	// 容错判断，如果当前dsema_value小于dsema_orig，表示信号量还正在使用，不能进行销毁，
	// 如下代码会导致此crash:
	// dispatch_semaphore_t sema = dispatch_semaphore_create(1); // 创建 value = 1，orig = 1
	// dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER); // value = 0，orig = 1
	// sema = dispatch_semaphore_create(1); // 重新赋值或者置为nil导致原始 dispatch_semaphore_s 释放，但是此时orig是1，value是0则造成 crash

	if (dsema->dsema_value < dsema->dsema_orig) {
		DISPATCH_CLIENT_CRASH(dsema->dsema_orig - dsema->dsema_value,
				"Semaphore object deallocated while in use");
	}
    // 销毁信号量
	// MACH: semaphore_destroy
	// POSIX: sem_destroy
	// WIN32: CloseHandle
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
	/// 唤醒一条线程
	/// 不同平台因为使用的不同的信号量,所以唤醒的API不同
	/// MACH:  semaphore_signal
	/// POSIX: sem_post
	/// WIN32: ReleaseSemaphore
	_dispatch_sema4_signal(&dsema->dsema_sema, 1);
	return 1;
}

// 使信号量原子+1。如果先前的值小于零，则此函数在返回之前唤醒等待的线程。如果线程被唤醒，此函数将返回非零值。否则，返回零。
long
dispatch_semaphore_signal(dispatch_semaphore_t dsema)
{
	/// 将dsema的成员变量dsema_value的值原子加1
	long value = os_atomic_inc2o(dsema, dsema_value, release);//os_atomic_inc2o对原子操作+1的封装。
	if (likely(value > 0)) {
		// 如果value大于0表示目前没有线程需要唤醒，直接return0
		return 0;
	}
	/// 如果过度释放，导致value的值一直增加到LONG_MIN溢出，则crash.
	if (unlikely(value == LONG_MIN)) {
		DISPATCH_CLIENT_CRASH(value,
				"Unbalanced call to dispatch_semaphore_signal()");
	}
	/// value小于等于0时，表示目前有线程需要唤醒,调用内核去唤醒等待中的线程
	return _dispatch_semaphore_signal_slow(dsema);
}


DISPATCH_NOINLINE
static long
_dispatch_semaphore_wait_slow(dispatch_semaphore_t dsema,
		dispatch_time_t timeout)
{
	long orig;
    /// 为 &dsema->dsema_sema 赋值
	_dispatch_sema4_create(&dsema->dsema_sema, _DSEMA4_POLICY_FIFO);
	/// 如果timeout是一个特定时间的话,调用 _dispatch_sema4_timedwait进行timeout时间的等待
	switch (timeout) {
	default:
		if (!_dispatch_sema4_timedwait(&dsema->dsema_sema, timeout)) {
			break; //mach: semaphore_timedwait | POSIX: sem_timedwait | win32 :  WaitForSingleObject
		}
		// Fall through and try to undo what the fast path did to
		// dsema->dsema_value
	case DISPATCH_TIME_NOW: //如果timeout参数是DISPATCH_TIME_NOW
		orig = dsema->dsema_value;
		while (orig < 0) {
			/// dsema_value加1抵消掉dispatch_semaphore_wait函数中的减1操作
			if (os_atomic_cmpxchgvw2o(dsema, dsema_value, orig, orig + 1,
					&orig, relaxed)) {
				/// 返回超时
				return _DSEMA4_TIMEOUT();
			}
		}
		// Another thread called semaphore_signal().
		// Fall through and drain the wakeup.
	/// 如果timeout参数是 DISPATCH_TIME_FOREVER的话调用 _dispatch_sema4_wait 一直等待，直到得到signal信号
	case DISPATCH_TIME_FOREVER:
			// MACH :调用了mach内核的信号量接口semaphore_wait进行wait操作
			// POSIX : sem_wait
			// WIN32 : WaitForSingleObject
		_dispatch_sema4_wait(&dsema->dsema_sema);
		break;
	}
	return 0;
}
//信号量原子-1
long
dispatch_semaphore_wait(dispatch_semaphore_t dsema, dispatch_time_t timeout)
{
	/// dsema的成员变量dsema_value的值原子减1
	long value = os_atomic_dec2o(dsema, dsema_value, acquire);
	if (likely(value >= 0)) {
		return 0; /// 如果减1后仍然大于等于0，则直接return
	}
	/// 如果小于0，则调用_dispatch_semaphore_wait_slow函数进行阻塞等待
	return _dispatch_semaphore_wait_slow(dsema, timeout);
}

#pragma mark - 组队列
#pragma mark dispatch_group_t

/*
 在dispatch_group 进行进组出组操作每次是用加减4 （DISPATCH_GROUP_VALUE_INTERVAL）来记录的，
 //并不是常见的加1减1，然后起始值是从uint32_t的最小值0开始的，这里用了一个无符号数和有符号数的转换的小技巧，例如 dispatch_group 起始状态时 uint32_t 类型的dg_bits值为 0，然后第一个enter操作进来以后，把uint32_t类型的dg_bits从0减去4，然后-4转换为uint32_t类型后值为4294967292,然后leave 操作时dg_bits加4，即4294967292加4，这样会使uint32_t类型值溢出然后dg_bits值就变回0（uint32_t 类型的最小值），对应到 dispatch_group 中的逻辑原理即表示dg_bits达到临界值了，表示与组关联的block都执行完成了，可以执行后续的唤醒操作了。
 dg_bits 使用 32 bit 空间对应使用 uint32_t 类型，然后 DISPATCH_GROUP_VALUE_INTERVAL（间隔）用 4 是因为 uint32_t 类型表示的数字个数刚好是 4 的整数倍吗，不过只要是 2 的幂都是整数倍，且 uint32_t 类型的数字即使以 4 为间隔表示的数字个数也完全足够使用了， 这里的还包括了掩码的使用，4 的二进制表示时后两位是 0，正好可以用来表示两个掩码位，仅后两位是 1 时分别对应 DISPATCH_GROUP_HAS_NOTIFS 和 DISPATCH_GROUP_HAS_WAITERS 两个宏.
 
 */
DISPATCH_ALWAYS_INLINE
static inline dispatch_group_t
_dispatch_group_create_with_count(uint32_t n)
{
	//typedef struct dispatch_group_s *dispatch_group_t;
	// 分配内存空间 DISPATCH_VTABLE(name)层层嵌套的宏定义 最终为:OS_dispatch_group_class
	// _dispatch_object_alloc 是为 dispatch_group_s 申请空间，然后用 &OS_dispatch_group_class 初始化，
	// &OS_dispatch_group_class 设置了 dispatch_group_t 的相关回调函数，如销毁函数 _dispatch_group_dispose 等。

	dispatch_group_t dg = _dispatch_object_alloc(DISPATCH_VTABLE(group),
			sizeof(struct dispatch_group_s));
	dg->do_next = DISPATCH_OBJECT_LISTLESS;
	//设置目标队列
	/*
	_dispatch_get_default_queue(overcommit) \
			_dispatch_root_queues[DISPATCH_ROOT_QUEUE_IDX_DEFAULT_QOS + \
					!!(overcommit)]._as_dq
	 */
	//// 目标队列（从全局的根队列数组 _dispatch_root_queues 中取默认 QOS 的队列）
	dg->do_targetq = _dispatch_get_default_queue(false);
	if (n) { // 0  表示不执行 // n 表示 dg 关联的 block 数量。
		//os_atomic_store(&(p)->f, (v), m)
		//把v 存入dg.dg_bits
		//把 -1 转换为 uint32_t 后再转换为 ULL（无符号 long long） 然后乘以 0x0000000000000004ULL 后再强转为 uint32_t（也可以理解为 -4 转换为 uint32_t）
		os_atomic_store2o(dg, dg_bits,
				(uint32_t)-n * DISPATCH_GROUP_VALUE_INTERVAL, relaxed);
		//把1 存入do_ref_cnt 引用计数为 1，即目前有与组关联的 block 或者有任务进组了
		os_atomic_store2o(dg, do_ref_cnt, 1, relaxed); // <rdar://22318411>
	}
	return dg;
}
//用于创建可与block关联的dispatch_group_s结构体实例，此dispatch_group_s结构体实例可用于等待与它关联的所有block的异步执行完成。
dispatch_group_t
dispatch_group_create(void)
{  //入参为 0，表明目前没有block关联dispatch_group
	return _dispatch_group_create_with_count(0);
}

dispatch_group_t
_dispatch_group_create_and_enter(void)
{  //入参为 1，表明有一个 block 关联 dispatch_group 操作
	return _dispatch_group_create_with_count(1);
}

//销毁
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
	//// for 死循环，等待内部的条件满足时 return，否则一直进行死循环
	for (;;) {
		// 比较等待，内部是根据指定的时间进行时间等待，并根据 &dg->dg_gen 值判断是否关联的 block 都异步执行完毕了。
		// 这里牵涉到 dg_state 的进位，当 dg_bits 溢出时会进位到 dg_gen 中，此时 dg_gen 不再是 0，可表示关联的 block 都执行完毕了。

		int rc = _dispatch_wait_on_address(&dg->dg_gen, gen, timeout, 0);
		
		//// 表示 dispatch_group 关联的 block 都异步执行完毕了，return 0
		if (likely(gen != os_atomic_load2o(dg, dg_gen, acquire))) {
			return 0;
		}
		//// 等到超过指定时间了，return _DSEMA4_TIMEOUT() 超时
		if (rc == ETIMEDOUT) {
			return _DSEMA4_TIMEOUT();
		}
	}
}

//同步等待直到与 dispatch_group 关联的所有 block 都异步执行完成或者直到指定的超时时间过去为止，才会返回。
//如果没有与 dispatch_group 关联的 block，则此函数将立即返回。
//从多个线程同时使用同一 dispatch_group 调用此函数的结果是不确定的。
//成功返回此函数后，dispatch_group 关联的 block 为空，可以使用 dispatch_release 释放 dispatch_group，也可以将其重新用于其它 block。

//timeout：指定何时超时（dispatch_time_t）。有 DISPATCH_TIME_NOW 和 DISPATCH_TIME_FOREVER 常量。
//result：成功返回零（与该组关联的所有块在指定的时间内完成），错误返回非零（即超时）。
long
dispatch_group_wait(dispatch_group_t dg, dispatch_time_t timeout)
{
	uint64_t old_state, new_state;
	// os_atomic_rmw_loop2o 宏定义，内部是一个 do while 循环，
	// 每次循环都从本地原子取值，判断 dispatch_group 所处的状态，
	// 是否关联的 block 都异步执行完毕了
	os_atomic_rmw_loop2o(dg, dg_state, old_state, new_state, relaxed, {
		// #define DISPATCH_GROUP_VALUE_MASK   0x00000000fffffffcULL
		// 表示关联的 block 为 0 或者关联的 block 都执行完毕了，则直接 return 0，
		//（函数返回，停止阻塞当前线程。）

		if ((old_state & DISPATCH_GROUP_VALUE_MASK) == 0) {
			//// 跳出循环并返回 0
			os_atomic_rmw_loop_give_up_with_fence(acquire, return 0);
		}
		
		// 如果 timeout 等于 0，则立即跳出循环并返回 _DSEMA4_TIMEOUT()，
		// 指定等待时间为 0，则函数返回，并返回超时提示，
		//（继续向下执行，停止阻塞当前线程。）
		if (unlikely(timeout == 0)) {
			//// 跳出循环并返回 _DSEMA4_TIMEOUT() 超时
			os_atomic_rmw_loop_give_up(return _DSEMA4_TIMEOUT());
		}
		
		///// #define DISPATCH_GROUP_HAS_WAITERS   0x0000000000000001ULL
		new_state = old_state | DISPATCH_GROUP_HAS_WAITERS;
		/// 表示目前需要等待，至少等到关联的 block 都执行完毕或者等到指定时间超时
		if (unlikely(old_state & DISPATCH_GROUP_HAS_WAITERS)) {
			os_atomic_rmw_loop_give_up(break);
		}
	});

	return _dispatch_group_wait_slow(dg, _dg_state_gen(new_state), timeout);
}

//把notify 回调函数链表中的所有的函数提交到指定的队列中异步执行，needs_release 表示是否需要释放所有关联 block 异步执行完成、所有的 notify 回调函数执行完成的 dispatch_group 对象。dg_state 则是 dispatch_group 的状态，包含目前的关联的 block 数量等信息。

DISPATCH_NOINLINE
static void
_dispatch_group_wake(dispatch_group_t dg, uint64_t dg_state, bool needs_release)
{
	/// dispatch_group 对象的引用计数是否需要 -1
	uint16_t refs = needs_release ? 1 : 0; // <rdar://problem/22318411>

	// #define DISPATCH_GROUP_HAS_NOTIFS   0x0000000000000002ULL // 用来判断 dispatch_group 是否存在 notify 函数的掩码
	// 这里如果 dg_state & 0x0000000000000002ULL 结果不为 0，即表示 dg 存在 notify 回调函数

	if (dg_state & DISPATCH_GROUP_HAS_NOTIFS) {
		dispatch_continuation_t dc, next_dc, tail;

		// Snapshot before anything is notified/woken <rdar://problem/8554546>
		//// 取出 dg 的 notify 回调函数链表的头
		dc = os_mpsc_capture_snapshot(os_mpsc(dg, dg_notify), &tail);
		do {
			//// 取出dc创建时指定的队列，对应 _dispatch_group_notify 函数中的 dsn->dc_data = dq 赋值操作
			dispatch_queue_t dsn_queue = (dispatch_queue_t)dc->dc_data;
			// 取得下一个节点
			next_dc = os_mpsc_pop_snapshot_head(dc, tail, do_next);
			// 根据各队列的优先级异步执行 notify 链表中的函数
			_dispatch_continuation_async(dsn_queue, dc,
					_dispatch_qos_from_pp(dc->dc_priority), dc->dc_flags);
			// 释放 notify 函数执行时的队列 dsn_queue（os_obj_ref_cnt - 1）
			_dispatch_release(dsn_queue);
			
			// 当 next_dc 为 NULL 时，跳出循环
		} while ((dc = next_dc));

		// 这里的 refs 计数增加 1 正对应了 _dispatch_group_notify 函数中，
		// 当第一次给 dispatch_group 添加 notify 函数时的引用计数加 1，_dispatch_retain(dg)
		// 代码执行到这里时 dg 的所有 notify 函数都执行完毕了。
		//（统计 dispatch_group 的引用计数需要减小的值）
		refs++;
	}

	//// 根据 &dg->dg_gen 的值判断是否处于阻塞状态
	if (dg_state & DISPATCH_GROUP_HAS_WAITERS) {
		_dispatch_wake_by_address(&dg->dg_gen);
	}

	// 根据 refs 判断是否需要释放 dg（执行 os_obj_ref_cnt - refs），当 os_obj_ref_cnt 的值小于 0 时，可销毁 dg。
	// 如果 needs_release 为真，并且 dg 有 notify 函数时，会执行 os_obj_ref_cnt - 2
	// 如果 needs_release 为假，但是 dg 有 notify 函数时，会执行 os_obj_ref_cnt - 1
	// 如果 needs_release 为假，且 dg 无 notify 函数时，不执行操作

	if (refs) _dispatch_release_n(dg, refs);
}

//手动指示dispatch_group中的一个关联block已完成，或者说是一个block已解除关联。
//调用此函数表示一个关联 block 已完成，并且已通过 dispatch_group_async 以外的方式与 dispatch_group 解除了关联。
void
dispatch_group_leave(dispatch_group_t dg)
{
	// The value is incremented on a 64bits wide atomic so that the carry for
	// the -1 -> 0 transition increments the generation atomically.
	// 以原子方式增加 dg_state 的值，dg_bits 的内存空间是 dg_state 的低 32 bit，
	// 所以 dg_state + DISPATCH_GROUP_VALUE_INTERVAL 没有进位到 33 bit 时都可以理解为是 dg_bits + DISPATCH_GROUP_VALUE_INTERVAL。

	//（这里注意是把 dg_state 的旧值同时赋值给了 new_state 和 old_state 两个变量）
	uint64_t new_state, old_state = os_atomic_add_orig2o(dg, dg_state,
			DISPATCH_GROUP_VALUE_INTERVAL, release);
	
	// #define DISPATCH_GROUP_VALUE_MASK   0x00000000fffffffcULL ➡️ 0b0000...11111100ULL
	// #define DISPATCH_GROUP_VALUE_1   DISPATCH_GROUP_VALUE_MASK
		
	// dg_state的旧值和DISPATCH_GROUP_VALUE_MASK进行与操作掩码取值，如果此时仅关联了一个block的话那么dg_state 的旧值就是（十六进制：0xFFFFFFFC）
	//（那么上面的 os_atomic_add_orig2o 执行后，dg_state 的值是 0x0000000100000000ULL，
	// 因为它是 uint64_t 类型它会从最大的uint32_t继续进位，而不同于dg_bits的uint32_t 类型溢出后为 0）
	// 如果dg_state旧值old_state等于 0xFFFFFFFC则和DISPATCH_GROUP_VALUE_MASK 与操作结果还是 0xFFFFFFFC
	
	uint32_t old_value = (uint32_t)(old_state & DISPATCH_GROUP_VALUE_MASK);

	// 如果 old_value 的值是 DISPATCH_GROUP_VALUE_1。
	// old_state 是 0x00000000fffffffcULL，DISPATCH_GROUP_VALUE_INTERVAL 的值是 0x0000000000000004ULL
	// 所以这里 old_state 是 uint64_t 类型，加 DISPATCH_GROUP_VALUE_INTERVAL 后不会发生溢出会产生正常的进位，old_state = 0x0000000100000000ULL

	if (unlikely(old_value == DISPATCH_GROUP_VALUE_1)) {
		old_state += DISPATCH_GROUP_VALUE_INTERVAL;
		do {
			/// new_state = 0x0000000100000000ULL
			new_state = old_state;
			if ((old_state & DISPATCH_GROUP_VALUE_MASK) == 0) {
				// 如果目前是仅关联了一个 block 而且是正常的 enter 和 leave 配对执行，则会执行这里。
								
				// 清理 new_state 中对应 DISPATCH_GROUP_HAS_WAITERS 的非零位的值，
			    // 即把 new_state 二进制表示的倒数第一位置 0

				new_state &= ~DISPATCH_GROUP_HAS_WAITERS;
				// 清理 new_state 中对应 DISPATCH_GROUP_HAS_NOTIFS 的非零位的值，
				// 即把 new_state 二进制表示的倒数第二位置 0
				new_state &= ~DISPATCH_GROUP_HAS_NOTIFS;
			} else {
				// If the group was entered again since the atomic_add above,
				// we can't clear the waiters bit anymore as we don't know for
				// which generation the waiters are for
				// 清理 new_state 中对应 DISPATCH_GROUP_HAS_NOTIFS 的非零位的值，
				// 即把 new_state 二进制表示的倒数第二位置 0
				new_state &= ~DISPATCH_GROUP_HAS_NOTIFS;
			}
			// 如果目前是仅关联了一个 block 而且是正常的 enter 和 leave 配对执行，则会执行这里的 break，
			// 结束 do while 循环，执行下面的 _dispatch_group_wake 函数，唤醒异步执行 dispatch_group_notify 添加到指定队列中的回调通知。

			if (old_state == new_state) break;
			// 比较 dg_state 和 old_state 的值，如果相等则把 dg_state 的值存入 new_state 中，并返回 true，如果不相等则把 dg_state 的值存入 old_state 中，并返回 false。
			// unlikely(!os_atomic_cmpxchgv2o(dg, dg_state, old_state, new_state, &old_state, relaxed)) 表达式值为 false 时才会结束循环，否则继续循环，
			// 即 os_atomic_cmpxchgv2o(dg, dg_state, old_state, new_state, &old_state, relaxed) 返回 true 时才会结束循环，否则继续循环，
			// 即 dg_state 和 old_state 的值相等时才会结束循环，否则继续循环。
			
			//（正常 enter 和 leave 的话，此时 dg_state 和 old_state 的值都是 0x0000000100000000ULL，会结束循环）

		} while (unlikely(!os_atomic_cmpxchgv2o(dg, dg_state,
				old_state, new_state, &old_state, relaxed)));
		//// 唤醒异步执行 dispatch_group_notify 添加到指定队列中的回调通知
		return _dispatch_group_wake(dg, old_state, true);
	}

	// 如果 old_value 为 0，而上面又进行了一个 dg_state + DISPATCH_GROUP_VALUE_INTERVAL 操作，此时就过度 leave 了，则 crash，
	// 例如创建好一个 dispatch_group 后直接调用 dispatch_group_leave 函数即会触发这个 crash。

	if (unlikely(old_value == 0)) {
		DISPATCH_CLIENT_CRASH((uintptr_t)old_value,
				"Unbalanced call to dispatch_group_leave()");
	}
}

//手动标识要执行一个任务块
//表示一个block与dispatch_group关联，同时block执行完后要调用dispatch_group_leave表示解除关联，否则dispatch_group_s会永远等下去。
void
dispatch_group_enter(dispatch_group_t dg)
{
	// The value is decremented on a 32bits wide atomic so that the carry
	// for the 0 -> -1 transition is not propagated to the upper 32bits.
	//// dg_bits 是无符号32位int，-1和0的转换在32位int范围内，不会过渡到高位，影响dg_gen和dg_state的值
	//// dg_bits 以原子方式减少DISPATCH_GROUP_VALUE_INTERVAL，并返回dg_bits的旧值，表示dispatch_group增加了一个关联block
	uint32_t old_bits = os_atomic_sub_orig2o(dg, dg_bits,
			DISPATCH_GROUP_VALUE_INTERVAL, acquire);
	// #define DISPATCH_GROUP_VALUE_MASK   0x00000000fffffffcULL 二进制表示 ➡️ 0b0000...11111100ULL
	// 拿 old_bits 和 DISPATCH_GROUP_VALUE_MASK 进行与操作，取出 dg_bits 的旧值，
	// old_bits 的二进制表示的后来两位是其它作用的掩码标记位，需要做这个与操作把它们置为 0，
	// old_value 可用来判断这次enter之前 dispatch_group 内部是否关联过 block。
	
	uint32_t old_value = old_bits & DISPATCH_GROUP_VALUE_MASK;
	if (unlikely(old_value == 0)) {
		// 表示此时调度组由未关联任何block的状态变换到了关联了一个 block 的状态，
		// 调用 _dispatch_retain把dg的内部引用计数+1表明dg目前正在被使用，不能进行销毁。
				
		//（表示 dispatch_group 内部有 block 没执行完成即调度组正在被使用，
		// 此时 dispatch_group 不能进行释放，想到前面的信号量，
		// 如果 dsema_value 小于 dsema_orig 表示信号量实例正在被使用，此时释放信号量实例的话也会导致 crash，
		// 整体思想和我们的 NSObject 的引用计数原理是相同的，不同之处是内存泄漏不一定会 crash，而这里则是立即 crash，
	    // 当然作为一名合格的开发绝对不能容许任何内存泄漏和崩溃 ！！！！）

		_dispatch_retain(dg); // <rdar://problem/22318411> //GCD 对象的引用计数加 1（os_obj_ref_cnt 的值）
	}
	
	// #define DISPATCH_GROUP_VALUE_INTERVAL   0x0000000000000004ULL 二进制表示 ➡️ 0b0000...00000100ULL
	// #define DISPATCH_GROUP_VALUE_MAX   DISPATCH_GROUP_VALUE_INTERVAL
	   
	// 如果 old_bits & DISPATCH_GROUP_VALUE_MASK 的结果等于 DISPATCH_GROUP_VALUE_MAX，即 old_bits 的值是 DISPATCH_GROUP_VALUE_INTERVAL。
	// 这里可以理解为上面 4294967292 每次减 4，一直往下减，直到溢出...
	// 表示dispatch_group_enter函数过度调用，则 crash。
	// DISPATCH_GROUP_VALUE_MAX = 0 + DISPATCH_GROUP_VALUE_INTERVAL;


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

	//// dispatch_continuation_t 的 dc_data 成员变量被赋值为 dispatch_continuation_s 执行时所在的队列
	dsn->dc_data = dq;
	// dq 队列引用计数 +1，因为有新的 dsn 要在这个 dq 中执行了（`os_obj_ref_cnt` 的值 +1）
	_dispatch_retain(dq);
	//    prev =  ({
	//        // 以下都是原子操作:
	//        _os_atomic_basetypeof(&(dg)->dg_notify_head) _tl = (dsn); // 类型转换
	//        // 把 dsn 的 do_next 置为 NULL，防止错误数据
	//        os_atomic_store(&(_tl)->do_next, (NULL), relaxed);
	//        // 入参 dsn 存储到 dg 的成员变量 dg_notify_tail 中，并返回之前的旧的 dg_notify_tail
	//        atomic_exchange_explicit(_os_atomic_c11_atomic(&(dg)->dg_notify_tail), _tl, memory_order_release);
	//    });

	// 把dsn存储到dg的dg_notify_tail 成员变量中，并返回之前的旧dg_notify_tail，
	// 这个dg_notify_tail是一个指针,用来指向dg的notify回调函数链表的尾节点。
	prev = os_mpsc_push_update_tail(os_mpsc(dg, dg_notify), dsn, do_next);
	
	
	// #define os_mpsc_push_was_empty(prev) ((prev) == NULL)
	// 如果prev 为 NULL，表示dg是第一次添加notify回调函数，则再次增加dg的引用计数（os_obj_ref_cnt + 1），
	// 前面我们还看到dg在第一次执行enter时也会增加一次引用计数（os_obj_ref_cnt + 1）。
	if (os_mpsc_push_was_empty(prev)) _dispatch_retain(dg);
	
	//    ({
	//        // prev 是指向 notify 回调函数链表的尾节点的一个指针
	//        _os_atomic_basetypeof(&(dg)->dg_notify_head) _prev = (prev);
	//        if (likely(_prev)) {
	//            // 如果之前的尾节点存在，则把 dsn 存储到之前尾节点的 do_next 中，即进行了链表拼接
	//            (void)os_atomic_store(&(_prev)->do_next, ((dsn)), relaxed);
	//        } else {
	//            // 如果之前尾节点不存在，则表示链表为空，则 dsn 就是头节点了，并存储到 dg 的 dg_notify_head 成员变量中
	//            (void)os_atomic_store(&(dg)->dg_notify_head, (dsn), relaxed);
	//        }
	//    });

	// 把dsn拼接到dg的notify回调函数链表中，或者是第一次的话，则把dsn作为notify回调函数链表的头节点
	os_mpsc_push_update_prev(os_mpsc(dg, dg_notify), prev, dsn, do_next);
	
	if (os_mpsc_push_was_empty(prev)) {
		// 如果 prev 不为 NULL 的话，表示 dg 有 notify 回调函数存在。
			
		// os_atomic_rmw_loop2o 是一个宏定义，内部包裹了一个 do while 循环，
		// 直到 old_state == 0 时跳出循环执行 _dispatch_group_wake 函数唤醒执行 notify 链表中的回调通知，
		// 即对应我们上文中的 dispatch_group_leave 函数中 dg_bits 的值回到 0 表示 dispatch_group 中关联的 block 都执行完了。
		
		// 大概逻辑是这样，这里不再拆开宏定义分析了，具体拆开如下面的 os_atomic_rmw_loop2o 宏分析，
		// 实在太杀时间了，低头一小时，抬头一小时...😭😭
		
		// 只要记得这里是用一个 do while 循环等待，每次循环以原子方式读取状态值（dg_bits），
		// 直到 0 状态，去执行 _dispatch_group_wake 唤醒函数把 notify 链表中的函数提交到指定的队列异步执行就好了！⛽️⛽️

		os_atomic_rmw_loop2o(dg, dg_state, old_state, new_state, release, {
			
			// #define DISPATCH_GROUP_HAS_NOTIFS   0x0000000000000002ULL
			// 这里挺重要的一个点，把 new_state 的二进制表示的倒数第二位置为 1，
			// 表示 dg 存在 notify 回调函数。
			new_state = old_state | DISPATCH_GROUP_HAS_NOTIFS;
			if ((uint32_t)old_state == 0) {
				//判断是否是0,所以notify在最开始的地方也是可以执行的,如果enter次数多于leave会得不到执行
				//// 跳出循环执行_dispatch_group_wake 函数，把notify回调函数链表中的任务提交到指定的队列中执行
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
//当与 dispatch_group 相关联的所有 block 都已完成时，计划将 db 提交到队列 dq（即当与 dispatch_group 相关联的所有 block 都已完成时，notify 添加的回调通知将得到执行）。如果没有 block 与 dispatch_group 相关联，则通知块 db 将立即提交。
//通知块 db 提交到目标队列 dq 时，该 dispatch_group 关联的 block 将为空，或者说只有该 dispatch_group 关联的 block 为空时，通知块 db 才会提交到目标队列 dq。此时可以通过 dispatch_release 释放 dispatch_group，也可以重新用于其他操作。
//dispatch_group_notify 函数不会阻塞当前线程，此函数会立即返回，如果我们想阻塞当前线程，想要等 dispatch_group 中关联的 block 全部执行完成后才执行接下来的操作时，可以使用 dispatch_group_wait 函数并指定具体的等待时间（DISPATCH_TIME_FOREVER）。

void
dispatch_group_notify(dispatch_group_t dg, dispatch_queue_t dq,
		dispatch_block_t db)
{
	// 从缓存中取一个 dispatch_continuation_t 或者新建一个 dispatch_continuation_t 返回
	dispatch_continuation_t dsn = _dispatch_continuation_alloc();
	// 配置 dsn，即用 dispatch_continuation_s 封装 db。（db 转换为函数）
	_dispatch_continuation_init(dsn, dq, db, 0, DC_FLAG_CONSUME);
	// 调用 _dispatch_group_notify 函数
	_dispatch_group_notify(dg, dq, dsn);
}
#endif

//首先调用 enter 表示 block 与 dispatch_group 建立关联，然后把 dispatch_group 赋值给 dispatch_continuation 的 dc_data 成员变量，这里的用途是当执行完 dispatch_continuation 中的函数后从 dc_data 中读取到 dispatch_group，然后对此 dispatch_group 进行一次出组 leave 操作
DISPATCH_ALWAYS_INLINE
static inline void
_dispatch_continuation_group_async(dispatch_group_t dg, dispatch_queue_t dq,
		dispatch_continuation_t dc, dispatch_qos_t qos)
{
	//// 调用 dispatch_group_enter 表示与一个 block 建立关联
	dispatch_group_enter(dg);
	// 把 dg 赋值给了 dc 的 dc_data 成员变量，当 dc 中的函数执行完成后，从 dc_data 中读出 dg 执行 leave 操作，正是和上面的 enter 操作对应。
	dc->dc_data = dg;
	//// 在指定队列中异步执行 dc
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
//dispatch_group_async函数与block关联，其实它是在内部封装了一对enter和leave操作。
//dispatch_group_async 将一个 block 提交到指定的调度队列并进行异步调用，并将该 block 与给定的 dispatch_group 关联（其内部自动插入了 dispatch_group_enter 和 dispatch_group_leave 操作，相当于 dispatch_async 和 dispatch_group_enter、dispatch_group_leave 三个函数的一个封装）
void
dispatch_group_async(dispatch_group_t dg, dispatch_queue_t dq,
		dispatch_block_t db)
{
	//把入参 block db 封装成 dispatch_continuation_t  dc 的过程中，会把 dc_flags 设置为 DC_FLAG_CONSUME | DC_FLAG_GROUP_ASYNC，这里的 DC_FLAG_GROUP_ASYNC 标志关系到 dc 执行的时候调用的具体函数（这里的提交的任务的 block 和 dispatch_group 关联的点就在这里，dc 执行时会调用 _dispatch_continuation_with_group_invoke(dc)，而我们日常使用的 dispatch_async 函数提交的异步任务的 block 执行的时候调用的是 _dispatch_client_callout(dc->dc_ctxt, dc->dc_func) 函数，它们正是根据 dc_flags 中的 DC_FLAG_GROUP_ASYNC

	// 从缓存中取一个 dispatch_continuation_t 或者新建一个 dispatch_continuation_t 返回赋值给 dc。
	dispatch_continuation_t dc = _dispatch_continuation_alloc();
	// 这里的 DC_FLAG_GROUP_ASYNC 的标记很重要，是它标记了 dispatch_continuation 中的函数异步执行时具体调用哪个函数。
	uintptr_t dc_flags = DC_FLAG_CONSUME | DC_FLAG_GROUP_ASYNC;
	// 优先级
	dispatch_qos_t qos;
	// 配置 dsn，（db block 转换为函数）
	qos = _dispatch_continuation_init(dc, dq, db, 0, dc_flags);
	// 调用 _dispatch_continuation_group_async 函数异步执行提交到 dq 的 db
	_dispatch_continuation_group_async(dg, dq, dc, qos);
}
#endif
