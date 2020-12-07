/*
 * Copyright (c) 2014 Apple Inc. All rights reserved.
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

#ifndef __DISPATCH_BLOCK__
#define __DISPATCH_BLOCK__

#ifndef __DISPATCH_INDIRECT__
#error "Please #include <dispatch/dispatch.h> instead of this file directly."
#include <dispatch/base.h> // for HeaderDoc
#endif

#ifdef __BLOCKS__

/*!
 * @group Dispatch block objects
 */

DISPATCH_ASSUME_NONNULL_BEGIN

__BEGIN_DECLS

/*!
 * @typedef dispatch_block_flags_t
 * Flags to pass to the dispatch_block_create* functions.
 * 传递给 dispatch_block_create* 函数的标志。
 *
 * @const DISPATCH_BLOCK_BARRIER
 * Flag indicating that a dispatch block object should act as a barrier block
 * when submitted to a DISPATCH_QUEUE_CONCURRENT queue.
 * See dispatch_barrier_async() for details.
 * This flag has no effect when the dispatch block object is invoked directly.
 * 指示调度块对象（dispatch block object）在提交给 DISPATCH_QUEUE_CONCURRENT 队列时应充当屏障块（barrier block）的标志。有关详细信息，参考 dispatch_barrier_async。当直接调用调度块对象（dispatch block object）时，此标志无效。

 *
 * @const DISPATCH_BLOCK_DETACHED
 * Flag indicating that a dispatch block object should execute disassociated
 * from current execution context attributes such as os_activity_t
 * and properties of the current IPC request (if any). With regard to QoS class,
 * the behavior is the same as for DISPATCH_BLOCK_NO_QOS. If invoked directly,
 * the block object will remove the other attributes from the calling thread for
 * the duration of the block body (before applying attributes assigned to the
 * block object, if any). If submitted to a queue, the block object will be
 * executed with the attributes of the queue (or any attributes specifically
 * assigned to the block object). 指示应该执行与当前执行上下文属性（例如 os_activity_t 和当前 IPC 请求的属性，如果有）无关的调度块对象（dispatch block object）的标志。关于 QoS 类别，其行为与 DISPATCH_BLOCK_NO_QOS 相同。如果直接调用，则块对象将在块主体的持续时间内从调用线程中删除其他属性（在应用分配给块对象的属性（如果有）之前）。如果提交给队列，则将使用队列的属性（或专门分配给该块对象的任何属性）执行该块对象。

 * @const DISPATCH_BLOCK_ASSIGN_CURRENT
 * Flag indicating that a dispatch block object should be assigned the execution
 * context attributes that are current at the time the block object is created.
 * This applies to attributes such as QOS class, os_activity_t and properties of
 * the current IPC request (if any). If invoked directly, the block object will
 * apply these attributes to the calling thread for the duration of the block
 * body. If the block object is submitted to a queue, this flag replaces the
 * default behavior of associating the submitted block instance with the
 * execution context attributes that are current at the time of submission.
 * If a specific QOS class is assigned with DISPATCH_BLOCK_NO_QOS_CLASS or
 * dispatch_block_create_with_qos_class(), that QOS class takes precedence over
 * the QOS class assignment indicated by this flag.
 * 指示应为调度块对象分配创建块对象时当前的执行上下文属性的标志。这适用于诸如 QOS 类，os_activity_t 的属性以及当前 IPC 请求的属性（如果有）。如果直接调用，则块对象将在块主体的持续时间内将这些属性应用于调用线程。如果将块对象提交到队列，则此标志替换将提交的块实例与提交时最新的执行上下文属性相关联的默认行为。如果使用 DISPATCH_BLOCK_NO_QOS_CLASS 或 dispatch_block_create_with_qos_class 分配了特定的 QOS 类，则该 QOS 类优先于此标志指示的 QOS 类分配。
 
 *
 * @const DISPATCH_BLOCK_NO_QOS_CLASS
 * Flag indicating that a dispatch block object should be not be assigned a QOS
 * class. If invoked directly, the block object will be executed with the QOS
 * class of the calling thread. If the block object is submitted to a queue,
 * this replaces the default behavior of associating the submitted block
 * instance with the QOS class current at the time of submission.
 * This flag is ignored if a specific QOS class is assigned with
 * dispatch_block_create_with_qos_class().
 * 指示不应为调度块对象分配 QOS 类的标志。如果直接调用，则块对象将与调用线程的 QOS 类一起执行。如果将块对象提交到队列，这将替换默认行为，即在提交时将提交的块实例与当前的 QOS 类相关联。如果为特定的 QOS 类分配了 dispatch_block_create_with_qos_class，则忽略此标志。

 * @const DISPATCH_BLOCK_INHERIT_QOS_CLASS
 * Flag indicating that execution of a dispatch block object submitted to a
 * queue should prefer the QOS class assigned to the queue over the QOS class
 * assigned to the block (resp. associated with the block at the time of
 * submission). The latter will only be used if the queue in question does not
 * have an assigned QOS class, as long as doing so does not result in a QOS
 * class lower than the QOS class inherited from the queue's target queue.
 * This flag is the default when a dispatch block object is submitted to a queue
 * for asynchronous execution and has no effect when the dispatch block object
 * is invoked directly. It is ignored if DISPATCH_BLOCK_ENFORCE_QOS_CLASS is
 * also passed. 指示执行提交到队列的调度块对象的标志应优先于分配给队列的 QOS 类，而不是分配给该块的 QOS 类（提交时与该块相关联的 resp。）。仅当所讨论的队列没有分配的 QOS 类时，才使用后者，只要这样做不会导致 QOS 类低于从队列的目标队列继承的 QOS 类。当将调度块对象提交到队列以进行异步执行时，此标志是默认设置；当直接调用调度块对象时，此标志无效。如果还传递了 DISPATCH_BLOCK_ENFORCE_QOS_CLASS，则将其忽略。
 *
 * @const DISPATCH_BLOCK_ENFORCE_QOS_CLASS
 * Flag indicating that execution of a dispatch block object submitted to a
 * queue should prefer the QOS class assigned to the block (resp. associated
 * with the block at the time of submission) over the QOS class assigned to the
 * queue, as long as doing so will not result in a lower QOS class.
 * This flag is the default when a dispatch block object is submitted to a queue
 * for synchronous execution or when the dispatch block object is invoked
 * directly. 指示执行提交到队列的调度块对象的标志应优先于分配给队列的 QOS 类，而不是分配给队列的 QOS 类，分配给该块的 QOS 类（在提交时与该块相关联）不会导致较低的 QOS 等级。当将调度块对象提交到队列以进行同步执行或直接调用调度块对象时，此标志是默认设置。
 
 */
DISPATCH_OPTIONS(dispatch_block_flags, unsigned long,
	DISPATCH_BLOCK_BARRIER
			DISPATCH_ENUM_API_AVAILABLE(macos(10.10), ios(8.0)) = 0x1,
	DISPATCH_BLOCK_DETACHED
			DISPATCH_ENUM_API_AVAILABLE(macos(10.10), ios(8.0)) = 0x2,
	DISPATCH_BLOCK_ASSIGN_CURRENT
			DISPATCH_ENUM_API_AVAILABLE(macos(10.10), ios(8.0)) = 0x4,
	DISPATCH_BLOCK_NO_QOS_CLASS
			DISPATCH_ENUM_API_AVAILABLE(macos(10.10), ios(8.0)) = 0x8,
	DISPATCH_BLOCK_INHERIT_QOS_CLASS
			DISPATCH_ENUM_API_AVAILABLE(macos(10.10), ios(8.0)) = 0x10,
	DISPATCH_BLOCK_ENFORCE_QOS_CLASS
			DISPATCH_ENUM_API_AVAILABLE(macos(10.10), ios(8.0)) = 0x20,
);

/*!
 * @function dispatch_block_create
 *
 * @abstract 根据现有块（existing block）和给定的标志（flags）在堆（heap）上创建一个新的调度块对象（dispatch block object）。
 * Create a new dispatch block object on the heap from an existing block and
 * the given flags.
 *
 * @discussion 提供的块被 Block_copy 到堆中，并由新创建的调度块对象保留。
 * The provided block is Block_copy'ed to the heap and retained by the newly
 * created dispatch block object.
 *
 * The returned dispatch block object is intended to be submitted to a dispatch
 * queue with dispatch_async() and related functions, but may also be invoked
 * directly. Both operations can be performed an arbitrary number of times but
 * only the first completed execution of a dispatch block object can be waited
 * on with dispatch_block_wait() or observed with dispatch_block_notify().
 *  返回的调度块对象（dispatch block object）旨在通过 dispatch_async 和相关函数提交给调度队列，但也可以直接调用。两种操作都可以执行任意次，但只有第一次完成的调度块对象（dispatch block object）的执行才能用 dispatch_block_wait 等待，或用 dispatch_block_notify 来观察。
 *
 * If the returned dispatch block object is submitted to a dispatch queue, the
 * submitted block instance will be associated with the QOS class current at the
 * time of submission, unless one of the following flags assigned a specific QOS
 * class (or no QOS class) at the time of block creation:
 * 如果将返回的调度块对象（dispatch block object）提交给调度队列（dispatch queue），则提交的块实例（block instance）将与提交时当前的 QOS 类相关联，除非以下标志之一在分配时分配了特定的 QOS 类（或没有 QOS 类）。块创建时间：
 
 *  - DISPATCH_BLOCK_ASSIGN_CURRENT
 *  - DISPATCH_BLOCK_NO_QOS_CLASS
 *  - DISPATCH_BLOCK_DETACHED
 *  块对象将与之一起执行的 QOS 类还取决于分配给队列的 QOS 类，以及以下哪个标志被指定或默认设置：
 * The QOS class the block object will be executed with also depends on the QOS
 * class assigned to the queue and which of the following flags was specified or
 * defaulted to:
 *  - DISPATCH_BLOCK_INHERIT_QOS_CLASS (default for asynchronous execution)
 *  - DISPATCH_BLOCK_ENFORCE_QOS_CLASS (default for synchronous execution)
 * See description of dispatch_block_flags_t for details.
 *
 * If the returned dispatch block object is submitted directly to a serial queue
 * and is configured to execute with a specific QOS class, the system will make
 * a best effort to apply the necessary QOS overrides to ensure that blocks
 * submitted earlier to the serial queue are executed at that same QOS class or
 * higher. 如果返回的调度块对象（dispatch block object）被直接提交到串行队列，并且被配置为使用特定的 QOS 类执行，那么系统将尽最大努力应用必要的 QOS 覆盖，以确保先前提交到串行队列的块在相同的 QOS 类或更高的 QOS 类中执行。
 

 *
 * @param flags
 * Configuration flags for the block object.
 * Passing a value that is not a bitwise OR of flags from dispatch_block_flags_t
 * results in NULL being returned. 块对象（block object）的配置标志。传递的值与 dispatch_block_flags_t 中的标志不是按位或的结果将返回 NULL。
 *
 * @param block
 * The block to create the dispatch block object from. 从中创建调度块对象（dispatch block object）的块。
 *
 * @result
 * The newly created dispatch block object, or NULL.
 * When not building with Objective-C ARC, must be released with a -[release]
 * message or the Block_release() function. 新创建的调度块对象，或者 NULL。当不使用 Objective-C ARC 构建时，必须使用 -[release] 消息或 Block_release 函数来进行释放。
 */
API_AVAILABLE(macos(10.10), ios(8.0))
DISPATCH_EXPORT DISPATCH_NONNULL2 DISPATCH_RETURNS_RETAINED_BLOCK
DISPATCH_WARN_RESULT DISPATCH_NOTHROW
dispatch_block_t
dispatch_block_create(dispatch_block_flags_t flags, dispatch_block_t block);

/*!
 * @function dispatch_block_create_with_qos_class
 *
 * @abstract
 * Create a new dispatch block object on the heap from an existing block and
 * the given flags, and assign it the specified QOS class and relative priority.
 * 根据现有块（existing block）和给定的标志（flags）在堆（heap）上创建一个新的调度块对象（dispatch block object），并为其分配指定的 QOS 类（qos_class）和相对优先级（relative_priority）。

 *
 * @discussion
 * The provided block is Block_copy'ed to the heap and retained by the newly
 * created dispatch block object. 提供的块被 Block_copy 到堆中，并由新创建的调度块对象（dispatch block object）保留。
 *
 * The returned dispatch block object is intended to be submitted to a dispatch
 * queue with dispatch_async() and related functions, but may also be invoked
 * directly. Both operations can be performed an arbitrary number of times but
 * only the first completed execution of a dispatch block object can be waited
 * on with dispatch_block_wait() or observed with dispatch_block_notify().
 * 返回的调度块对象（dispatch block object）旨在通过 dispatch_async 和相关函数提交给调度队列，但也可以直接调用。两种操作都可以执行任意次，但只有第一次完成的调度块对象（dispatch block object）的执行才能用 dispatch_block_wait 等待，或用 dispatch_block_notify 来观察。

 *
 *
 * If invoked directly, the returned dispatch block object will be executed with
 * the assigned QOS class as long as that does not result in a lower QOS class
 * than what is current on the calling thread. 如果直接调用，则返回的调度块对象（dispatch block object）将与分配的 QOS 类一起执行，只要它不会导致其 QOS 类比调用线程上当前的类低。
 *
 * If the returned dispatch block object is submitted to a dispatch queue, the
 * QOS class it will be executed with depends on the QOS class assigned to the
 * block, the QOS class assigned to the queue and which of the following flags
 * was specified or defaulted to:
 *  - DISPATCH_BLOCK_INHERIT_QOS_CLASS: default for asynchronous execution
 *  - DISPATCH_BLOCK_ENFORCE_QOS_CLASS: default for synchronous execution
 * See description of dispatch_block_flags_t for details.
 *  如果将返回的调度块对象提交到调度队列，则将执行的 QOS 类取决于分配给该块的 QOS 类，分配给该队列的 QOS 类以及以下哪个标志被指定或默认设置。
 *
 * If the returned dispatch block object is submitted directly to a serial queue
 * and is configured to execute with a specific QOS class, the system will make
 * a best effort to apply the necessary QOS overrides to ensure that blocks
 * submitted earlier to the serial queue are executed at that same QOS class or
 * higher.
 * 如果将返回的调度块对象（dispatch block object）提交到调度队列（dispatch queue），则执行该对象所用的 QOS 类取决于分配给块的QOS类、分配给队列的 QOS 类以及指定或默认为以下哪个标志：
 * DISPATCH_BLOCK_INHERIT_QOS_CLASS: default for asynchronous execution
 DISPATCH_BLOCK_ENFORCE_QOS_CLASS: default for synchronous execution

  (See description of dispatch_block_flags_t for details.)
  如果返回的调度块对象（dispatch block object）被直接提交到串行队列，并且被配置为使用特定的 QOS 类执行，那么系统将尽最大努力应用必要的 QOS 覆盖，以确保先前提交到串行队列的块在相同的 QOS 类或更高的 QOS 类中执行。


 *
 * @param flags
 * Configuration flags for the new block object.
 * Passing a value that is not a bitwise OR of flags from dispatch_block_flags_t
 * results in NULL being returned. 块对象（block object）的配置标志。传递的值与 dispatch_block_flags_t 中的标志不是按位或的结果将返回 NULL。
 *
 * @param qos_class QOS 类的值。
 * A QOS class value:
 *  - QOS_CLASS_USER_INTERACTIVE
 *  - QOS_CLASS_USER_INITIATED
 *  - QOS_CLASS_DEFAULT
 *  - QOS_CLASS_UTILITY
 *  - QOS_CLASS_BACKGROUND
 *  - QOS_CLASS_UNSPECIFIED
 * Passing QOS_CLASS_UNSPECIFIED is equivalent to specifying the
 * DISPATCH_BLOCK_NO_QOS_CLASS flag. Passing any other value results in NULL
 * being returned. 传递 QOS_CLASS_UNSPECIFIED 等效于指定 DISPATCH_BLOCK_NO_QOS_CLASS 标志。传递任何其他值都会导致返回 NULL。
 

 *
 * @param relative_priority
 * A relative priority within the QOS class. This value is a negative
 * offset from the maximum supported scheduler priority for the given class.
 * Passing a value greater than zero or less than QOS_MIN_RELATIVE_PRIORITY
 * results in NULL being returned. QOS 类中的相对优先级。该值是给定类别与最大支持的调度程序优先级的负偏移量。传递大于零或小于 QOS_MIN_RELATIVE_PRIORITY 的值将导致返回 NULL。
 *
 * @param block
 * The block to create the dispatch block object from. 从中创建调度块对象的块。
 *
 * @result
 * The newly created dispatch block object, or NULL.
 * When not building with Objective-C ARC, must be released with a -[release]
 * message or the Block_release() function. 新创建的调度块对象，或者 NULL。当不使用 Objective-C ARC 构建时，必须使用 -[release] 消息或 Block_release 函数来进行释放。
 */
API_AVAILABLE(macos(10.10), ios(8.0))
DISPATCH_EXPORT DISPATCH_NONNULL4 DISPATCH_RETURNS_RETAINED_BLOCK
DISPATCH_WARN_RESULT DISPATCH_NOTHROW
dispatch_block_t
dispatch_block_create_with_qos_class(dispatch_block_flags_t flags,
		dispatch_qos_class_t qos_class, int relative_priority,
		dispatch_block_t block);

/*!
 * @function dispatch_block_perform
 *
 * @abstract 从指定的块（block）和标志（flags）创建，同步执行（synchronously execute）和释放（release）调度块对象（dispatch block object）。
 * Create, synchronously execute and release a dispatch block object from the
 * specified block and flags.
 *
 * @discussion
 * Behaves identically to the sequence 行为与序列相同，如下:
 * <code>
 * dispatch_block_t b = dispatch_block_create(flags, block);
 * b();
 * Block_release(b);
 * </code>
 *  但可以通过内部方式更有效地实现，而无需复制指定块（block）到堆中或分配创建新的块（block）对象。
 * but may be implemented more efficiently internally by not requiring a copy
 * to the heap of the specified block or the allocation of a new block object.
 *
 * @param flags 临时块对象的配置标志。传递与 dispatch_block_flags_t 中的标志不是按位或的值，结果将是未定义的。
 * Configuration flags for the temporary block object.
 * The result of passing a value that is not a bitwise OR of flags from
 * dispatch_block_flags_t is undefined.
 *
 * @param block 从中创建临时块对象的块。
 * The block to create the temporary block object from.
 */
API_AVAILABLE(macos(10.10), ios(8.0))
DISPATCH_EXPORT DISPATCH_NONNULL2 DISPATCH_NOTHROW
void
dispatch_block_perform(dispatch_block_flags_t flags,
		DISPATCH_NOESCAPE dispatch_block_t block);

/*!
 * @function dispatch_block_wait
 *
 * @abstract 同步等待（阻塞），直到指定的分发块对象的执行完成或指定的超时时间结束为止。
 * Wait synchronously until execution of the specified dispatch block object has
 * completed or until the specified timeout has elapsed.
 *
 * @discussion
 * This function will return immediately if execution of the block object has
 * already completed. 如果块对象的执行已经完成，该函数将立即返回。
 *
 * It is not possible to wait for multiple executions of the same block object
 * with this interface; use dispatch_group_wait() for that purpose. A single
 * dispatch block object may either be waited on once and executed once,
 * or it may be executed any number of times. The behavior of any other
 * combination is undefined. Submission to a dispatch queue counts as an
 * execution, even if cancellation (dispatch_block_cancel) means the block's
 * code never runs. 不能使用此接口等待同一块对象的多次执行；为此，请使用 dispatch_group_wait。单个调度块对象（dispatch block object）可以等待一次并执行一次，也可以执行任意次数。任何其他组合的行为都是未定义的。即使取消（dispatch_block_cancel）表示该块的代码从不运行，向调度队列的提交也被视为执行。
 
 
 *
 * The result of calling this function from multiple threads simultaneously
 * with the same dispatch block object is undefined, but note that doing so
 * would violate the rules described in the previous paragraph.
 * 从多个线程同时使用同一个调度块对象同时调用此函数的结果是不确定的，但请注意，这样做将违反上一段中描述的规则。
 
 * If this function returns indicating that the specified timeout has elapsed,
 * then that invocation does not count as the one allowed wait. 如果此函数的返回值指示已超过指定的超时，则该调用不算作允许的一次等待。
 *
 * If at the time this function is called, the specified dispatch block object
 * has been submitted directly to a serial queue, the system will make a best
 * effort to apply the necessary QOS overrides to ensure that the block and any
 * blocks submitted earlier to that serial queue are executed at the QOS class
 * (or higher) of the thread calling dispatch_block_wait(). 如果在调用此函数时，指定的调度块对象已直接提交到串行队列，则系统将尽最大努力应用必要的 QOS 覆盖，以确保该块和任何较早提交给该串行队列的块在调用 dispatch_block_wait 的线程的 QOS 类（或更高版本）上执行。
 *
 * @param block
 * The dispatch block object to wait on.
 * The result of passing NULL or a block object not returned by one of the
 * dispatch_block_create* functions is undefined. 要等待的调度块对象。传递 NULL 或未由 dispatch_block_create* 函数之一返回的块对象的结果是未定义的。
 *
 * @param timeout
 * When to timeout (see dispatch_time). As a convenience, there are the
 * DISPATCH_TIME_NOW and DISPATCH_TIME_FOREVER constants.
 * 何时超时（dispatch_time）。为方便起见，有 DISPATCH_TIME_NOW 和 DISPATCH_TIME_FOREVER 常量。
 *
 * @result
 * Returns zero on success (the dispatch block object completed within the
 * specified timeout) or non-zero on error (i.e. timed out). 成功返回零（在指定的超时内完成调度块对象），错误返回非零（即超时）。
 */
API_AVAILABLE(macos(10.10), ios(8.0))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_NOTHROW
long
dispatch_block_wait(dispatch_block_t block, dispatch_time_t timeout);

/*!
 * @function dispatch_block_notify
 *
 * @abstract 计划在完成指定调度块对象（block）的执行后将通知块（notification_block）提交给队列。
 * Schedule a notification block to be submitted to a queue when the execution
 * of a specified dispatch block object has completed.
 *
 * @discussion 如果观察到的块对象（block）的执行已经完成，则此函数将立即提交通知块（notification_block）。
 * This function will submit the notification block immediately if execution of
 * the observed block object has already completed.
 *
 * It is not possible to be notified of multiple executions of the same block
 * object with this interface, use dispatch_group_notify() for that purpose. 使用此接口无法通知同一块对象的多次执行，请为此目的使用 dispatch_group_notify。
 *
 * A single dispatch block object may either be observed one or more times
 * and executed once, or it may be executed any number of times. The behavior
 * of any other combination is undefined. Submission to a dispatch queue
 * counts as an execution, even if cancellation (dispatch_block_cancel) means
 * the block's code never runs. 单个分发块对象（single dispatch block object）可以被观察一次或多次并执行一次，也可以执行任意次。任何其他组合的行为均未定义。即使取消（dispatch_block_cancel）表示该块的代码从不运行，向调度队列的提交也被视为执行。

 * If multiple notification blocks are scheduled for a single block object,
 * there is no defined order in which the notification blocks will be submitted
 * to their associated queues. 如果为单个块对象计划了多个通知块，则没有定义将通知块提交到其关联队列的顺序。
 *
 * @param block
 * The dispatch block object to observe.
 * The result of passing NULL or a block object not returned by one of the
 * dispatch_block_create* functions is undefined. 要观察的调度块对象。传递 NULL 或未由 dispatch_block_create* 函数之一返回的块对象的结果是不确定的。
 *
 * @param queue
 * The queue to which the supplied notification block will be submitted when
 * the observed block completes. 当观察到的块完成时，将向其提交所提供的通知块的队列。
 *
 * @param notification_block
 * The notification block to submit when the observed block object completes. 观察的块对象（block）完成时要提交的通知块。
 */
API_AVAILABLE(macos(10.10), ios(8.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
void
dispatch_block_notify(dispatch_block_t block, dispatch_queue_t queue,
		dispatch_block_t notification_block);

/*!
 * @function dispatch_block_cancel
 *
 * @abstract 异步取消指定的调度块对象（dispatch block object）。
 * Asynchronously cancel the specified dispatch block object.
 *
 * @discussion
 * Cancellation causes any future execution of the dispatch block object to
 * return immediately, but does not affect any execution of the block object
 * that is already in progress. 取消会使调度块对象（dispatch block object）的任何将来执行立即返回，但不影响已经在进行中的块对象的任何执行。
 *
 * Release of any resources associated with the block object will be delayed
 * until execution of the block object is next attempted (or any execution
 * already in progress completes). 与该块对象相关联的任何资源的释放将被延迟，直到下一次尝试执行该块对象（或已完成的任何执行完成）为止。
 *
 * NOTE: care needs to be taken to ensure that a block object that may be
 *       canceled does not capture any resources that require execution of the
 *       block body in order to be released (e.g. memory allocated with
 *       malloc(3) that the block body calls free(3) on). Such resources will
 *       be leaked if the block body is never executed due to cancellation.
 *       注意：需要注意确保可以取消的块对象不会捕获需要执行块体才能释放的任何资源（例如，块体调用 free（用 malloc 分配的内存））。如果由于取消而从不执行块体，则这些资源将被泄漏。
 *
 * @param block
 * The dispatch block object to cancel.
 * The result of passing NULL or a block object not returned by one of the
 * dispatch_block_create* functions is undefined. 要取消的调度块对象。传递 NULL 或未由 dispatch_block_create* 函数之一返回的块对象的结果是不确定的。
 
 */
API_AVAILABLE(macos(10.10), ios(8.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
void
dispatch_block_cancel(dispatch_block_t block);

/*!
 * @function dispatch_block_testcancel
 *
 * @abstract 测试给定的调度块对象（dispatch block object）是否已取消。
 * Tests whether the given dispatch block object has been canceled.
 *
 * @param block
 * The dispatch block object to test.
 * The result of passing NULL or a block object not returned by one of the
 * dispatch_block_create* functions is undefined. 要测试的调度块对象。传递 NULL 或未由 dispatch_block_create* 函数之一返回的块对象的结果是未定义的。
 *
 * @result 如果取消，则返回非零；如果未取消，返回零。
 * Non-zero if canceled and zero if not canceled.
 */
API_AVAILABLE(macos(10.10), ios(8.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_WARN_RESULT DISPATCH_PURE
DISPATCH_NOTHROW
long
dispatch_block_testcancel(dispatch_block_t block);

__END_DECLS

DISPATCH_ASSUME_NONNULL_END

#endif // __BLOCKS__

#endif // __DISPATCH_BLOCK__
