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

#ifndef __DISPATCH_SOURCE__
#define __DISPATCH_SOURCE__

#ifndef __DISPATCH_INDIRECT__
#error "Please #include <dispatch/dispatch.h> instead of this file directly."
#include <dispatch/base.h> // for HeaderDoc
#endif

#if TARGET_OS_MAC
#include <mach/port.h>
#include <mach/message.h>
#endif

#if !defined(_WIN32)
#include <sys/signal.h>
#endif

DISPATCH_ASSUME_NONNULL_BEGIN

/*!
 * @header
 * The dispatch framework provides a suite of interfaces for monitoring low-
 * level system objects (file descriptors, Mach ports, signals, VFS nodes, etc.)
 * for activity and automatically submitting event handler blocks to dispatch
 * queues when such activity occurs.
 *
 * This suite of interfaces is known as the Dispatch Source API.
 * dispatch framework 提供了一套接口，用于监视低级系统对象（file descriptors（文件描述符）, Mach ports, signals, VFS nodes, etc.）的活动，并在此类活动发生时自动向 dispatch queues 提交事件处理程序块（event handler blocks）。这套接口称为 Dispatch Source API

 *
 */

/*!
 * @typedef dispatch_source_t
 *
 * @abstract
 * Dispatch sources are used to automatically submit event handler blocks to
 * dispatch queues in response to external events.
 */
//dispatch_source_t 表示 dispatch sources 类型，调度源（dispatch sources）用于自动提交事件处理程序块（event handler blocks）到调度队列（dispatch queues）以响应外部事件。
/*
在 Swift（在 Swift 中使用 Objective-C）下宏定义展开是:

OS_EXPORT OS_OBJECT_OBJC_RUNTIME_VISIBLE
@interface OS_dispatch_source : OS_dispatch_object
- (instancetype)init OS_SWIFT_UNAVAILABLE("Unavailable in Swift");
@end

typedef OS_dispatch_source * dispatch_source_t

@protocol OS_dispatch_source <NSObject>
@end

@interface OS_dispatch_source () <OS_dispatch_source>
@end

 OS_dispatch_source 是继承自 OS_dispatch_object 的类，然后 dispatch_source_t 是一个指向 OS_dispatch_source 的指针。

在 Objective-C 下宏定义展开是:

@protocol OS_dispatch_source <OS_dispatch_object>
@end
typedef NSObject<OS_dispatch_source> * dispatch_source_t;

 OS_dispatch_source 是继承自 OS_dispatch_object 协议的协议，并且为遵循该协议的 NSObject 实例对象类型的指针定义了一个 dispatch_source_t 的别名。

在 C++ 下宏定义展开是:

typedef struct dispatch_source_s : public dispatch_object_s {} * dispatch_source_t;

 dispatch_source_t 是一个指向 dispatch_source_s 结构体的指针。

在 C（Plain C）下宏定义展开是:

typedef struct dispatch_source_s *dispatch_source_t

 dispatch_source_t 是指向 struct dispatch_source_s 的指针。
*/

DISPATCH_SOURCE_DECL(dispatch_source);

__BEGIN_DECLS

/*!
 * @typedef dispatch_source_type_t
 *
 * @abstract
 * Constants of this type represent the class of low-level system object that
 * is being monitored by the dispatch source. Constants of this type are
 * passed as a parameter to dispatch_source_create() and determine how the
 * handle argument is interpreted (i.e. as a file descriptor, mach port,
 * signal number, process identifier, etc.), and how the mask argument is
 * interpreted.
 * 定义类型别名。此类型的常量表示调度源（dispatch source）正在监视的低级系统对象的类（class of low-level system object）。此类型的常量作为参数传递给 dispatch_source_create 函数并确定如何解释 handle 参数（handle argument ）（i.e. as a file descriptor（文件描述符）, mach port, signal number, process identifier, etc.）以及如何解释 mask 参数（mask argument）。
 */
typedef const struct dispatch_source_type_s *dispatch_source_type_t;

/*!
 * @const DISPATCH_SOURCE_TYPE_DATA_ADD
 * @discussion A dispatch source that coalesces data obtained via calls to
 * dispatch_source_merge_data(). An ADD is used to coalesce the data.
 * The handle is unused (pass zero for now).
 * The mask is unused (pass zero for now).
 * 一种调度源（dispatch source），它合并通过调用 dispatch_source_merge_data 获得的数据。ADD 用于合并数据。句柄未使用（暂时传递零），mask 未使用（暂时传递零）。
 */
/*
 
在 Swift（在 Swift 中使用 Objective-C）下宏定义展开是:

extern struct dispatch_source_type_s _dispatch_source_type_data_add;
@protocol OS_dispatch_source_data_add <OS_dispatch_source>
@end

@interface OS_dispatch_source () <OS_dispatch_source_data_add>
@end

在 Objective-C/C++/C 下宏定义展开是:

extern const struct dispatch_source_type_s _dispatch_source_type_data_add;

*/

#define DISPATCH_SOURCE_TYPE_DATA_ADD (&_dispatch_source_type_data_add)
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_SOURCE_TYPE_DECL(data_add);

/*!
 * @const DISPATCH_SOURCE_TYPE_DATA_OR
 * @discussion A dispatch source that coalesces data obtained via calls to
 * dispatch_source_merge_data(). A bitwise OR is used to coalesce the data.
 * The handle is unused (pass zero for now).
 * The mask is unused (pass zero for now).
 * 一种调度源（dispatch source），它合并通过调用 dispatch_source_merge_data 获得的数据。按位或进行合并数据。句柄未使用（暂时传递零），mask 未使用（暂时传递零）。
 */
#define DISPATCH_SOURCE_TYPE_DATA_OR (&_dispatch_source_type_data_or)
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_SOURCE_TYPE_DECL(data_or);

/*!
 * @const DISPATCH_SOURCE_TYPE_DATA_REPLACE
 * @discussion A dispatch source that tracks data obtained via calls to
 * dispatch_source_merge_data(). Newly obtained data values replace existing
 * data values not yet delivered to the source handler
 *
 * A data value of zero will cause the source handler to not be invoked.
 *
 * The handle is unused (pass zero for now).
 * The mask is unused (pass zero for now).
 * 一种调度源（dispatch source），它跟踪通过调用 dispatch_source_merge_data 获得的数据。新获得的数据值替换了尚未传递到源处理程序（source handler）的现有数据值。数据值为零将不调用源处理程序（source handler）。句柄未使用（暂时传递零），mask 未使用（暂时传递零）。
 
 */
#define DISPATCH_SOURCE_TYPE_DATA_REPLACE (&_dispatch_source_type_data_replace)
API_AVAILABLE(macos(10.13), ios(11.0), tvos(11.0), watchos(4.0))
DISPATCH_SOURCE_TYPE_DECL(data_replace);

/*!
 * @const DISPATCH_SOURCE_TYPE_MACH_SEND
 * @discussion A dispatch source that monitors a Mach port for dead name
 * notifications (send right no longer has any corresponding receive right).
 * The handle is a Mach port with a send or send-once right (mach_port_t).
 * The mask is a mask of desired events from dispatch_source_mach_send_flags_t.
 * 一种调度源（dispatch source），用于监视 Mach port 的 dead name 通知（发送权限不再具有任何相应的接收权限）。句柄（handle）是一个 Mach port，具有 send 或 send once right（mach_port_t）。mask 是 dispatch_source_mach_send_flags_t 中所需事件的 mask。
 
 */
#define DISPATCH_SOURCE_TYPE_MACH_SEND (&_dispatch_source_type_mach_send)
API_AVAILABLE(macos(10.6), ios(4.0)) DISPATCH_LINUX_UNAVAILABLE()
DISPATCH_SOURCE_TYPE_DECL(mach_send);

/*!
 * @const DISPATCH_SOURCE_TYPE_MACH_RECV
 * @discussion A dispatch source that monitors a Mach port for pending messages.
 * The handle is a Mach port with a receive right (mach_port_t).
 * The mask is a mask of desired events from dispatch_source_mach_recv_flags_t,
 * but no flags are currently defined (pass zero for now).
 * 一种调度源（dispatch source），用于监视 Mach port 中的挂起消息。句柄（handle）是具有接收权限（mach_port_t）的 Mach port。mask 是来自 dispatch_source_mach_recv_flags_t 中所需事件的 mask，但是当前未定义任何标志（现在传递零）。
 
 */
#define DISPATCH_SOURCE_TYPE_MACH_RECV (&_dispatch_source_type_mach_recv)
API_AVAILABLE(macos(10.6), ios(4.0)) DISPATCH_LINUX_UNAVAILABLE()
DISPATCH_SOURCE_TYPE_DECL(mach_recv);

/*!
 * @const DISPATCH_SOURCE_TYPE_MEMORYPRESSURE
 * @discussion A dispatch source that monitors the system for changes in
 * memory pressure condition.
 * The handle is unused (pass zero for now).
 * The mask is a mask of desired events from
 * dispatch_source_memorypressure_flags_t.
 * 一种调度源（dispatch source），用于监视系统内存压力状况的变化。该句柄（handle）未使用（现在传递零）。mask 是来自 dispatch_source_mach_recv_flags_t 中所需事件的 mask。
 */
#define DISPATCH_SOURCE_TYPE_MEMORYPRESSURE \
		(&_dispatch_source_type_memorypressure)
API_AVAILABLE(macos(10.9), ios(8.0)) DISPATCH_LINUX_UNAVAILABLE()
DISPATCH_SOURCE_TYPE_DECL(memorypressure);

/*!
 * @const DISPATCH_SOURCE_TYPE_PROC
 * @discussion A dispatch source that monitors an external process for events
 * defined by dispatch_source_proc_flags_t.
 * The handle is a process identifier (pid_t).
 * The mask is a mask of desired events from dispatch_source_proc_flags_t.
 * 一种调度源（dispatch source），用于监视外部进程中由 dispatch_source_proc_flags_t 定义的事件。句柄（handle）是进程标识符（pid_t）。mask 是来自 dispatch_source_mach_recv_flags_t 中所需事件的 mask。
 
 */
#define DISPATCH_SOURCE_TYPE_PROC (&_dispatch_source_type_proc)
API_AVAILABLE(macos(10.6), ios(4.0)) DISPATCH_LINUX_UNAVAILABLE()
DISPATCH_SOURCE_TYPE_DECL(proc);

/*!
 * @const DISPATCH_SOURCE_TYPE_READ
 * @discussion A dispatch source that monitors a file descriptor for pending
 * bytes available to be read.
 * The handle is a file descriptor (int).
 * The mask is unused (pass zero for now).
 * 一种调度源（dispatch source），用于监视文件描述符的待处理字节，以获取可读取的字节。句柄（handle）是文件描述符（int）。mask 未使用（现在传递零）。
 */
#define DISPATCH_SOURCE_TYPE_READ (&_dispatch_source_type_read)
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_SOURCE_TYPE_DECL(read);

/*!
 * @const DISPATCH_SOURCE_TYPE_SIGNAL
 * @discussion A dispatch source that monitors the current process for signals.
 * The handle is a signal number (int).
 * The mask is unused (pass zero for now).
 * 监视当前进程以获取信号的调度源（dispatch source）。句柄（handle）是信号编号（int）。mask 未使用（现在传递零）。
 */
#define DISPATCH_SOURCE_TYPE_SIGNAL (&_dispatch_source_type_signal)
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_SOURCE_TYPE_DECL(signal);

/*!
 * @const DISPATCH_SOURCE_TYPE_TIMER
 * @discussion A dispatch source that submits the event handler block based
 * on a timer.
 * The handle is unused (pass zero for now).
 * The mask specifies which flags from dispatch_source_timer_flags_t to apply.
 * 基于计时器（based on a timer）提交（submits）事件处理程序块（event handler block）的调度源（dispatch source）。句柄（handle）未使用（现在传递零）。mask 指定要应用的来自 dispatch_source_timer_flags_t 的标志。
 */
#define DISPATCH_SOURCE_TYPE_TIMER (&_dispatch_source_type_timer)
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_SOURCE_TYPE_DECL(timer);

/*!
 * @const DISPATCH_SOURCE_TYPE_VNODE
 * @discussion A dispatch source that monitors a file descriptor for events
 * defined by dispatch_source_vnode_flags_t.
 * The handle is a file descriptor (int).
 * The mask is a mask of desired events from dispatch_source_vnode_flags_t.
 * 一种调度源（dispatch source），它监视由 dispatch_source_vnode_flags_t 定义的事件的文件描述符。句柄（handle）是文件描述符（int）。mask 是来自 dispatch_source_vnode_flags_t 的所需事件的 mask。
 
 */
#define DISPATCH_SOURCE_TYPE_VNODE (&_dispatch_source_type_vnode)
API_AVAILABLE(macos(10.6), ios(4.0)) DISPATCH_LINUX_UNAVAILABLE()
DISPATCH_SOURCE_TYPE_DECL(vnode);

/*!
 * @const DISPATCH_SOURCE_TYPE_WRITE
 * @discussion A dispatch source that monitors a file descriptor for available
 * buffer space to write bytes.
 * The handle is a file descriptor (int).
 * The mask is unused (pass zero for now).
 * 一种调度源（dispatch source），它监视文件描述符以获取可用于写入字节的缓冲区空间。句柄（handle）是文件描述符（int）。mask 未使用（现在传递零）。
 */
#define DISPATCH_SOURCE_TYPE_WRITE (&_dispatch_source_type_write)
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_SOURCE_TYPE_DECL(write);

/*!
 * @typedef dispatch_source_mach_send_flags_t
 * Type of dispatch_source_mach_send flags
 *
 * @constant DISPATCH_MACH_SEND_DEAD
 * The receive right corresponding to the given send right was destroyed.
 * dispatch_source_mach_send 标志的类型。
 */
#define DISPATCH_MACH_SEND_DEAD	0x1

typedef unsigned long dispatch_source_mach_send_flags_t;

/*!
 * @typedef dispatch_source_mach_recv_flags_t
 * Type of dispatch_source_mach_recv flags
 * dispatch_source_mach_recv 标志的类型。
 */
typedef unsigned long dispatch_source_mach_recv_flags_t;

/*!
 * @typedef dispatch_source_memorypressure_flags_t
 * Type of dispatch_source_memorypressure flags
 * dispatch_source_memorypressure 标志的类型。
 *
 * @constant DISPATCH_MEMORYPRESSURE_NORMAL 系统内存压力状况已恢复正常。
 * The system memory pressure condition has returned to normal.
 *
 * @constant DISPATCH_MEMORYPRESSURE_WARN 系统内存压力状况已更改为警告。
 * The system memory pressure condition has changed to warning.
 *
 * @constant DISPATCH_MEMORYPRESSURE_CRITICAL 系统内存压力状况已变为严重。
 * The system memory pressure condition has changed to critical.
 *
 *
 * @discussion
 * Elevated memory pressure is a system-wide condition that applications
 * registered for this source should react to by changing their future memory
 * use behavior, e.g. by reducing cache sizes of newly initiated operations
 * until memory pressure returns back to normal.
 * NOTE: applications should NOT traverse and discard existing caches for past
 * operations when the system memory pressure enters an elevated state, as that
 * is likely to trigger VM operations that will further aggravate system memory
 * pressure.
 * 内存压力升高是一种系统范围内的情况，为此源注册的应用程序应通过更改其将来的内存使用行为来作出反应，例如：通过减少新启动操作的缓存大小，直到内存压力恢复正常。
  注意：当系统内存压力进入提升状态时，应用程序不应遍历并丢弃现有缓存以进行过去的操作，因为这很可能会触发 VM 操作，从而进一步加剧系统内存压力。
 *
 *
 */

#define DISPATCH_MEMORYPRESSURE_NORMAL		0x01
#define DISPATCH_MEMORYPRESSURE_WARN		0x02
#define DISPATCH_MEMORYPRESSURE_CRITICAL	0x04

typedef unsigned long dispatch_source_memorypressure_flags_t;

/*!
 * @typedef dispatch_source_proc_flags_t
 * Type of dispatch_source_proc flags
 * dispatch_source_proc 标志的类型。
 *
 * @constant DISPATCH_PROC_EXIT 该进程已经退出（也许是 cleanly，也许不是）。
 * The process has exited (perhaps cleanly, perhaps not).
 *
 * @constant DISPATCH_PROC_FORK 该进程已创建一个或多个子进程。
 * The process has created one or more child processes.
 *
 * @constant DISPATCH_PROC_EXEC 通过 exec *() 或 posix_spawn *()，该进程已成为另一个可执行映像（executable image）。
 * The process has become another executable image via
 * exec*() or posix_spawn*().
 *
 * @constant DISPATCH_PROC_SIGNAL Unix 信号已传递到该进程。
 * A Unix signal was delivered to the process.
 */
#define DISPATCH_PROC_EXIT		0x80000000
#define DISPATCH_PROC_FORK		0x40000000
#define DISPATCH_PROC_EXEC		0x20000000
#define DISPATCH_PROC_SIGNAL	0x08000000

typedef unsigned long dispatch_source_proc_flags_t;

/*!
 * @typedef dispatch_source_vnode_flags_t
 * Type of dispatch_source_vnode flags dispatch_source_vnode 标志的类型。
 *
 * @constant DISPATCH_VNODE_DELETE filesystem 对象已从 namespace 中删除。
 * The filesystem object was deleted from the namespace.
 *
 * @constant DISPATCH_VNODE_WRITE filesystem 对象数据已更改。
 * The filesystem object data changed.
 *
 * @constant DISPATCH_VNODE_EXTEND  filesystem 对象的大小已更改。
 * The filesystem object changed in size.
 *
 * @constant DISPATCH_VNODE_ATTRIB  filesystem 对象 metadata 已更改。
 * The filesystem object metadata changed.
 *
 * @constant DISPATCH_VNODE_LINK filesystem 对象 link计数已更改。
 * The filesystem object link count changed.
 *
 * @constant DISPATCH_VNODE_RENAME filesystem 对象在 namespace 中被重命名。
 * The filesystem object was renamed in the namespace.
 *
 * @constant DISPATCH_VNODE_REVOKE filesystem 对象被 revoked。
 * The filesystem object was revoked.
 *
 * @constant DISPATCH_VNODE_FUNLOCK filesystem 对象已解锁。
 * The filesystem object was unlocked.
 */

#define DISPATCH_VNODE_DELETE	0x1
#define DISPATCH_VNODE_WRITE	0x2
#define DISPATCH_VNODE_EXTEND	0x4
#define DISPATCH_VNODE_ATTRIB	0x8
#define DISPATCH_VNODE_LINK		0x10
#define DISPATCH_VNODE_RENAME	0x20
#define DISPATCH_VNODE_REVOKE	0x40
#define DISPATCH_VNODE_FUNLOCK	0x100

typedef unsigned long dispatch_source_vnode_flags_t;

/*!
 * @typedef dispatch_source_timer_flags_t
 * Type of dispatch_source_timer flags dispatch_source_timer 标志的类型。
 *
 * @constant DISPATCH_TIMER_STRICT
 * Specifies that the system should make a best effort to strictly observe the
 * leeway value specified for the timer via dispatch_source_set_timer(), even
 * if that value is smaller than the default leeway value that would be applied
 * to the timer otherwise. A minimal amount of leeway will be applied to the
 * timer even if this flag is specified.
 *
 * CAUTION: Use of this flag may override power-saving techniques employed by
 * the system and cause higher power consumption, so it must be used with care
 * and only when absolutely necessary.
 *
 * DISPATCH_TIMER_STRICT 指定系统应尽最大努力严格遵守通过 dispatch_source_set_timer 为计时器指定的 leeway value，even if that value is smaller than the default leeway value that would be applied to the timer otherwise. 即使指定了此标志，也会有 minimal amount of leeway 应用于计时器。注意：使用此标志可能会覆盖系统采用的节电（power-saving）技术，并导致更高的功耗，因此必须谨慎使用它，并且仅在绝对必要时使用。

 *
 */

#define DISPATCH_TIMER_STRICT 0x1

typedef unsigned long dispatch_source_timer_flags_t;

/*!
 * @function dispatch_source_create
 *
 * @abstract
 * Creates a new dispatch source to monitor low-level system objects and auto-
 * matically submit a handler block to a dispatch queue in response to events.
 *
 * dispatch_source_create 创建一个新的调度源（dispatch source）来监视低级系统对象（low-level system objects），并根据事件自动将处理程序块（handler block）提交给调度队列（dispatch queue）。
 *
 * @discussion
 * Dispatch sources are not reentrant. Any events received while the dispatch
 * source is suspended or while the event handler block is currently executing
 * will be coalesced and delivered after the dispatch source is resumed or the
 * event handler block has returned.
 * Dispatch sources 不可重入。在调度源被挂起或事件处理程序块当前正在执行时，接收到的任何事件都将在调度源恢复或事件处理程序块返回后合并和传递。
 *
 * Dispatch sources are created in an inactive state. After creating the
 * source and setting any desired attributes (i.e. the handler, context, etc.),
 * a call must be made to dispatch_activate() in order to begin event delivery.
 * Dispatch sources 在非活动状态下创建。创建源并设置任何所需的属性（即处理程序，上下文等）之后，必须调用 dispatch_activate 才能开始事件传递。
 

 * Calling dispatch_set_target_queue() on a source once it has been activated
 * is not allowed (see dispatch_activate() and dispatch_set_target_queue()).
 *
 * 一旦被激活，就不允许在源上调用 dispatch_set_target_queue（参阅 dispatch_activate 和 dispatch_set_target_queue）。
 *
 * For backward compatibility reasons, dispatch_resume() on an inactive,
 * and not otherwise suspended source has the same effect as calling
 * dispatch_activate(). For new code, using dispatch_activate() is preferred.
 *出于向后兼容性的原因，在非活动且未暂停的源上的 dispatch_resume 与调用 dispatch_activate 具有相同的效果。对于新代码，首选使用 dispatch_activate。
 *
 * @param type
 * Declares the type of the dispatch source. Must be one of the defined
 * dispatch_source_type_t constants. 声明调度源的类型。必须是已定义的 dispatch_source_type_t 常量之一。
 *
 * @param handle
 * The underlying system handle to monitor. The interpretation of this argument
 * is determined by the constant provided in the type parameter. 要监视的基础系统句柄（handle）。此参数的解释由 type 参数中提供的常量确定。
 *
 * @param mask
 * A mask of flags specifying which events are desired. The interpretation of
 * this argument is determined by the constant provided in the type parameter. 指定所需事件的标志 mask。此参数的解释由 type 参数中提供的常量确定。
 *
 * @param queue
 * The dispatch queue to which the event handler block will be submitted.
 * If queue is DISPATCH_TARGET_QUEUE_DEFAULT, the source will submit the event
 * handler block to the default priority global
 * queue. 事件处理程序块（event handler block）将提交到的调度队列。如果队列为 DISPATCH_TARGET_QUEUE_DEFAULT，则源（source）将事件处理程序块提交到默认优先级全局队列。
 *
 * @result
 * The newly created dispatch source. Or NULL if invalid arguments are passed. 新创建的调度源（dispatch source）。如果传递了无效的参数，则为 NULL。
 */

API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_MALLOC DISPATCH_RETURNS_RETAINED DISPATCH_WARN_RESULT
DISPATCH_NOTHROW
dispatch_source_t
dispatch_source_create(dispatch_source_type_t type,
	uintptr_t handle,
	unsigned long mask,
	dispatch_queue_t _Nullable queue);

/*!
 * @function dispatch_source_set_event_handler
 *
 * @abstract
 * Sets the event handler block for the given dispatch source.
 * 为给定的调度源（dispatch source）设置事件处理程序块（event handler block）。
 *
 * @param source
 * The dispatch source to modify.
 * The result of passing NULL in this parameter is undefined. 要进行修改的调度源。在此参数中传递 NULL 的结果是未定义的
 *
 * @param handler
 * The event handler block to submit to the source's target queue. 事件处理程序块将提交到源的目标队列。
 */
#ifdef __BLOCKS__
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_NOTHROW
void
dispatch_source_set_event_handler(dispatch_source_t source,
	dispatch_block_t _Nullable handler);
#endif /* __BLOCKS__ */

/*!
 * @function dispatch_source_set_event_handler_f
 *
 * @abstract 为给定的调度源设置事件处理函数。
 * Sets the event handler function for the given dispatch source.
 *
 * @param source
 * The dispatch source to modify.
 * The result of passing NULL in this parameter is undefined.
 *
 * @param handler 事件处理程序函数提交到源的目标队列。传递给事件处理程序（函数）的 context 参数是设置事件处理程序时当前调度源的上下文。
 * The event handler function to submit to the source's target queue.
 * The context parameter passed to the event handler function is the context of
 * the dispatch source current at the time the event handler was set.
 */
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_NOTHROW
void
dispatch_source_set_event_handler_f(dispatch_source_t source,
	dispatch_function_t _Nullable handler);

/*!
 * @function dispatch_source_set_cancel_handler
 *
 * @abstract
 * Sets the cancellation handler block for the given dispatch source. 为给定的调度源设置取消处理程序块（cancellation handler block）。
 *
 * @discussion
 * The cancellation handler (if specified) will be submitted to the source's
 * target queue in response to a call to dispatch_source_cancel() once the
 * system has released all references to the source's underlying handle and
 * the source's event handler block has returned. 一旦系统释放了对源基础句柄的所有引用，并且返回了源的事件处理程序块，则取消处理程序（如果已指定）将被提交到源的目标队列，以响应对 dispatch_source_cancel 的调用。
 *
 * IMPORTANT:
 * Source cancellation and a cancellation handler are required for file
 * descriptor and mach port based sources in order to safely close the
 * descriptor or destroy the port.
 * Closing the descriptor or port before the cancellation handler is invoked may
 * result in a race condition. If a new descriptor is allocated with the same
 * value as the recently closed descriptor while the source's event handler is
 * still running, the event handler may read/write data to the wrong descriptor.
 * IMPORTANT：file descriptor 和基于 mach port 的源需要源取消（source cancellation）和取消处理程序（a cancellation handler），以便安全地关闭描述符或销毁端口。在调用取消处理程序之前关闭描述符或端口可能会导致竞态。如果在源的事件处理程序仍在运行时为新描述符分配了与最近关闭的描述符相同的值，则事件处理程序可能会将数据读/写到错误的描述符。
 
 * @param source
 * The dispatch source to modify.
 * The result of passing NULL in this parameter is undefined.
 *
 * @param handler 取消处理程序块将提交到源的目标队列。
 * The cancellation handler block to submit to the source's target queue.
 */
#ifdef __BLOCKS__
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_NOTHROW
void
dispatch_source_set_cancel_handler(dispatch_source_t source,
	dispatch_block_t _Nullable handler);
#endif /* __BLOCKS__ */

/*!
 * @function dispatch_source_set_cancel_handler_f
 *
 * @abstract 设置给定调度源的取消处理函数。
 * Sets the cancellation handler function for the given dispatch source.
 *
 * @discussion
 * See dispatch_source_set_cancel_handler() for more details.
 *
 * @param source
 * The dispatch source to modify.
 * The result of passing NULL in this parameter is undefined.
 *
 * @param handler
 * The cancellation handler function to submit to the source's target queue.
 * The context parameter passed to the event handler function is the current
 * context of the dispatch source at the time the handler call is made.
 */
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_NOTHROW
void
dispatch_source_set_cancel_handler_f(dispatch_source_t source,
	dispatch_function_t _Nullable handler);

/*!
 * @function dispatch_source_cancel
 *
 * @abstract 异步取消调度源（dispatch source），以防止进一步调用其事件处理程序块。
 * Asynchronously cancel the dispatch source, preventing any further invocation
 * of its event handler block.
 *
 * @discussion
 * Cancellation prevents any further invocation of the event handler block for
 * the specified dispatch source, but does not interrupt an event handler
 * block that is already in progress.
 *  取消操作（dispatch_source_cancel）将阻止对指定调度源的事件处理程序块（event handler block）的任何进一步调用，但不会中断已在进行中的事件处理程序块。
 *
 * The cancellation handler is submitted to the source's target queue once the
 * the source's event handler has finished, indicating it is now safe to close
 * the source's handle (i.e. file descriptor or mach port).
 *  一旦源的事件处理程序完成，取消处理程序将提交到源的目标队列，这表明现在可以安全地关闭源的句柄（i.e. file descriptor or mach port）。
 *
 * See dispatch_source_set_cancel_handler() for more information.
 *
 * @param source
 * The dispatch source to be canceled.
 * The result of passing NULL in this parameter is undefined.
 */
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
void
dispatch_source_cancel(dispatch_source_t source);

/*!
 * @function dispatch_source_testcancel
 *
 * @abstract 测试给定的调度源是否已取消。
 * Tests whether the given dispatch source has been canceled.
 *
 * @param source 取消则非零，未取消则为零。
 * The dispatch source to be tested.
 * The result of passing NULL in this parameter is undefined.
 *
 * @result
 * Non-zero if canceled and zero if not canceled.
 */
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_WARN_RESULT DISPATCH_PURE
DISPATCH_NOTHROW
long
dispatch_source_testcancel(dispatch_source_t source);

/*!
 * @function dispatch_source_get_handle
 *
 * @abstract 返回与此调度源关联的基础系统句柄（underlying system handle）。
 * Returns the underlying system handle associated with this dispatch source.
 *
 * @param source
 * The result of passing NULL in this parameter is undefined.
 *
 * @result
 * The return value should be interpreted according to the type of the dispatch
 * source, and may be one of the following handles: 返回值应根据调度源的类型进行解释，并且可以是以下句柄之一:
 *
 *  DISPATCH_SOURCE_TYPE_DATA_ADD:        n/a
 *  DISPATCH_SOURCE_TYPE_DATA_OR:         n/a
 *  DISPATCH_SOURCE_TYPE_DATA_REPLACE:    n/a
 *  DISPATCH_SOURCE_TYPE_MACH_SEND:       mach port (mach_port_t)
 *  DISPATCH_SOURCE_TYPE_MACH_RECV:       mach port (mach_port_t)
 *  DISPATCH_SOURCE_TYPE_MEMORYPRESSURE   n/a
 *  DISPATCH_SOURCE_TYPE_PROC:            process identifier (pid_t)
 *  DISPATCH_SOURCE_TYPE_READ:            file descriptor (int)
 *  DISPATCH_SOURCE_TYPE_SIGNAL:          signal number (int)
 *  DISPATCH_SOURCE_TYPE_TIMER:           n/a
 *  DISPATCH_SOURCE_TYPE_VNODE:           file descriptor (int)
 *  DISPATCH_SOURCE_TYPE_WRITE:           file descriptor (int)
 */
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_WARN_RESULT DISPATCH_PURE
DISPATCH_NOTHROW
uintptr_t
dispatch_source_get_handle(dispatch_source_t source);

/*!
 * @function dispatch_source_get_mask
 *
 * @abstract 返回由调度源监视的事件的 mask。
 * Returns the mask of events monitored by the dispatch source.
 *
 * @param source
 * The result of passing NULL in this parameter is undefined.
 *
 * @result
 * The return value should be interpreted according to the type of the dispatch
 * source, and may be one of the following flag sets:
 * 返回值应根据调度源的类型进行解释，并且可以是以下 flag 之一：
 *  DISPATCH_SOURCE_TYPE_DATA_ADD:        n/a
 *  DISPATCH_SOURCE_TYPE_DATA_OR:         n/a
 *  DISPATCH_SOURCE_TYPE_DATA_REPLACE:    n/a
 *  DISPATCH_SOURCE_TYPE_MACH_SEND:       dispatch_source_mach_send_flags_t
 *  DISPATCH_SOURCE_TYPE_MACH_RECV:       dispatch_source_mach_recv_flags_t
 *  DISPATCH_SOURCE_TYPE_MEMORYPRESSURE   dispatch_source_memorypressure_flags_t
 *  DISPATCH_SOURCE_TYPE_PROC:            dispatch_source_proc_flags_t
 *  DISPATCH_SOURCE_TYPE_READ:            n/a
 *  DISPATCH_SOURCE_TYPE_SIGNAL:          n/a
 *  DISPATCH_SOURCE_TYPE_TIMER:           dispatch_source_timer_flags_t
 *  DISPATCH_SOURCE_TYPE_VNODE:           dispatch_source_vnode_flags_t
 *  DISPATCH_SOURCE_TYPE_WRITE:           n/a
 */
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_WARN_RESULT DISPATCH_PURE
DISPATCH_NOTHROW
unsigned long
dispatch_source_get_mask(dispatch_source_t source);

/*!
 * @function dispatch_source_get_data
 *
 * @abstract 返回调度源的待处理数据。
 * Returns pending data for the dispatch source.
 *
 * @discussion
 * This function is intended to be called from within the event handler block.
 * The result of calling this function outside of the event handler callback is
 * undefined. 该函数旨在从事件处理程序块中调用。在事件处理程序回调之外调用此函数的结果是未定义的。
 *
 * @param source
 * The result of passing NULL in this parameter is undefined.
 *
 * @result
 * The return value should be interpreted according to the type of the dispatch
 * source, and may be one of the following:
 * 返回值应根据调度源的类型进行解释，并且可以是以下之一：
 *  DISPATCH_SOURCE_TYPE_DATA_ADD:        application defined data
 *  DISPATCH_SOURCE_TYPE_DATA_OR:         application defined data
 *  DISPATCH_SOURCE_TYPE_DATA_REPLACE:    application defined data
 *  DISPATCH_SOURCE_TYPE_MACH_SEND:       dispatch_source_mach_send_flags_t
 *  DISPATCH_SOURCE_TYPE_MACH_RECV:       dispatch_source_mach_recv_flags_t
 *  DISPATCH_SOURCE_TYPE_MEMORYPRESSURE   dispatch_source_memorypressure_flags_t
 *  DISPATCH_SOURCE_TYPE_PROC:            dispatch_source_proc_flags_t
 *  DISPATCH_SOURCE_TYPE_READ:            estimated bytes available to read
 *  DISPATCH_SOURCE_TYPE_SIGNAL:          number of signals delivered since
 *                                            the last handler invocation
 *  DISPATCH_SOURCE_TYPE_TIMER:           number of times the timer has fired
 *                                            since the last handler invocation
 *  DISPATCH_SOURCE_TYPE_VNODE:           dispatch_source_vnode_flags_t
 *  DISPATCH_SOURCE_TYPE_WRITE:           estimated buffer space available
 */
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_WARN_RESULT DISPATCH_PURE
DISPATCH_NOTHROW
unsigned long
dispatch_source_get_data(dispatch_source_t source);

/*!
 * @function dispatch_source_merge_data
 *
 * @abstract 将数据合并到类型为 DISPATCH_SOURCE_TYPE_DATA_ADD，DISPATCH_SOURCE_TYPE_DATA_OR 或 DISPATCH_SOURCE_TYPE_DATA_REPLACE 的调度源中，并将其事件处理程序块提交到其目标队列。
 * Merges data into a dispatch source of type DISPATCH_SOURCE_TYPE_DATA_ADD,
 * DISPATCH_SOURCE_TYPE_DATA_OR or DISPATCH_SOURCE_TYPE_DATA_REPLACE,
 * and submits its event handler block to its target queue.
 *
 * @param source 使用调度源类型指定的逻辑 OR 或 ADD 与待处理数据合并的值。零值无效并且也不会导致事件处理程序块的提交。
 * The result of passing NULL in this parameter is undefined.
 *
 * @param value
 * The value to coalesce with the pending data using a logical OR or an ADD
 * as specified by the dispatch source type. A value of zero has no effect
 * and will not result in the submission of the event handler block.
 */
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
void
dispatch_source_merge_data(dispatch_source_t source, unsigned long value);

/*!
 * @function dispatch_source_set_timer
 *
 * @abstract 设置 timer source 的开始时间，间隔和回程值（leeway value）。
 * Sets a start time, interval, and leeway value for a timer source.
 *
 * @discussion
 * Once this function returns, any pending source data accumulated for the
 * previous timer values has been cleared; the next fire of the timer will
 * occur at 'start', and every 'interval' nanoseconds thereafter until the
 * timer source is canceled.
 * 此函数返回后，将清除先前计时器值累积的所有未决源数据；计时器的下一次触发将在 start 时发生，此后每隔 interval 纳秒，直到计时器源被取消。
 *
 * Any fire of the timer may be delayed by the system in order to improve power
 * consumption and system performance. The upper limit to the allowable delay
 * may be configured with the 'leeway' argument, the lower limit is under the
 * control of the system. 系统可能会延迟计时器的任何触发时间，以改善功耗和系统性能。允许延迟的上限可以使用 leeway 参数进行配置，下限在系统的控制下。
 *
 * For the initial timer fire at 'start', the upper limit to the allowable
 * delay is set to 'leeway' nanoseconds. For the subsequent timer fires at
 * 'start' + N * 'interval', the upper limit is MIN('leeway','interval'/2). 对于 start 时的初始计时器触发，允许延迟的上限设置为 leeway 纳秒。对于随后的计时器以 start + N * interval 触发的情况，上限为 MIN（leeway，interval / 2）。
 *
 * The lower limit to the allowable delay may vary with process state such as
 * visibility of application UI. If the specified timer source was created with
 * a mask of DISPATCH_TIMER_STRICT, the system will make a best effort to
 * strictly observe the provided 'leeway' value even if it is smaller than the
 * current lower limit. Note that a minimal amount of delay is to be expected
 * even if this flag is specified. 允许延迟的下限可能随过程状态（例如应用程序 UI 的可见性）而变化。如果指定的计时器源是使用 DISPATCH_TIMER_STRICT 的 mask 创建的，则系统将尽最大努力严格遵守所提供的 leeway 值，即使该值小于当前下限。请注意，即使指定了此标志，也希望有最小的延迟量。
 *
 * The 'start' argument also determines which clock will be used for the timer:
 * If 'start' is DISPATCH_TIME_NOW or was created with dispatch_time(3), the
 * timer is based on up time (which is obtained from mach_absolute_time() on
 * Apple platforms). If 'start' was created with dispatch_walltime(3), the
 * timer is based on gettimeofday(3). start 参数还确定将使用哪个时钟作为计时器：如果 start 是 DISPATCH_TIME_NOW 或由 dispatch_time(3) 创建的，则计时器基于正常运行时间（从 Apple 平台上的 mach_absolute_time 获取） 。如果使用 dispatch_walltime(3) 创建了 start，则计时器基于 gettimeofday(3)。

 *
 * Calling this function has no effect if the timer source has already been
 * canceled. 如果 timer source 已被取消，则调用此函数无效。
 *
 * @param start
 * The start time of the timer. See dispatch_time() and dispatch_walltime()
 * for more information. 计时器的开始时间。参考 dispatch_time() 和 dispatch_walltime()。
 *
 * @param interval
 * The nanosecond interval for the timer. Use DISPATCH_TIME_FOREVER for a
 * one-shot timer. 计时器的纳秒间隔。将 DISPATCH_TIME_FOREVER 用于一键式计时器（a one-shot timer）。
 *
 * @param leeway
 * The nanosecond leeway for the timer. timer 的纳秒 leeway。
 */
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
void
dispatch_source_set_timer(dispatch_source_t source,
	dispatch_time_t start,
	uint64_t interval,
	uint64_t leeway);

/*!
 * @function dispatch_source_set_registration_handler
 *
 * @abstract 设置给定调度源的注册处理程序块（registration handler block）。
 * Sets the registration handler block for the given dispatch source.
 *
 * @discussion
 * The registration handler (if specified) will be submitted to the source's
 * target queue once the corresponding kevent() has been registered with the
 * system, following the initial dispatch_resume() of the source.
 * 一旦相应的 kevent 已在源中的初始 dispatch_resume 之后向系统注册，注册处理程序（如果已指定）将被提交到源的目标队列。
 * If a source is already registered when the registration handler is set, the
 * registration handler will be invoked immediately.
 * 如果在设置注册处理程序时已经注册了源，则会立即调用注册处理程序。
 *
 * @param source
 * The dispatch source to modify.
 * The result of passing NULL in this parameter is undefined.
 *
 * @param handler 注册处理程序块将提交到源的目标队列。
 * The registration handler block to submit to the source's target queue.
 */
#ifdef __BLOCKS__
API_AVAILABLE(macos(10.7), ios(4.3))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_NOTHROW
void
dispatch_source_set_registration_handler(dispatch_source_t source,
	dispatch_block_t _Nullable handler);
#endif /* __BLOCKS__ */

/*!
 * @function dispatch_source_set_registration_handler_f
 *
 * @abstract 设置给定调度源（dispatch source）的注册处理函数。
 * Sets the registration handler function for the given dispatch source.
 *
 * @discussion
 * See dispatch_source_set_registration_handler() for more details.
 *
 * @param source
 * The dispatch source to modify.
 * The result of passing NULL in this parameter is undefined.
 *
 * @param handler
 * The registration handler function to submit to the source's target queue.
 * The context parameter passed to the registration handler function is the
 * current context of the dispatch source at the time the handler call is made.
 */
API_AVAILABLE(macos(10.7), ios(4.3))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_NOTHROW
void
dispatch_source_set_registration_handler_f(dispatch_source_t source,
	dispatch_function_t _Nullable handler);

__END_DECLS

DISPATCH_ASSUME_NONNULL_END

#endif
