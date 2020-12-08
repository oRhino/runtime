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

/*
 * IMPORTANT: This header file describes INTERNAL interfaces to libdispatch
 * which are subject to change in future releases of Mac OS X. Any applications
 * relying on these interfaces WILL break.
 */

#ifndef __DISPATCH_SEMAPHORE_INTERNAL__
#define __DISPATCH_SEMAPHORE_INTERNAL__

#pragma mark - 信号量
struct dispatch_queue_s;

DISPATCH_CLASS_DECL(semaphore, OBJECT);
struct dispatch_semaphore_s {
	DISPATCH_OBJECT_HEADER(semaphore);
	long volatile dsema_value; //信号量的当前值
	long dsema_orig; //信号量的初始值
	_dispatch_sema4_t dsema_sema; //不同平台的实际信号量
};

//展开如下:
// struct dispatch_semaphore_s {
//   struct dispatch_object_s _as_do[0];
//	 struct _os_object_s _as_os_obj[0];
//
//	 const struct dispatch_semaphore_vtable_s *do_vtable; /* must be pointer-sized */
//
//	 int volatile do_ref_cnt;
//	 int volatile do_xref_cnt;
//
//	 struct dispatch_semaphore_s *volatile do_next;
//	 struct dispatch_queue_s *do_targetq;
//	 void *do_ctxt;
//	 void *do_finalizer;
//	long volatile dsema_value; //信号量的当前值
//	long dsema_orig; //信号量的初始值
//	_dispatch_sema4_t dsema_sema; 信号量的结构。
//}



/*
 * Dispatch Group State:
 *
 * Generation (32 - 63):
 *   32 bit counter that is incremented each time the group value reaaches
 *   0 after a dispatch_group_leave. This 32bit word is used to block waiters
 *   (threads in dispatch_group_wait) in _dispatch_wait_on_address() until the
 *   generation changes.
 *
 * Value (2 - 31):
 *   30 bit value counter of the number of times the group was entered.
 *   dispatch_group_enter counts downward on 32bits, and dispatch_group_leave
 *   upward on 64bits, which causes the generation to bump each time the value
 *   reaches 0 again due to carry propagation.
 *
 * Has Notifs (1):
 *   This bit is set when the list of notifications on the group becomes non
 *   empty. It is also used as a lock as the thread that successfuly clears this
 *   bit is the thread responsible for firing the notifications.
 *
 * Has Waiters (0):
 *   This bit is set when there are waiters (threads in dispatch_group_wait)
 *   that need to be woken up the next time the value reaches 0. Waiters take
 *   a snapshot of the generation before waiting and will wait for the
 *   generation to change before they return.
 */
#define DISPATCH_GROUP_GEN_MASK         0xffffffff00000000ULL
#define DISPATCH_GROUP_VALUE_MASK       0x00000000fffffffcULL
#define DISPATCH_GROUP_VALUE_INTERVAL   0x0000000000000004ULL
#define DISPATCH_GROUP_VALUE_1          DISPATCH_GROUP_VALUE_MASK  ///可表示此时 dispatch_group 关联了一个 block
#define DISPATCH_GROUP_VALUE_MAX        DISPATCH_GROUP_VALUE_INTERVAL  /// 可表示 dispatch_group 关联的 block 达到了最大值，正常情况时应小于此值
#define DISPATCH_GROUP_HAS_NOTIFS       0x0000000000000002ULL // 表示dispatch_group是否有notify回调通知的掩码
#define DISPATCH_GROUP_HAS_WAITERS      0x0000000000000001ULL // 对应dispatch_group_wait函数的使用，表示 dispatch_group 是否处于等待状态的掩码

DISPATCH_CLASS_DECL(group, OBJECT);
struct dispatch_group_s {
	DISPATCH_OBJECT_HEADER(group); //一些公共成员<isa,ref_t,do_next...> 抽取成多个宏
	DISPATCH_UNION_LE(uint64_t volatile dg_state,
			uint32_t dg_bits,
			uint32_t dg_gen
	) DISPATCH_ATOMIC64_ALIGN;
	//把所有的notify回调block存进链表中
	struct dispatch_continuation_s *volatile dg_notify_head; //链表头
	struct dispatch_continuation_s *volatile dg_notify_tail; //链表尾
};

DISPATCH_ALWAYS_INLINE
static inline uint32_t
_dg_state_value(uint64_t dg_state)
{
	return (uint32_t)(-((uint32_t)dg_state & DISPATCH_GROUP_VALUE_MASK)) >> 2;
}

DISPATCH_ALWAYS_INLINE
static inline uint32_t
_dg_state_gen(uint64_t dg_state)
{
	return (uint32_t)(dg_state >> 32);
}

dispatch_group_t _dispatch_group_create_and_enter(void);
void _dispatch_group_dispose(dispatch_object_t dou, bool *allow_free);
DISPATCH_COLD
size_t _dispatch_group_debug(dispatch_object_t dou, char *buf,
		size_t bufsiz);

void _dispatch_semaphore_dispose(dispatch_object_t dou, bool *allow_free);
DISPATCH_COLD
size_t _dispatch_semaphore_debug(dispatch_object_t dou, char *buf,
		size_t bufsiz);

#endif
