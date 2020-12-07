/*
 * Copyright (c) 2017-2019 Apple Inc. All rights reserved.
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

#ifndef __DISPATCH_WORKLOOP__
#define __DISPATCH_WORKLOOP__

#ifndef __DISPATCH_INDIRECT__
#error "Please #include <dispatch/dispatch.h> instead of this file directly."
#include <dispatch/base.h> // for HeaderDoc
#endif

DISPATCH_ASSUME_NONNULL_BEGIN

__BEGIN_DECLS

//调度工作循环（dispatch workloops），是 dispatch_queue_t 的子类。
//调度工作循环（dispatch workloops）按优先级调用提交给它们的工作项。（dispatch_workloop_t 继承自 dispatch_queue_t）。
/*
 在 Swift （在 Swift 中使用 Objective-C）下宏定义展开是:

 OS_EXPORT OS_OBJECT_OBJC_RUNTIME_VISIBLE
 @interface OS_dispatch_workloop : OS_dispatch_queue
 - (instancetype)init OS_SWIFT_UNAVAILABLE("Unavailable in Swift");
 @end

 typedef OS_dispatch_workloop * dispatch_workloop_t;

  OS_dispatch_workloop 是继承自 OS_dispatch_queue 的类，然后 dispatch_workloop_t 是指向 OS_dispatch_workloop 的指针。

 在 Objective-C 下宏定义展开是:

 @protocol OS_dispatch_workloop <OS_dispatch_queue>
 @end

 typedef NSObject<OS_dispatch_workloop> * dispatch_workloop_t;

  OS_dispatch_workloop 是继承自 OS_dispatch_queue 协议的协议，并且为遵循该协议的 NSObject 实例对象类型的指针定义了一个 dispatch_workloop_t 的别名。

 在 C++ 下宏定义展开是:

 typedef struct dispatch_workloop_s : public dispatch_queue_s {} *dispatch_workloop_t;

  dispatch_workloop_t 是一个指向 dispatch_workloop_s 结构体的指针。

 在 C（Plain C）下宏定义展开是:

 typedef struct dispatch_queue_t *dispatch_workloop_t

dispatch_group_t 是指向 struct dispatch_group_s 的指针。
 */

/*!
 * @typedef dispatch_workloop_t
 *
 * @abstract
 * Dispatch workloops invoke workitems submitted to them in priority order.
 *
 * @discussion 调度工作循环（dispatch workloop）是 dispatch_queue_t 的一种形式，它是优先排序的队列（使用提交的工作项的 QOS 类作为排序依据）。

 * A dispatch workloop is a flavor of dispatch_queue_t that is a priority
 * ordered queue (using the QOS class of the submitted workitems as the
 * ordering). 在每次调用 workitem 之间，workloop 将评估是否有更高优先级的工作项直接提交给 workloop 或任何以 workloop 为目标的队列，并首先执行这些工作项。
 

 *
 * Between each workitem invocation, the workloop will evaluate whether higher
 * priority workitems have since been submitted, either directly to the
 * workloop or to any queues that target the workloop, and execute these first. 针对 workloop 的 serial queues 维护其工作项的 FIFO 执行。但是，workloop 可以基于它们的优先级，将提交给以其为目标的独立串行队列（independent serial queues）的工作项彼此重新排序，同时保留关于每个串行队列的 FIFO 执行。
 *
 * Serial queues targeting a workloop maintain FIFO execution of their
 * workitems. However, the workloop may reorder workitems submitted to
 * independent serial queues targeting it with respect to each other,
 * based on their priorities, while preserving FIFO execution with respect to
 * each serial queue.
 *
 * A dispatch workloop is a "subclass" of dispatch_queue_t which can be passed
 * to all APIs accepting a dispatch queue, except for functions from the
 * dispatch_sync() family. dispatch_async_and_wait() must be used for workloop
 * objects. Functions from the dispatch_sync() family on queues targeting
 * a workloop are still permitted but discouraged for performance reasons.
 * dispatch workloop 是 dispatch_queue_t 的 “subclass” ，可以将其传递给所有接受 dispatch queue 的 API，但 dispatch_sync 系列中的函数除外。 dispatch_async_and_wait 必须用于 workloop 对象。以 workloop 为目标的队列上的 dispatch_sync 系列函数仍被允许，但出于性能原因不建议使用。
 
 */
#if defined(__DISPATCH_BUILDING_DISPATCH__) && !defined(__OBJC__)
typedef struct dispatch_workloop_s *dispatch_workloop_t;
#else
DISPATCH_DECL_SUBCLASS(dispatch_workloop, dispatch_queue);
#endif

/*!
 * @function dispatch_workloop_create
 *
 * @abstract 创建一个新的调度工作循环（dispatch workloop），可以向其提交工作项（workitems）。
 * Creates a new dispatch workloop to which workitems may be submitted.
 *
 * @param label 附加到工作循环（workloop）的字符串标签。
 * A string label to attach to the workloop.
 *
 * @result 新创建的调度工作循环（dispatch workloop）。
 * The newly created dispatch workloop.
 */
API_AVAILABLE(macos(10.14), ios(12.0), tvos(12.0), watchos(5.0))
DISPATCH_EXPORT DISPATCH_MALLOC DISPATCH_RETURNS_RETAINED DISPATCH_WARN_RESULT
DISPATCH_NOTHROW
dispatch_workloop_t
dispatch_workloop_create(const char *_Nullable label);

/*!
 * @function dispatch_workloop_create_inactive
 *
 * @abstract 创建一个可以设置后续激活（setup and then activated）的新的非活动调度工作循环（dispatch workloop）。
 * Creates a new inactive dispatch workloop that can be setup and then
 * activated.
 *
 * @discussion 创建一个不活动的 workloop 可以使其在激活之前接受进一步的配置，并可以向其提交工作项。
 * Creating an inactive workloop allows for it to receive further configuration
 * before it is activated, and workitems can be submitted to it.
 *
 * Submitting workitems to an inactive workloop is undefined and will cause the
 * process to be terminated. 将工作项（workitems）提交到无效的工作循环（inactive workloop）是未定义的，这将导致过程终止。
 *
 * @param label 附加到 workloop 的字符串标签。
 * A string label to attach to the workloop.
 *
 * @result 新创建的调度工作循环。
 * The newly created dispatch workloop.
 */
API_AVAILABLE(macos(10.14), ios(12.0), tvos(12.0), watchos(5.0))
DISPATCH_EXPORT DISPATCH_MALLOC DISPATCH_RETURNS_RETAINED DISPATCH_WARN_RESULT
DISPATCH_NOTHROW
dispatch_workloop_t
dispatch_workloop_create_inactive(const char *_Nullable label);

/*!
 * @function dispatch_workloop_set_autorelease_frequency
 *
 * @abstract 设置 workloop 的自动释放频率（autorelease frequency）
 * Sets the autorelease frequency of the workloop.
 *
 * @discussion 可参考 dispatch_queue_attr_make_with_autorelease_frequency，workloop 的默认策略是 DISPATCH_AUTORELEASE_FREQUENCY_WORK_ITEM。
 * See dispatch_queue_attr_make_with_autorelease_frequency().
 * The default policy for a workloop is
 * DISPATCH_AUTORELEASE_FREQUENCY_WORK_ITEM.
 *
 * @param workloop
 * The dispatch workloop to modify.
 * dispatch workloop 进行修改。该 workloop 必须是非活动的，传递激活的对象是不确定的，并且将导致进程终止。
 * This workloop must be inactive, passing an activated object is undefined
 * and will cause the process to be terminated.
 *
 * @param frequency
 * The requested autorelease frequency.
 */
API_AVAILABLE(macos(10.14), ios(12.0), tvos(12.0), watchos(5.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
void
dispatch_workloop_set_autorelease_frequency(dispatch_workloop_t workloop,
		dispatch_autorelease_frequency_t frequency);

__END_DECLS

DISPATCH_ASSUME_NONNULL_END

#endif
