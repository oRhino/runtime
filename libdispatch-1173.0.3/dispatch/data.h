/*
 * Copyright (c) 2009-2013 Apple Inc. All rights reserved.
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

#ifndef __DISPATCH_DATA__
#define __DISPATCH_DATA__

#ifndef __DISPATCH_INDIRECT__
#error "Please #include <dispatch/dispatch.h> instead of this file directly."
#include <dispatch/base.h> // for HeaderDoc
#endif

DISPATCH_ASSUME_NONNULL_BEGIN

__BEGIN_DECLS

/*! @header
 * Dispatch data objects describe contiguous or sparse regions of memory that
 * may be managed by the system or by the application.
 * Dispatch data objects are immutable, any direct access to memory regions
 * represented by dispatch objects must not modify that memory.
 * 调度数据对象（dispatch data objects）描述了可以由系统或应用程序管理的内存的连续或稀疏区域。调度数据对象（Dispatch data objects）是不可变的，任何直接访问由调度对象表示的内存区域都不得修改该内存。
 */

/*!
 * @typedef dispatch_data_t
 * A dispatch object representing memory regions. 代表内存区域（memory regions）的调度对象（dispatch object）。
 */
/*
 在 Swift（在 Swift 中使用 Objective-C）下宏定义展开是:

 OS_EXPORT OS_OBJECT_OBJC_RUNTIME_VISIBLE
 @interface OS_dispatch_data : NSObject
 - (instancetype)init OS_SWIFT_UNAVAILABLE("Unavailable in Swift");
 @end

 typedef OS_dispatch_data * dispatch_data_t;
 
  OS_dispatch_data 是继承自 NSObject 的类，然后 dispatch_data_t 是指向 OS_dispatch_data 的指针。

 在 Objective-C 下宏定义展开是:

 @protocol OS_dispatch_data <OS_dispatch_object>
 @end

 typedef NSObject<OS_dispatch_data> * dispatch_data_t;
 
  OS_dispatch_data 是继承自 OS_dispatch_object 协议的协议，并且为遵循该协议的 NSObject 实例对象类型的指针定义了一个 dispatch_data_t 的别名。

 在 C++ 下宏定义展开是:

 typedef struct dispatch_data_s : public dispatch_object_s {} *dispatch_data_t;
 
  dispatch_data_t 是一个指向 dispatch_data_s 结构体的指针。

 在 C（Plain C）下宏定义展开是:

 typedef struct dispatch_data_s *dispatch_data_t;
 
 dispatch_data_t 是指向 struct dispatch_data_s 的指针。
 */

DISPATCH_DATA_DECL(dispatch_data);

/*!
 * @var dispatch_data_empty
 * @discussion The singleton dispatch data object representing a zero-length
 * memory region. 表示零长度（zero-length）存储区域（memory region）的单例分发数据对象（singleton dispatch data object）。
 */
#define dispatch_data_empty \
		DISPATCH_GLOBAL_OBJECT(dispatch_data_t, _dispatch_data_empty)
API_AVAILABLE(macos(10.7), ios(5.0))
DISPATCH_EXPORT struct dispatch_data_s _dispatch_data_empty;

/*!
 * @const DISPATCH_DATA_DESTRUCTOR_DEFAULT
 * @discussion The default destructor for dispatch data objects.
 * Used at data object creation to indicate that the supplied buffer should
 * be copied into internal storage managed by the system.
 * 调度数据对象（dispatch data objects）的默认析构函数。在创建数据对象（data object）时使用，以指示应将提供的缓冲区复制到系统管理的内部存储器中。
 */

#define DISPATCH_DATA_DESTRUCTOR_DEFAULT NULL
//根据是否是 __BLOCKS__ 环境来转换 _dispatch_data_destructor_##name 为 dispatch_block_t 或者 dispatch_function_t。
#ifdef __BLOCKS__
/*! @parseOnly */
#define DISPATCH_DATA_DESTRUCTOR_TYPE_DECL(name) \
	DISPATCH_EXPORT const dispatch_block_t _dispatch_data_destructor_##name
#else
#define DISPATCH_DATA_DESTRUCTOR_TYPE_DECL(name) \
	DISPATCH_EXPORT const dispatch_function_t \
	_dispatch_data_destructor_##name
#endif /* __BLOCKS__ */

/*!
 * @const DISPATCH_DATA_DESTRUCTOR_FREE
 * @discussion The destructor for dispatch data objects created from a malloc'd
 * buffer. Used at data object creation to indicate that the supplied buffer
 * was allocated by the malloc() family and should be destroyed with free(3).
 * 从 malloc 的缓冲区创建的调度数据对象（dispatch data objects）的析构函数。在创建数据对象（data object）时使用，以指示所提供的缓冲区是由 malloc 系列函数分配的，应使用 free 销毁。
 */
#define DISPATCH_DATA_DESTRUCTOR_FREE (_dispatch_data_destructor_free)
API_AVAILABLE(macos(10.7), ios(5.0))
DISPATCH_DATA_DESTRUCTOR_TYPE_DECL(free);

/*!
 * @const DISPATCH_DATA_DESTRUCTOR_MUNMAP
 * @discussion The destructor for dispatch data objects that have been created
 * from buffers that require deallocation with munmap(2).
 * 从需要使用 munmap 释放的缓冲区，创建的调度数据对象（dispatch data objects）的析构函数。
 */
#define DISPATCH_DATA_DESTRUCTOR_MUNMAP (_dispatch_data_destructor_munmap)
API_AVAILABLE(macos(10.9), ios(7.0))
DISPATCH_DATA_DESTRUCTOR_TYPE_DECL(munmap);

#ifdef __BLOCKS__
/*!
 * @function dispatch_data_create
 * Creates a dispatch data object from the given contiguous buffer of memory. If
 * a non-default destructor is provided, ownership of the buffer remains with
 * the caller (i.e. the bytes will not be copied). The last release of the data
 * object will result in the invocation of the specified destructor on the
 * specified queue to free the buffer.
 * 从给定的连续内存缓冲区（buffer）中创建一个调度数据对象（dispatch data object）。如果提供了非默认的析构函数（non-default destructor），则缓冲区所有权归调用者所有（即不会复制字节）。数据对象（data object）的最新（last release）版本将导致在指定队列上调用指定的析构函数以释放缓冲区。

 *
 * If the DISPATCH_DATA_DESTRUCTOR_FREE destructor is provided the buffer will
 * be freed via free(3) and the queue argument ignored.
 * 如果提供了 DISPATCH_DATA_DESTRUCTOR_FREE 析构函数，则将通过 free 释放缓冲区，并且忽略队列参数。
 *
 * If the DISPATCH_DATA_DESTRUCTOR_DEFAULT destructor is provided, data object
 * creation will copy the buffer into internal memory managed by the system.
 * 如果提供了 DISPATCH_DATA_DESTRUCTOR_DEFAULT 析构函数，则数据对象的创建会将缓冲区复制到系统管理的内部存储器中。
 *
 * @param buffer	A contiguous buffer of data. 连续的数据缓冲区。
 * @param size		The size of the contiguous buffer of data. 连续数据缓冲区的大小。
 * @param queue		The queue to which the destructor should be submitted. 析构函数应提交的队列。
 * @param destructor	The destructor responsible for freeing the data when it
 *			is no longer needed. 析构函数负责在不再需要时释放数据。
 * @result		A newly created dispatch data object. 新创建的调度数据对象。
 */
API_AVAILABLE(macos(10.7), ios(5.0))
DISPATCH_EXPORT DISPATCH_RETURNS_RETAINED DISPATCH_WARN_RESULT DISPATCH_NOTHROW
dispatch_data_t
dispatch_data_create(const void *buffer,
	size_t size,
	dispatch_queue_t _Nullable queue,
	dispatch_block_t _Nullable destructor);
#endif /* __BLOCKS__ */

/*!
 * @function dispatch_data_get_size 返回由指定调度数据对象（dispatch data object）表示的内存区域的逻辑大小。
 * Returns the logical size of the memory region(s) represented by the specified
 * dispatch data object.
 *
 * @param data	The dispatch data object to query. 要查询的调度数据对象（dispatch data object）。
 * @result	The number of bytes represented by the data object. 数据对象（data object）表示的字节数。
 */
API_AVAILABLE(macos(10.7), ios(5.0))
DISPATCH_EXPORT DISPATCH_PURE DISPATCH_NONNULL1 DISPATCH_NOTHROW
size_t
dispatch_data_get_size(dispatch_data_t data);

/*!
 * @function dispatch_data_create_map
 * Maps the memory represented by the specified dispatch data object as a single
 * contiguous memory region and returns a new data object representing it.
 * If non-NULL references to a pointer and a size variable are provided, they
 * are filled with the location and extent of that region. These allow direct
 * read access to the represented memory, but are only valid until the returned
 * object is released. Under ARC, if that object is held in a variable with
 * automatic storage, care needs to be taken to ensure that it is not released
 * by the compiler before memory access via the pointer has been completed.
 * 将指定的调度数据对象（dispatch data object）表示的内存映射为单个连续的内存区域，并返回表示该内存区域的新数据对象。如果提供了对指针和大小变量的非 NULL 引用，则将使用该区域的位置和范围填充它们。这些允许对表示的内存进行直接读取访问，但是仅在释放返回的对象之前才有效。在 ARC 下，如果对象被保存在一个自动存储的变量（局部变量）中，则需要注意确保在通过指针访问内存之前编译器不会释放它。

 * @param data		The dispatch data object to map. 要映射的调度数据对象（dispatch data object）。
 * @param buffer_ptr	A pointer to a pointer variable to be filled with the
 *			location of the mapped contiguous memory region, or
 *			NULL. 指向指针变量的指针，该指针变量将使用映射的连续内存区域的位置或 NULL 填充。
 * @param size_ptr	A pointer to a size_t variable to be filled with the
 *			size of the mapped contiguous memory region, or NULL. 指向要用映射的连续内存区域的大小或 NULL 填充的 size_t 变量的指针。
 * @result		A newly created dispatch data object. 新创建的调度数据对象（dispatch data object）。
 */
API_AVAILABLE(macos(10.7), ios(5.0))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_RETURNS_RETAINED
DISPATCH_WARN_RESULT DISPATCH_NOTHROW
dispatch_data_t
dispatch_data_create_map(dispatch_data_t data,
	const void *_Nullable *_Nullable buffer_ptr,
	size_t *_Nullable size_ptr);

/*!
 * @function dispatch_data_create_concat
 * Returns a new dispatch data object representing the concatenation of the
 * specified data objects. Those objects may be released by the application
 * after the call returns (however, the system might not deallocate the memory
 * region(s) described by them until the newly created object has also been
 * released).
 *返回一个表示指定数据对象（data objects）的串联的新调度数据对象（dispatch data objec）。调用返回后，这些对象可以由应用程序释放（但是，在新创建的对象也被释放之前，系统可能不会释放由它们描述的内存区域）。
 

 * @param data1	The data object representing the region(s) of memory to place
 *		at the beginning of the newly created object. 表示要放置在新创建对象内存区域开头的数据对象。
 * @param data2	The data object representing the region(s) of memory to place
 *		at the end of the newly created object. 表示要放置在新创建对象内存区域末尾的数据对象。
 * @result	A newly created object representing the concatenation of the
 *		data1 and data2 objects. 一个新创建的对象，表示 data1 和 data2 对象的串联。
 */
API_AVAILABLE(macos(10.7), ios(5.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_RETURNS_RETAINED
DISPATCH_WARN_RESULT DISPATCH_NOTHROW
dispatch_data_t
dispatch_data_create_concat(dispatch_data_t data1, dispatch_data_t data2);

/*!
 * @function dispatch_data_create_subrange
 * Returns a new dispatch data object representing a subrange of the specified
 * data object, which may be released by the application after the call returns
 * (however, the system might not deallocate the memory region(s) described by
 * that object until the newly created object has also been released).
 * 返回表示指定数据对象子范围的新调度数据对象（dispatch data object），该对象可能在调用返回后由应用程序释放（但是，在新创建的对象也已释放之前，系统可能不会释放该对象描述的内存区域）。
 * @param data		The data object representing the region(s) of memory to
 *			create a subrange of. 表示要创建其内存区域子范围的数据对象。
 * @param offset	The offset into the data object where the subrange
 *			starts. 数据对象的偏移量表示子范围开始处。
 * @param length	The length of the range. 范围的长度。
 * @result		A newly created object representing the specified
 *			subrange of the data object. 一个新创建的对象，代表数据对象的指定子范围。
 */
API_AVAILABLE(macos(10.7), ios(5.0))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_RETURNS_RETAINED
DISPATCH_WARN_RESULT DISPATCH_NOTHROW
dispatch_data_t
dispatch_data_create_subrange(dispatch_data_t data,
	size_t offset,
	size_t length);

#ifdef __BLOCKS__
/*!
 * @typedef dispatch_data_applier_t
 * A block to be invoked for every contiguous memory region in a data object.
 * 表示为一个数据对象（data object）中的每个连续内存区域调用的块。
 * @param region	A data object representing the current region. 表示当前区域的数据对象。
 * @param offset	The logical offset of the current region to the start
 *			of the data object. 当前区域到数据对象开始的逻辑偏移量。
 * @param buffer	The location of the memory for the current region. 当前区域的内存位置。
 * @param size		The size of the memory for the current region. 当前区域的内存大小。
 * @result		A Boolean indicating whether traversal should continue. 一个布尔值，指示是否应继续遍历。
 */
typedef bool (^dispatch_data_applier_t)(dispatch_data_t region,
	size_t offset,
	const void *buffer,
	size_t size);

/*!
 * @function dispatch_data_apply
 * Traverse the memory regions represented by the specified dispatch data object
 * in logical order and invoke the specified block once for every contiguous
 * memory region encountered.
 * 以逻辑顺序遍历指定的调度数据对象（dispatch data object）表示的内存区域，并为遇到的每个连续内存区域调用一次指定的块（block）。
 *
 * Each invocation of the block is passed a data object representing the current
 * region and its logical offset, along with the memory location and extent of
 * the region. These allow direct read access to the memory region, but are only
 * valid until the passed-in region object is released. Note that the region
 * object is released by the system when the block returns, it is the
 * responsibility of the application to retain it if the region object or the
 * associated memory location are needed after the block returns.
 * 块的每次调用都会传递一个数据对象，该对象表示当前区域及其逻辑偏移量，以及该区域的存储位置和范围。它们允许对内存区域的直接读取访问，但是仅在释放传入的区域对象之前才有效。请注意，区域对象在块返回时由系统释放，如果块返回后需要区域对象或关联的内存位置，则应用程序有责任保留该对象。

 *
 * @param data		The data object to traverse. 要遍历的数据对象。
 * @param applier	The block to be invoked for every contiguous memory
 *			region in the data object. 数据对象中每个连续存储区域要调用的块。
 * @result		A Boolean indicating whether traversal completed
 *			successfully. 一个布尔值，指示遍历是否成功完成。
 */
API_AVAILABLE(macos(10.7), ios(5.0))
DISPATCH_EXPORT DISPATCH_NONNULL_ALL DISPATCH_NOTHROW
bool
dispatch_data_apply(dispatch_data_t data,
	DISPATCH_NOESCAPE dispatch_data_applier_t applier);
#endif /* __BLOCKS__ */

/*!
 * @function dispatch_data_copy_region
 * Finds the contiguous memory region containing the specified location among
 * the regions represented by the specified object and returns a copy of the
 * internal dispatch data object representing that region along with its logical
 * offset in the specified object.
 * 在由指定对象（data）表示的区域中查找包含指定位置（location）的连续内存区域，并返回表示该区域的内部调度数据对象（dispatch data object）的副本（函数返回值）及其在指定对象中的逻辑偏移量（用一个 size_t * 记录）。
 *
 * @param data		The dispatch data object to query. 要查询的调度数据对象。
 * @param location	The logical position in the data object to query. 要查询的数据对象中的逻辑位置。
 * @param offset_ptr	A pointer to a size_t variable to be filled with the
 *			logical offset of the returned region object to the
 *			start of the queried data object.
 *			指向 size_t 变量的指针，指针包含的值是返回的区域对象到查询的数据对象的起始位置的逻辑偏移量。
 * @result		A newly created dispatch data object. 新创建的调度数据对象（dispatch data object）。
 */
API_AVAILABLE(macos(10.7), ios(5.0))
DISPATCH_EXPORT DISPATCH_NONNULL1 DISPATCH_NONNULL3 DISPATCH_RETURNS_RETAINED
DISPATCH_WARN_RESULT DISPATCH_NOTHROW
dispatch_data_t
dispatch_data_copy_region(dispatch_data_t data,
	size_t location,
	size_t *offset_ptr);

__END_DECLS

DISPATCH_ASSUME_NONNULL_END

#endif /* __DISPATCH_DATA__ */
