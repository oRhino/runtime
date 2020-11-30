/*
 * Copyright (c) 2019 Apple Inc.  All Rights Reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#ifndef _NSOBJECT_INTERNAL_H
#define _NSOBJECT_INTERNAL_H

/*
 * WARNING  DANGER  HAZARD  BEWARE  EEK
 *
 * Everything in this file is for Apple Internal use only.
 * These will change in arbitrary OS updates and in unpredictable ways.
 * When your program breaks, you get to keep both pieces.
 */

/*
 * NSObject-internal.h: Private SPI for use by other system frameworks.
 */
//此文件的任何内容都是 Apple 内部使用的，它们可能在任何的版本更新中以不可预测的方式修改文件里面的内容。

/***********************************************************************
   Autorelease pool implementation 自动释放池的实现原理

   A thread's autorelease pool is a stack of pointers.
   Each pointer is either an object to release, or POOL_BOUNDARY which is
	 an autorelease pool boundary.
   A pool token is a pointer to the POOL_BOUNDARY for that pool. When
	 the pool is popped, every object hotter than the sentinel is released.
   The stack is divided into a doubly-linked list of pages. Pages are added
	 and deleted as necessary.
   Thread-local storage points to the hot page, where newly autoreleased
	 objects are stored.
 
  一个线程的自动释放池就是一个存放指针的栈（自动释放池整体结构是由 AutoreleasePoolPage 组成的双向链表，而每个 AutoreleasePoolPage 里面则有一个存放对象指针的栈）。栈里面的每个指针要么是等待 autorelease 的对象，要么是 POOL_BOUNDARY 自动释放池边界（实际为 #define POOL_BOUNDARY nil，同时也是 next 的指向）。一个 pool token 是指向 POOL_BOUNDARY 的指针。当自动释放池执行pop,在边界之后添加的对象将会调用release进行释放,这些栈分散位于由 AutoreleasePoolPage 构成的双向链表中。AutoreleasePoolPage 会根据需要进行添加和删除。hotPage 保存在当前线程中，当有新的 autorelease 对象添加进自动释放池时会被添加到 hotPage。
 
  
**********************************************************************/

// structure version number. Only bump if ABI compatability is broken
#define AUTORELEASEPOOL_VERSION 1 //自动释放池的版本号，仅当 ABI 的兼容性被打破时才会改变。

// Set this to 1 to mprotect() autorelease pool contents
// 将此设置为 1 即可进行 mprotect() 自动释放池的内容。（mprotect() 可设置自动释放池的内存区域的保护属性，限制该内存区域只可读或者可读可写）
#define PROTECT_AUTORELEASEPOOL 0

// Set this to 1 to validate the entire autorelease pool header all the time
// (i.e. use check() instead of fastcheck() everywhere)
// 将此设置为 1 要在所有时刻都完整验证自动释放池的 header。（也就是 magic_t 的 check() 和 fastcheck()，完整验证数组的 4 个元素全部相等，还是只要验证第一个元素相等，当设置为 1 在任何地方使用 check() 代替 fastcheck()，可看出在 Debug 状态下是进行的完整验证，其它情况都是快速验证）
#define CHECK_AUTORELEASEPOOL (DEBUG)

#ifdef __cplusplus
#include <string.h>
#include <assert.h>
#include <objc/objc.h>
#include <pthread.h>


#ifndef C_ASSERT
	#if __has_feature(cxx_static_assert)
		#define C_ASSERT(expr) static_assert(expr, "(" #expr ")!")
	#elif __has_feature(c_static_assert)
		#define C_ASSERT(expr) _Static_assert(expr, "(" #expr ")!")
	#else
		#define C_ASSERT(expr)
	#endif
#endif

// Make ASSERT work when objc-private.h hasn't been included.
#ifndef ASSERT
#define ASSERT(x) assert(x)
#endif

struct magic_t {
    // 静态不可变 32 位 int 值
	static const uint32_t M0 = 0xA1A1A1A1;
    
#   define M1 "AUTORELEASE!"
    // m 数组占用16个字节，每个uint32_t占4个字节，减去第一个元素的4剩下是 12
	static const size_t M1_len = 12;
    // 长度为 4 的 uint32_t 数组
	uint32_t m[4];

    // magic_t 的构造函数
	magic_t() {
        // 都是 12
		ASSERT(M1_len == strlen(M1));
        // 12 = 3 * 4
		ASSERT(M1_len == 3 * sizeof(m[1]));

        // m 数组第一个元素是 M0
		m[0] = M0;
        // 把 M1 复制到从 m[1] 开始的往后 12 个字节内
        // 那么 m 数组，前面 4 个字节放数字 M0 然后后面 12 个字节放字符串 AUTORELEASE!
		strncpy((char *)&m[1], M1, M1_len);
	}

    // 析构函数
	~magic_t() {
		// Clear magic before deallocation.
        // magic_t 在 deallocation 之前清理数据。
		// This prevents some false positives in memory debugging tools.
        // 这样可以防止内存调试工具出现误报。
		// fixme semantically this should be memset_s(), but the
		// compiler doesn't optimize that at all (rdar://44856676).
        // fixme 从语义上讲，这应该是 memset_s（），但是编译器根本没有对其进行优化。
        
        // 把 m 转化为一个 uint64_t 的数组， uint64_t 类型占 8 个字节，
        // 即把原本 4 个元素每个元素 4 个字节共 16 个字节的数组转化成了 2 个元素每个元素 8 个字节共 16 个字节的数组。
		volatile uint64_t *p = (volatile uint64_t *)m;
        
        // 16 个字节置 0
		p[0] = 0; p[1] = 0;
	}

	bool check() const {
        // 检测
        // 0 元素是 M0，后面 12 个字节是 M1，和构造函数中初始化的值一模一样的话即返回 true
		return (m[0] == M0 && 0 == strncmp((char *)&m[1], M1, M1_len));
	}

	bool fastcheck() const {
#if CHECK_AUTORELEASEPOOL
        // 程序在 DEBUG 模式下执行完整比较
		return check();
#else
		return (m[0] == M0);
#endif
	}

// M1 解除宏定义
#   undef M1
};

// 前向声明，AutoreleasePoolPage 是私有继承自 AutoreleasePoolPageData 的类，
// 在 AutoreleasePoolPageData 中要声明 AutoreleasePoolPage 类型的成员变量，
// 即双向链表中使用的两个指针 parent 和 child。

class AutoreleasePoolPage;
struct AutoreleasePoolPageData
{
    // struct magic_t 作为 AutoreleasePoolPage 的 header 来验证 AutoreleasePoolPage
    // 0xA1A1A1A1AUTORELEASE!
	magic_t const magic;
    // __unsafe_unretained 修饰的 next，
    // next 指针作为游标指向栈顶最新 add 进来的 autorelease 对象的下一个位置
	__unsafe_unretained id *next; //栈当中下一个可以填充的位置
    
    // typedef __darwin_pthread_t pthread_t
    // typedef struct _opaque_pthread_t *__darwin_pthread_t
    // 原始是 struct _opaque_pthread_t 指针
    // AutoreleasePool 是按线程一一对应的，thread 是自动释放池所处的线程。
	pthread_t const thread; //线程 一对一
    
    // AutoreleasePool 没有单独的结构，而是由若干个 AutoreleasePoolPage 以双向链表的形式组合而成，
    // parent 和 child 这两个 AutoreleasePoolPage 指针正是构成链表用的值指针。
	AutoreleasePoolPage * const parent; //父指针
	AutoreleasePoolPage *child; //孩子指针
    
    // 标记每个指针的深度，例如第一个 page 的 depth 是 0，后续新增的 page 的 depth 依次递增
	uint32_t const depth;
    // high-water
	uint32_t hiwat;

    // 构造函数
    // 初始化列表中 parent 根据 _parent 初始化，child 初始化为 nil
    // 这里可以看出，第一个 page 的 parent 和 child 都是 nil
    // 然后第二个 page 初始化时第一个 page 作为它的 parent 传入
    // 然后第一个 page 的 child 指向 第二个 page，parent 指向 nil
    // 第二个 page 的 parent 指向第一个 page，child 此时指向 nil
	AutoreleasePoolPageData(__unsafe_unretained id* _next, pthread_t _thread, AutoreleasePoolPage* _parent, uint32_t _depth, uint32_t _hiwat)
		: magic(), next(_next), thread(_thread),
		  parent(_parent), child(nil),
		  depth(_depth), hiwat(_hiwat)
	{
	}
};


struct thread_data_t
{
#ifdef __LP64__
	pthread_t const thread; // pthread_t 的实际类型是 struct _opaque_pthread_t 的指针，占 8 个字节
	uint32_t const hiwat; // 4 字节
	uint32_t const depth; // 4 字节
#else
	pthread_t const thread;
	uint32_t const hiwat;
	uint32_t const depth;
	uint32_t padding;
#endif
};
// 一个断言，如果 thread_data_t 的 size 不是 16 的话就会执行该断言
// 可以看到在 __LP64__ 平台同时遵循内存对齐原则下 thread_data_t size 也正是 8 + 4 + 4 = 16
C_ASSERT(sizeof(thread_data_t) == 16);

#undef C_ASSERT

#endif
#endif
