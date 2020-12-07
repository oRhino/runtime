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

#ifndef __DISPATCH_GROUP__
#define __DISPATCH_GROUP__

#ifndef __DISPATCH_INDIRECT__
#error "Please #include <dispatch/dispatch.h> instead of this file directly."
#include <dispatch/base.h> // for HeaderDoc
#endif

DISPATCH_ASSUME_NONNULL_BEGIN

/*!
 * @typedef dispatch_group_t
 * @abstract
 * A group of blocks submitted to queues for asynchronous invocation. 表示提交给队列以进行异步调用的一组块。
 */
DISPATCH_DECL(dispatch_group);
//Swift :OS_dispatch_group 是继承自 OS_dispatch_object 的类，然后 dispatch_group_t 是一个指向 OS_dispatch_group 的指针。
//OC: OS_dispatch_group 是继承自 OS_dispatch_object 协议的协议，并且为遵循该协议的 NSObject 实例对象类型的指针定义了一个 dispatch_group_t 的别名。
//c++:  dispatch_group_t 是一个指向 dispatch_group_s 结构体的指针。
//c: dispatch_group_t 是指向 struct dispatch_group_s 的指针。

__BEGIN_DECLS

/*!
 * @function dispatch_group_create
 *
 * @abstract
 * Creates new group with which blocks may be associated.
 *
 * @discussion
 * This function creates a new group with which blocks may be associated.
 * The dispatch group may be used to wait for the completion of the blocks it
 * references. The group object memory is freed with dispatch_release().
 *
 * @result
 * The newly created group, or NULL on failure.
 * 此函数用于创建可与块关联的新组。调度组（dispatch group）可用于等待它引用的块的完成（所有的 blocks 异步执行完成）。使用 dispatch_release 释放组对象内存。
 
  result 新创建的组，如果失败，则为 NULL。
 */
// 创建可以与块关联的新组。
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_MALLOC DISPATCH_RETURNS_RETAINED DISPATCH_WARN_RESULT
DISPATCH_NOTHROW
dispatch_group_t
dispatch_group_create(void);

/*!
 * @function dispatch_group_async
 *
 * @abstract
 * Submits a block to a dispatch queue and associates the block with the given
 * dispatch group.
 *
 * @discussion
 * Submits a block to a dispatch queue and associates the block with the given
 * dispatch group. The dispatch group may be used to wait for the completion
 * of the blocks it references.
 *
 * @param group
 * A dispatch group to associate with the submitted block.
 * The result of passing NULL in this parameter is undefined.
 *
 * @param queue
 * The dispatch queue to which the block will be submitted for asynchronous
 * invocation.
 *
 * @param block
 * The block to perform asynchronously.
 * 将一个块提交到调度队列，并将该块与给定的调度组关联。调度组可用于等待其引用的块的完成。
 
  group：与提交的块关联的调度组。在此参数中传递 NULL 的结果是未定义的。

  queue：块将提交到该调度队列以进行异步调用。

  block：该块异步执行。
 */
//将一个块提交到调度队列，并将该块与给定的调度组关联。
#ifdef __BLOCKS__
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
void
dispatch_group_async(dispatch_group_t group,
	dispatch_queue_t queue,
	dispatch_block_t block);
#endif /* __BLOCKS__ */

/*!
 * @function dispatch_group_async_f
 *
 * @abstract
 * Submits a function to a dispatch queue and associates the block with the
 * given dispatch group.
 *
 * @discussion
 * See dispatch_group_async() for details.
 *
 * @param group
 * A dispatch group to associate with the submitted function.
 * The result of passing NULL in this parameter is undefined.
 *
 * @param queue
 * The dispatch queue to which the function will be submitted for asynchronous
 * invocation.
 *
 * @param context
 * The application-defined context parameter to pass to the function.
 *
 * @param work
 * The application-defined function to invoke on the target queue. The first
 * parameter passed to this function is the context provided to
 * dispatch_group_async_f().
 * 同上 dispatch_group_async 只是把 block 换为了函数。
 */
//将函数提交给调度队列，并将该函数与给定的调度组关联。
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_NONNULL2 DISPATCH_NONNULL4
DISPATCH_NOTHROW
void
dispatch_group_async_f(dispatch_group_t group,
	dispatch_queue_t queue,
	void *_Nullable context,
	dispatch_function_t work);

/*!
 * @function dispatch_group_wait
 *
 * @abstract
 * Wait synchronously until all the blocks associated with a group have
 * completed or until the specified timeout has elapsed.
 *
 * @discussion
 * This function waits for the completion of the blocks associated with the
 * given dispatch group, and returns after all blocks have completed or when
 * the specified timeout has elapsed.
 *
 * This function will return immediately if there are no blocks associated
 * with the dispatch group (i.e. the group is empty).
 *
 * The result of calling this function from multiple threads simultaneously
 * with the same dispatch group is undefined.
 *
 * After the successful return of this function, the dispatch group is empty.
 * It may either be released with dispatch_release() or re-used for additional
 * blocks. See dispatch_group_async() for more information.
 *
 * @param group
 * The dispatch group to wait on.
 * The result of passing NULL in this parameter is undefined.
 *
 * @param timeout
 * When to timeout (see dispatch_time). As a convenience, there are the
 * DISPATCH_TIME_NOW and DISPATCH_TIME_FOREVER constants.
 *
 * @result
 * Returns zero on success (all blocks associated with the group completed
 * within the specified timeout) or non-zero on error (i.e. timed out).
 * 该函数等待与给定调度组关联的块的完成，并在所有块完成或指定的超时时间结束后返回。（阻塞直到函数返回）
  如果没有与调度组关联的块（即该组为空），则此函数将立即返回。
  从多个线程同时使用同一调度组调用此函数的结果是不确定的。
  成功返回此函数后，调度组为空。可以使用 dispatch_release 释放它，也可以将其重新用于其他块。
  group：等待调度组。在此参数中传递 NULL 的结果是未定义的。
  timeout：何时超时（dispatch_time_t）。有 DISPATCH_TIME_NOW 和 DISPATCH_TIME_FOREVER 常量。
  result：成功返回零（与该组关联的所有块在指定的时间内完成），错误返回非零（即超时）。

 */
//同步等待，直到与一个组关联的所有块都已完成，或者直到指定的超时时间过去为止。


API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
long
dispatch_group_wait(dispatch_group_t group, dispatch_time_t timeout);

/*!
 * @function dispatch_group_notify
 *
 * @abstract
 * Schedule a block to be submitted to a queue when all the blocks associated
 * with a group have completed.
 *
 * @discussion
 * This function schedules a notification block to be submitted to the specified
 * queue once all blocks associated with the dispatch group have completed.
 *
 * If no blocks are associated with the dispatch group (i.e. the group is empty)
 * then the notification block will be submitted immediately.
 *
 * The group will be empty at the time the notification block is submitted to
 * the target queue. The group may either be released with dispatch_release()
 * or reused for additional operations.
 * See dispatch_group_async() for more information.
 *
 * @param group
 * The dispatch group to observe.
 * The result of passing NULL in this parameter is undefined.
 *
 * @param queue
 * The queue to which the supplied block will be submitted when the group
 * completes.
 *
 * @param block
 * The block to submit when the group completes.
 * 如果没有块与调度组相关联（即该组为空），则通知块将立即提交。
  通知块（block）提交到目标队列（queue）时，该组将为空。该组可以通过 dispatch_release 释放，也可以重新用于其他操作。
  group：观察的调度组。在此参数中传递 NULL 的结果是未定义的。
  queue：组完成后，将向其提交所提供的块的队列。
  block：组完成后要提交的 block。
 */
//当与组相关联的所有块都已完成时，计划将块提交到队列（即当与组相关联的所有块都已完成时，提交到 queue 的 block 将执行）。
#ifdef __BLOCKS__
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
void
dispatch_group_notify(dispatch_group_t group,
	dispatch_queue_t queue,
	dispatch_block_t block);
#endif /* __BLOCKS__ */

/*!
 * @function dispatch_group_notify_f
 *
 * @abstract
 * Schedule a function to be submitted to a queue when all the blocks
 * associated with a group have completed.
 *
 * @discussion
 * See dispatch_group_notify() for details.
 *
 * @param group
 * The dispatch group to observe.
 * The result of passing NULL in this parameter is undefined.
 *
 * @param context
 * The application-defined context parameter to pass to the function.
 *
 * @param work
 * The application-defined function to invoke on the target queue. The first
 * parameter passed to this function is the context provided to
 * dispatch_group_notify_f(). 同上 dispatch_group_notify 只是把 block 换为了函数。
 */
//dispatch_group_notify_f 与一个组关联的所有块均已完成后，计划将函数提交给队列。
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_NONNULL2 DISPATCH_NONNULL4
DISPATCH_NOTHROW
void
dispatch_group_notify_f(dispatch_group_t group,
	dispatch_queue_t queue,
	void *_Nullable context,
	dispatch_function_t work);

/*!
 * @function dispatch_group_enter
 *
 * @abstract
 * Manually indicate a block has entered the group
 *
 * @discussion
 * Calling this function indicates another block has joined the group through
 * a means other than dispatch_group_async(). Calls to this function must be
 * balanced with dispatch_group_leave().
 *
 * @param group
 * The dispatch group to update.
 * The result of passing NULL in this parameter is undefined.
 * 调用此函数表示另一个块已通过 dispatch_group_async 以外的其他方式加入了该组。对该函数的调用必须与 dispatch_group_leave 平衡。
 
  group：要更新的调度组。在此参数中传递 NULL 的结果是未定义的。
 */
///表示组已手动输入块。
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
void
dispatch_group_enter(dispatch_group_t group);

/*!
 * @function dispatch_group_leave
 *
 * @abstract
 * Manually indicate a block in the group has completed
 *
 * @discussion
 * Calling this function indicates block has completed and left the dispatch
 * group by a means other than dispatch_group_async().
 *
 * @param group
 * The dispatch group to update.
 * The result of passing NULL in this parameter is undefined.
 * 调用此函数表示块已完成，并且已通过 dispatch_group_async 以外的其他方式离开了调度组。
 
  group：要更新的调度组。在此参数中传递 NULL 的结果是未定义的。
 */
//手动指示组中的块已完成。
API_AVAILABLE(macos(10.6), ios(4.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
void
dispatch_group_leave(dispatch_group_t group);

__END_DECLS

DISPATCH_ASSUME_NONNULL_END

#endif
