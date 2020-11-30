/*
 * Copyright (c) 2010-2012 Apple Inc. All rights reserved.
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

#include "objc-private.h"
#include "NSObject.h"

#include "objc-weak.h"
#include "DenseMapExtras.h"

#include <malloc/malloc.h>
#include <stdint.h>
#include <stdbool.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/nlist.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <Block.h>
#include <map>
#include <execinfo.h>
#include "NSObject-internal.h"

@interface NSInvocation
- (SEL)selector;
@end

OBJC_EXTERN const uint32_t objc_debug_autoreleasepoolpage_magic_offset  = __builtin_offsetof(AutoreleasePoolPageData, magic);
OBJC_EXTERN const uint32_t objc_debug_autoreleasepoolpage_next_offset   = __builtin_offsetof(AutoreleasePoolPageData, next);
OBJC_EXTERN const uint32_t objc_debug_autoreleasepoolpage_thread_offset = __builtin_offsetof(AutoreleasePoolPageData, thread);
OBJC_EXTERN const uint32_t objc_debug_autoreleasepoolpage_parent_offset = __builtin_offsetof(AutoreleasePoolPageData, parent);
OBJC_EXTERN const uint32_t objc_debug_autoreleasepoolpage_child_offset  = __builtin_offsetof(AutoreleasePoolPageData, child);
OBJC_EXTERN const uint32_t objc_debug_autoreleasepoolpage_depth_offset  = __builtin_offsetof(AutoreleasePoolPageData, depth);
OBJC_EXTERN const uint32_t objc_debug_autoreleasepoolpage_hiwat_offset  = __builtin_offsetof(AutoreleasePoolPageData, hiwat);
#if __OBJC2__
OBJC_EXTERN const uint32_t objc_class_abi_version = OBJC_CLASS_ABI_VERSION_MAX;
#endif

/***********************************************************************
* Weak ivar support
**********************************************************************/

static id defaultBadAllocHandler(Class cls)
{
    _objc_fatal("attempt to allocate object of class '%s' failed", 
                cls->nameForLogging());
}

id(*badAllocHandler)(Class) = &defaultBadAllocHandler;

id _objc_callBadAllocHandler(Class cls)
{
    // fixme add re-entrancy protection in case allocation fails inside handler
    return (*badAllocHandler)(cls);
}

void _objc_setBadAllocHandler(id(*newHandler)(Class))
{
    badAllocHandler = newHandler;
}


namespace {

// The order of these bits is important.
#define SIDE_TABLE_WEAKLY_REFERENCED (1UL<<0)
#define SIDE_TABLE_DEALLOCATING      (1UL<<1)  // MSB-ward of weak bit
#define SIDE_TABLE_RC_ONE            (1UL<<2)  // MSB-ward of deallocating bit
#define SIDE_TABLE_RC_PINNED         (1UL<<(WORD_BITS-1))

#define SIDE_TABLE_RC_SHIFT 2
#define SIDE_TABLE_FLAG_MASK (SIDE_TABLE_RC_ONE-1)

struct RefcountMapValuePurgeable {
    static inline bool isPurgeable(size_t x) {
        return x == 0;
    }
};

// RefcountMap disguises its pointers because we 
// don't want the table to act as a root for `leaks`.
typedef objc::DenseMap<DisguisedPtr<objc_object>,size_t,RefcountMapValuePurgeable> RefcountMap;

// Template parameters.
enum HaveOld { DontHaveOld = false, DoHaveOld = true };
enum HaveNew { DontHaveNew = false, DoHaveNew = true };

#pragma mark - 引用计数表
struct SideTable {
    spinlock_t slock; //自旋锁 忙等的锁,轻量访问
    RefcountMap refcnts; //哈希表 引用计数
    weak_table_t weak_table; //弱引用表

    SideTable() {
        memset(&weak_table, 0, sizeof(weak_table));
    }

    ~SideTable() {
        _objc_fatal("Do not delete SideTable.");
    }

    void lock() { slock.lock(); }
    void unlock() { slock.unlock(); }
    void forceReset() { slock.forceReset(); }

    // Address-ordered lock discipline for a pair of side tables.

    template<HaveOld, HaveNew>
    static void lockTwo(SideTable *lock1, SideTable *lock2);
    template<HaveOld, HaveNew>
    static void unlockTwo(SideTable *lock1, SideTable *lock2);
};


template<>
void SideTable::lockTwo<DoHaveOld, DoHaveNew>
    (SideTable *lock1, SideTable *lock2)
{
    spinlock_t::lockTwo(&lock1->slock, &lock2->slock);
}

template<>
void SideTable::lockTwo<DoHaveOld, DontHaveNew>
    (SideTable *lock1, SideTable *)
{
    lock1->lock();
}

template<>
void SideTable::lockTwo<DontHaveOld, DoHaveNew>
    (SideTable *, SideTable *lock2)
{
    lock2->lock();
}

template<>
void SideTable::unlockTwo<DoHaveOld, DoHaveNew>
    (SideTable *lock1, SideTable *lock2)
{
    spinlock_t::unlockTwo(&lock1->slock, &lock2->slock);
}

template<>
void SideTable::unlockTwo<DoHaveOld, DontHaveNew>
    (SideTable *lock1, SideTable *)
{
    lock1->unlock();
}

template<>
void SideTable::unlockTwo<DontHaveOld, DoHaveNew>
    (SideTable *, SideTable *lock2)
{
    lock2->unlock();
}

#pragma mark - SideTables
static objc::ExplicitInit<StripedMap<SideTable>> SideTablesMap;


static StripedMap<SideTable>& SideTables() {
    return SideTablesMap.get();
}

// anonymous namespace
};

void SideTableLockAll() {
    SideTables().lockAll();
}

void SideTableUnlockAll() {
    SideTables().unlockAll();
}

void SideTableForceResetAll() {
    SideTables().forceResetAll();
}

void SideTableDefineLockOrder() {
    SideTables().defineLockOrder();
}

void SideTableLocksPrecedeLock(const void *newlock) {
    SideTables().precedeLock(newlock);
}

void SideTableLocksSucceedLock(const void *oldlock) {
    SideTables().succeedLock(oldlock);
}

void SideTableLocksPrecedeLocks(StripedMap<spinlock_t>& newlocks) {
    int i = 0;
    const void *newlock;
    while ((newlock = newlocks.getLock(i++))) {
        SideTables().precedeLock(newlock);
    }
}

void SideTableLocksSucceedLocks(StripedMap<spinlock_t>& oldlocks) {
    int i = 0;
    const void *oldlock;
    while ((oldlock = oldlocks.getLock(i++))) {
        SideTables().succeedLock(oldlock);
    }
}

//
// The -fobjc-arc flag causes the compiler to issue calls to objc_{retain/release/autorelease/retain_block}
//

id objc_retainBlock(id x) {
    return (id)_Block_copy(x);
}

//
// The following SHOULD be called by the compiler directly, but the request hasn't been made yet :-)
//

BOOL objc_should_deallocate(id object) {
    return YES;
}

id
objc_retain_autorelease(id obj)
{
    return objc_autorelease(objc_retain(obj));
}


void
objc_storeStrong(id *location, id obj)
{
    id prev = *location;
    if (obj == prev) {
        return;
    }
    objc_retain(obj);
    *location = obj;
    objc_release(prev);
}


// Update a weak variable.
// If HaveOld is true, the variable has an existing value 
//   that needs to be cleaned up. This value might be nil.
// If HaveNew is true, there is a new value that needs to be 
//   assigned into the variable. This value might be nil.
// If CrashIfDeallocating is true, the process is halted if newObj is 
//   deallocating or newObj's class does not support weak references. 
//   If CrashIfDeallocating is false, nil is stored instead.
enum CrashIfDeallocating {
    DontCrashIfDeallocating = false, DoCrashIfDeallocating = true
};
template <HaveOld haveOld, HaveNew haveNew,
          CrashIfDeallocating crashIfDeallocating>
static id 
storeWeak(id *location, objc_object *newObj)
{
    ASSERT(haveOld  ||  haveNew);
    if (!haveNew) ASSERT(newObj == nil);

    Class previouslyInitializedClass = nil;
    id oldObj;
    SideTable *oldTable;
    SideTable *newTable;

    // Acquire locks for old and new values.
    // Order by lock address to prevent lock ordering problems. 
    // Retry if the old value changes underneath us.
 retry:
    if (haveOld) {
        oldObj = *location;
        oldTable = &SideTables()[oldObj];
    } else {
        oldTable = nil;
    }
    if (haveNew) {
        newTable = &SideTables()[newObj];
    } else {
        newTable = nil;
    }

    SideTable::lockTwo<haveOld, haveNew>(oldTable, newTable);

    if (haveOld  &&  *location != oldObj) {
        SideTable::unlockTwo<haveOld, haveNew>(oldTable, newTable);
        goto retry;
    }

    // Prevent a deadlock between the weak reference machinery
    // and the +initialize machinery by ensuring that no 
    // weakly-referenced object has an un-+initialized isa.
    if (haveNew  &&  newObj) {
        Class cls = newObj->getIsa();
        if (cls != previouslyInitializedClass  &&  
            !((objc_class *)cls)->isInitialized()) 
        {
            SideTable::unlockTwo<haveOld, haveNew>(oldTable, newTable);
            class_initialize(cls, (id)newObj);

            // If this class is finished with +initialize then we're good.
            // If this class is still running +initialize on this thread 
            // (i.e. +initialize called storeWeak on an instance of itself)
            // then we may proceed but it will appear initializing and 
            // not yet initialized to the check above.
            // Instead set previouslyInitializedClass to recognize it on retry.
            previouslyInitializedClass = cls;

            goto retry;
        }
    }

    // Clean up old value, if any.
    if (haveOld) {
        weak_unregister_no_lock(&oldTable->weak_table, oldObj, location);
    }

    // Assign new value, if any.
    if (haveNew) {
        newObj = (objc_object *)
            weak_register_no_lock(&newTable->weak_table, (id)newObj, location, 
                                  crashIfDeallocating);
        // weak_register_no_lock returns nil if weak store should be rejected

        // Set is-weakly-referenced bit in refcount table.
        if (newObj  &&  !newObj->isTaggedPointer()) {
            newObj->setWeaklyReferenced_nolock();
        }

        // Do not set *location anywhere else. That would introduce a race.
        *location = (id)newObj;
    }
    else {
        // No new value. The storage is not changed.
    }
    
    SideTable::unlockTwo<haveOld, haveNew>(oldTable, newTable);

    return (id)newObj;
}


/** 
 * This function stores a new value into a __weak variable. It would
 * be used anywhere a __weak variable is the target of an assignment.
 * 
 * @param location The address of the weak pointer itself
 * @param newObj The new object this weak ptr should now point to
 * 
 * @return \e newObj
 */
id
objc_storeWeak(id *location, id newObj)
{
    return storeWeak<DoHaveOld, DoHaveNew, DoCrashIfDeallocating>
        (location, (objc_object *)newObj);
}


/** 
 * This function stores a new value into a __weak variable. 
 * If the new object is deallocating or the new object's class 
 * does not support weak references, stores nil instead.
 * 
 * @param location The address of the weak pointer itself
 * @param newObj The new object this weak ptr should now point to
 * 
 * @return The value stored (either the new object or nil)
 */
id
objc_storeWeakOrNil(id *location, id newObj)
{
    return storeWeak<DoHaveOld, DoHaveNew, DontCrashIfDeallocating>
        (location, (objc_object *)newObj);
}


/** 
 * Initialize a fresh weak pointer to some object location. 
 * It would be used for code like: 
 *
 * (The nil case) 
 * __weak id weakPtr;
 * (The non-nil case) 
 * NSObject *o = ...;
 * __weak id weakPtr = o;
 * 
 * This function IS NOT thread-safe with respect to concurrent 
 * modifications to the weak variable. (Concurrent weak clear is safe.)
 *
 * @param location Address of __weak ptr.  弱引用变量
 * @param newObj Object ptr.  被弱引用的对象
 */

#pragma mark - 初始化弱引用指针
id
objc_initWeak(id *location, id newObj)
{
    if (!newObj) {
        *location = nil;
        return nil;
    }
 ///false true true
    return storeWeak<DontHaveOld, DoHaveNew, DoCrashIfDeallocating>
        (location, (objc_object*)newObj);
}

id
objc_initWeakOrNil(id *location, id newObj)
{
    if (!newObj) {
        *location = nil;
        return nil;
    }

    return storeWeak<DontHaveOld, DoHaveNew, DontCrashIfDeallocating>
        (location, (objc_object*)newObj);
}


/** 
 * Destroys the relationship between a weak pointer
 * and the object it is referencing in the internal weak
 * table. If the weak pointer is not referencing anything, 
 * there is no need to edit the weak table. 
 *
 * This function IS NOT thread-safe with respect to concurrent 
 * modifications to the weak variable. (Concurrent weak clear is safe.)
 * 
 * @param location The weak pointer address. 
 */
void
objc_destroyWeak(id *location)
{
    (void)storeWeak<DoHaveOld, DontHaveNew, DontCrashIfDeallocating>
        (location, nil);
}


/*
  Once upon a time we eagerly cleared *location if we saw the object 
  was deallocating. This confuses code like NSPointerFunctions which 
  tries to pre-flight the raw storage and assumes if the storage is 
  zero then the weak system is done interfering. That is false: the 
  weak system is still going to check and clear the storage later. 
  This can cause objc_weak_error complaints and crashes.
  So we now don't touch the storage until deallocation completes.
*/

id
objc_loadWeakRetained(id *location)
{
    id obj;
    id result;
    Class cls;

    SideTable *table;
    
 retry:
    // fixme std::atomic this load
    obj = *location;
    if (!obj) return nil;
    if (obj->isTaggedPointer()) return obj;
    
    table = &SideTables()[obj];
    
    table->lock();
    if (*location != obj) {
        table->unlock();
        goto retry;
    }
    
    result = obj;

    cls = obj->ISA();
    if (! cls->hasCustomRR()) {
        // Fast case. We know +initialize is complete because
        // default-RR can never be set before then.
        ASSERT(cls->isInitialized());
        if (! obj->rootTryRetain()) {
            result = nil;
        }
    }
    else {
        // Slow case. We must check for +initialize and call it outside
        // the lock if necessary in order to avoid deadlocks.
        if (cls->isInitialized() || _thisThreadIsInitializingClass(cls)) {
            BOOL (*tryRetain)(id, SEL) = (BOOL(*)(id, SEL))
                class_getMethodImplementation(cls, @selector(retainWeakReference));
            if ((IMP)tryRetain == _objc_msgForward) {
                result = nil;
            }
            else if (! (*tryRetain)(obj, @selector(retainWeakReference))) {
                result = nil;
            }
        }
        else {
            table->unlock();
            class_initialize(cls, obj);
            goto retry;
        }
    }
        
    table->unlock();
    return result;
}

/** 
 * This loads the object referenced by a weak pointer and returns it, after
 * retaining and autoreleasing the object to ensure that it stays alive
 * long enough for the caller to use it. This function would be used
 * anywhere a __weak variable is used in an expression.
 * 
 * @param location The weak pointer address
 * 
 * @return The object pointed to by \e location, or \c nil if \e location is \c nil.
 */
id
objc_loadWeak(id *location)
{
    if (!*location) return nil;
    return objc_autorelease(objc_loadWeakRetained(location));
}


/** 
 * This function copies a weak pointer from one location to another,
 * when the destination doesn't already contain a weak pointer. It
 * would be used for code like:
 *
 *  __weak id src = ...;
 *  __weak id dst = src;
 * 
 * This function IS NOT thread-safe with respect to concurrent 
 * modifications to the destination variable. (Concurrent weak clear is safe.)
 *
 * @param dst The destination variable.
 * @param src The source variable.
 */
void
objc_copyWeak(id *dst, id *src)
{
    id obj = objc_loadWeakRetained(src);
    objc_initWeak(dst, obj);
    objc_release(obj);
}

/** 
 * Move a weak pointer from one location to another.
 * Before the move, the destination must be uninitialized.
 * After the move, the source is nil.
 *
 * This function IS NOT thread-safe with respect to concurrent 
 * modifications to either weak variable. (Concurrent weak clear is safe.)
 *
 */
void
objc_moveWeak(id *dst, id *src)
{
    objc_copyWeak(dst, src);
    objc_destroyWeak(src);
    *src = nil;
}


/***********************************************************************
   Autorelease pool implementation

   A thread's autorelease pool is a stack of pointers. 
   Each pointer is either an object to release, or POOL_BOUNDARY which is 
     an autorelease pool boundary.
   A pool token is a pointer to the POOL_BOUNDARY for that pool. When 
     the pool is popped, every object hotter than the sentinel is released.
   The stack is divided into a doubly-linked list of pages. Pages are added 
     and deleted as necessary. 
   Thread-local storage points to the hot page, where newly autoreleased 
     objects are stored. 
**********************************************************************/

BREAKPOINT_FUNCTION(void objc_autoreleaseNoPool(id obj));
BREAKPOINT_FUNCTION(void objc_autoreleasePoolInvalid(const void *token));

#pragma mark - 自动释放池
//AutoreleasePoolPage 是一个私有继承自 AutoreleasePoolPageData 的类。 thread_data_t 是 AutoreleasePoolPage 的友元结构体，可直接访问 AutoreleasePoolPage 的私有成员变量。

class AutoreleasePoolPage : private AutoreleasePoolPageData
{
	friend struct thread_data_t;

// 表示 AutoreleasePoolPage 的容量。已知在 NSObject-internal.h 中 PROTECT_AUTORELEASEPOOL 值为 0，那么 SIZE 的值是 PAGE_MIN_SIZE。（在 vm_param.h 中 PAGE_MAX_SIZE 和 PAGE_MIN_SIZE 都是 4096...）
//保存的 autorelease 对象的指针，每个指针占 8 个字节）。
public:
	static size_t const SIZE =
#if PROTECT_AUTORELEASEPOOL
		PAGE_MAX_SIZE;  // must be multiple of vm page size
#else
		PAGE_MIN_SIZE;  // size and alignment, power of 2
#endif
    
private:
    // 通过此 key 从当前线程的存储中取出 hotPage
	static pthread_key_t const key = AUTORELEASE_POOL_KEY; //// pthread_key_t 实际是一个 unsigned long 类型
    
    // SCRIBBLE
    // 在 releaseUntil 函数中，page 中的 objc_object ** 指向的对象执行 objc_release，
    // 然后它们留空的位置会放 SCRIBBLE
    // 也就是说通过 objc_objcect ** 把指向的对象执行 release 后，
    // 把之前存放 objc_object ** 的位置放 SCRIBBLE
	static uint8_t const SCRIBBLE = 0xA3;  // 0xA3A3A3A3 after releasing
    
    /// 可保存的 id 的数量 4096 / 8 = 512 (实际可用容量是 4096 减去成员变量占用的 56 字节 )
	static size_t const COUNT = SIZE / sizeof(id);

    // EMPTY_POOL_PLACEHOLDER is stored in TLS when exactly one pool is 
    // pushed and it has never contained any objects. This saves memory 
    // when the top level (i.e. libdispatch) pushes and pops pools but 
    // never uses them.
    // 当创建了一个自动释放池且未放入任何对象的时候 EMPTY_POOL_PLACEHOLDER 就会存储在 TLS 中。
    // 当 top level(例如 libdispatch) pushes 和 pools 却从不使用它们的时候可以节省内存。
    
    // 把 1 转为 objc_object **
#   define EMPTY_POOL_PLACEHOLDER ((id*)1)

    // pool 的边界是指一个 nil
#   define POOL_BOUNDARY nil

    // SIZE-sizeof(*this) bytes of contents follow

    // 申请空间并进行内存对齐
    static void * operator new(size_t size) {
        // extern malloc_zone_t *malloc_default_zone(void); /* The initial zone */ // 初始 zone
        // extern void *malloc_zone_memalign(malloc_zone_t *zone,
        //                                   size_t alignment,
        //                                   size_t size)
        //                                   __alloc_size(3) __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_0);
        // alignment 对齐长度
        // 分配一个大小为 size 的新指针，其地址是对齐的精确倍数。
        // 对齐方式必须是 2 的幂并且至少与 sizeof(void *) 一样大。 zone 必须为非 NULL。
        return malloc_zone_memalign(malloc_default_zone(), SIZE, SIZE);
    }
    // 释放内存
    static void operator delete(void * p) {
        return free(p);
    }

    //在 Linux 中 mprotect() 函数可以用来修改一段指定内存区域的保护属性。例如指定一块区域只可读、只可写、可读可写等等
    
    /*
     mprotect() 函数把自 start 开始的、长度为 len 的内存区的保护属性修改为 prot 指定的值。prot 可以取以下几个值，并且可以用 | 将几个属性合起来使用：

    PROT_READ：表示内存段内的内容可读。
    PROT_WRITE：表示内存段内的内容可写。
    PROT_EXEC：表示内存段中的内容可执行。
    PROT_NONE：表示内存段中的内容根本没法访问。
     */
    inline void protect() {
#if PROTECT_AUTORELEASEPOOL
        // 从 this 开始的长度为 SIZE 的内存区域只可读
        mprotect(this, SIZE, PROT_READ);
        check();
#endif
    }

    inline void unprotect() {
#if PROTECT_AUTORELEASEPOOL
        check();
        // 从 this 开始的长度为 SIZE 的内粗区域可读可写
        mprotect(this, SIZE, PROT_READ | PROT_WRITE);
#endif
    }

    //构造函数
    //第一个节点的 parent  和 child 都是 nil，当第一个 AutoreleasePoolPage 满了，会再创建一个 AutoreleasePoolPage，此时会拿第一个节点作为 newParent 参数来构建这第二个节点，即第一个节点的 child 指向第二个节点，第二个节点的 parent 指向第一个节点。

	AutoreleasePoolPage(AutoreleasePoolPage *newParent) :
		AutoreleasePoolPageData(begin(),
								objc_thread_self(), // 当前所处的线程
								newParent, // parent
                                // 可以理解为 page 的深度，第一个节点的 depth 是 0，
                                // 第二个节点是 1，第三个节点是 2，依次累加
								newParent ? 1+newParent->depth : 0,
								newParent ? newParent->hiwat : 0)
    { 
        if (parent) {
            // 检查 parent 节点是否合规，检查 magic 和 thread
            parent->check();
            // parent 节点的 child 必须为 nil，因为当前新建的 page 要作为 parent 的 child
            ASSERT(!parent->child);
            // 可读可写
            parent->unprotect();
            // 把当前节点作为入参 newParent 的 child 节点
            parent->child = this;
            // 只可读
            parent->protect();
        }
        // 只可读
        protect();
    }

    // 析构函数 必须满足 empty() 和 child 指向 nil，同时还有 magic.check() 必须为真，还有 thread == objc_thread_self()，这四个条件同时满足时才能正常析构。
    ~AutoreleasePoolPage() 
    {
        check(); //检查
        unprotect();//可读可写
        ASSERT(empty()); // page 里面没有 autorelease 对象否则执行断言

        // Not recursive: we don't want to blow out the stack 
        // if a thread accumulates a stupendous amount of garbage
        ASSERT(!child); // child 指向 nil 否则执行断言
    }

    // 根据 log 参数不同会决定是 _objc_fatal 或 _objc_inform
    template<typename Fn>
    void
    busted(Fn log) const
    {
        magic_t right; // 一个完整默认值的 magic_t 变量
        // log
        log("autorelease pool page %p corrupted\n"
             "  magic     0x%08x 0x%08x 0x%08x 0x%08x\n"
             "  should be 0x%08x 0x%08x 0x%08x 0x%08x\n"
             "  pthread   %p\n"
             "  should be %p\n", 
             this, 
             magic.m[0], magic.m[1], magic.m[2], magic.m[3], 
             right.m[0], right.m[1], right.m[2], right.m[3], 
             this->thread, objc_thread_self());
    }

    __attribute__((noinline, cold, noreturn))
    void
    busted_die() const
    { // 执行 _objc_fatal 打印
        busted(_objc_fatal);
        __builtin_unreachable();
    }

    //检查 magic 是否等于默认值和检查当前所处的线程，然后 log 传递 _objc_inform 或 _objc_fatal 调用 busted 函数。
    inline void
    check(bool die = true) const
    {
        if (!magic.check() || thread != objc_thread_self()) {
            if (die) {
                busted_die();
            } else {
                busted(_objc_inform);
            }
        }
    }

    inline void
    fastcheck() const
    {
        // #define CHECK_AUTORELEASEPOOL (DEBUG) // DEBUG 模式为 true RELEASE 模式为 false
#if CHECK_AUTORELEASEPOOL
        check();
#else
        // 如果 magic.fastcheck() 失败则执行 busted_die
        if (! magic.fastcheck()) {
            busted_die();
        }
#endif
    }

    //AutoreleasePoolPage 中存放的 自动释放对象 的起点。回顾上面的的 new 函数的实现我们已知系统总共给 AutoreleasePoolPage 分配了 4096 个字节的空间，这么大的空间除了前面一部分空间用来保存 AutoreleasePoolPage 的成员变量外，剩余的空间都是用来存放自动释放对象地址的。
    //AutoreleasePoolPage 的成员变量都是继承自 AutoreleasePoolPageDate，它们总共需要 56 个字节的空间，然后剩余 4040 字节空间，一个对象指针占 8 个字节，那么一个 AutoreleasePoolPage 能存放 505 个需要自动释放的对象。（可在 main.m 中引入 #include "NSObject-internal.h" 打印 sizeof(AutoreleasePoolPageData) 的值确实是 56。）

    id * begin() {
        // (uint8_t *)this 是 AutoreleasePoolPage 的起始地址，
        // 且这里用的是 (uint8_t *) 的强制类型转换，uint8_t 占 1 个字节，
        // 然后保证 (uint8_t *)this 加 56 时是按 56 个字节前进的
        
        // sizeof(*this) 是 AutoreleasePoolPage 所有成员变量的宽度是 56 个字节，
        // 返回从 page 的起始地址开始前进 56 个字节后的内存地址。

        return (id *) ((uint8_t *)this+sizeof(*this));
    }

    // (uint8_t *)this 起始地址，转为 uint8_t 指针
    // 然后前进 SIZE 个字节，刚好到 AutoreleasePoolPage 的末尾
    id * end() {
        return (id *) ((uint8_t *)this+SIZE);
    }

    bool empty() {
        //next 指针通常指向的是当前自动释放池内最后面一个自动释放对象的后面，如果此时 next 指向 begin 的位置，表示目前自动释放池内没有存放自动释放对象。
        return next == begin();
    }

    bool full() {
        //next 指向了 end 的位置，表明自动释放池内已经存满了需要自动释放的对象。
        return next == end();
    }

    //表示目前自动释放池存储的自动释放对象是否少于总容量的一半。next 与 begin 的距离是当前存放的自动释放对象的个数，end 与 begin 的距离是可以存放自动释放对象的总容量。
    bool lessThanHalfFull() {
        return (next - begin() < (end() - begin()) / 2);
    }

    //把autorelease 对象放进自动释放池。
    id *add(id obj)
    {
        ASSERT(!full()); // 如果自动释放池已经满了，则执行断言
        unprotect(); // 可读可写
        //// 记录当前 next 的指向，作为函数的返回值。比 `return next-1` 快
        id *ret = next;  // faster than `return next-1` because of aliasing
        // next 是一个 objc_object **，先使用解引用操作符 * 取出 objc_object * ，
        // 然后把 obj 赋值给它，然后 next 会做一次自增操作前进 8 个字节，指向下一个位置。
        *next++ = obj;
        protect(); // 只可读
        return ret; // ret 目前正是指向 obj 的位置。（obj 是 objc_object 指针，不是 objc_object）
    }

    void releaseAll() 
    {
        // 调用 releaseUntil 并传入 begin，
        // 从 next 开始，一直往后移动，直到 begin，
        // 把 begin 到 next 之间的所有自动释放对象执行一次 objc_release 操作
        releaseUntil(begin());
    }

    //从 next 开始一直向后移动直到到达 stop，把经过路径上的所有自动释放对象都执行一次 objc_release 操作。
    //从最前面的 page 开始一直向后移动直到到达 stop 所在的 page，并把经过的 page 里保存的对象都执行一次 objc_release 操作，把之前每个存放 objc_object ** 的空间都置为 SCRIBBLE，每个 page 的 next 都指向了该 page 的 begin。
    void releaseUntil(id *stop) 
    {
        // Not recursive: we don't want to blow out the stack 
        // if a thread accumulates a stupendous amount of garbage
        // 循环从 next 开始，一直后退，直到 next 到达 stop
        while (this->next != stop) {
            // Restart from hotPage() every time, in case -release 
            // autoreleased more objects
            // 取得当前的 AutoreleasePoolPage
            AutoreleasePoolPage *page = hotPage();

            // fixme I think this `while` can be `if`, but I can't prove it
            // fixme :我认为 “while” 可以是 “if”，但我无法证明
            // 我觉得也是可以用 if 代替 while
            // 一个 page 满了会生成一个新的 page 并链接为下一个 page，
            // 所以从第一个 page 开始到 hotPage 的前一个page，应该都是满的

            // 如果当前 page 已经空了，则往后退一步，把前一个 AutoreleasePoolPage 作为 hotPage
            while (page->empty()) {
                // 当前 page 已经空了，还没到 stop，
                // 往后走
                page = page->parent;
                setHotPage(page); // 把 page 作为 hotPage
            }

            page->unprotect();// 可读可写
            id obj = *--page->next;// next 后移一步，并用解引用符取出 objc_object * 赋值给 obj
            // 把 page->next 开始的 sizeof(*page->next) 个字节置为 SCRIBBLE
            memset((void*)page->next, SCRIBBLE, sizeof(*page->next));
            // 只可读
            page->protect();
            
            // 如果 obj 不为 nil，则执行 objc_release 操作
            if (obj != POOL_BOUNDARY) {
                objc_release(obj);
            }
        }
        // 这里还是把 this 作为 hotPage，
        // 可能从 stop 所在的 page 开始到 hotPage 这些 page 本来存放自动释放对象的位置都放的是 SCRIBBLE
        setHotPage(this);

#if DEBUG
        // we expect any children to be completely empty
        // 保证从当前 page 的 child 开始，向后都是空 page
        for (AutoreleasePoolPage *page = child; page; page = page->child) {
            ASSERT(page->empty());
        }
#endif
    }

    //release 做的事情是遍历释放保存的自动释放对象，而 kill 做的事情是遍历对 AutoreleasePoolPage 执行 delete 操作。
    //从当前的 page 开始，一直根据 child 链向前走直到 child 为空，把经过的 page 全部执行 delete 操作（包括当前 page）。
    void kill() 
    {
        // Not recursive: we don't want to blow out the stack 
        // if a thread accumulates a stupendous amount of garbage
        AutoreleasePoolPage *page = this;
        // 从当前 page 开始一直沿着 child 链往前走，直到 AutoreleasePool 的双向链表的最后一个 page
        while (page->child) page = page->child;

        
        // 临时变量（死亡指针）
        AutoreleasePoolPage *deathptr;
        
        // 是 do while 循环，所以会至少进行一次 delete，
        // 即当前 page 也会被执行 delete（不同与上面的 release 操作，入参 stop 并不会执行 objc_release 操作）
        do {
            // 要执行 delete 的 page
            deathptr = page;
            // 记录前一个 page
            page = page->parent;
            // 如果当前 page 的 parent 存在的话，要把这个 parent 的 child 置为 nil
            if (page) {
                page->unprotect(); // 可读可写
                page->child = nil; // child 置为 nil
                page->protect(); // 可写
            }
            delete deathptr; // delete page
        } while (deathptr != this);
    }

    //Thread Local Stroge dealloc 的时候，要把自动释放池内的所有自动释放对象执行 release 操作，然后所有的 page 执行 kill。
    static void tls_dealloc(void *p) 
    {
        // 如果 p 是空占位池则 return
        if (p == (void*)EMPTY_POOL_PLACEHOLDER) {
            // No objects or pool pages to clean up here.
            // 这里没有 objects 或者 pages 需要清理
            return;
        }

        // reinstate TLS value while we work
        // 这里直接把 p 保存在 TLS 中作为 hotPage
        setHotPage((AutoreleasePoolPage *)p);

        if (AutoreleasePoolPage *page = coldPage()) {
            // 如果 coldPage 存在（双向链表中的第一个 page）
                    
            // 这个调用的函数链超级长，最终实现的是把自动释放池里的所有自动释放对象都执行
            // objc_release 然后所有的 page 执行 delete
            if (!page->empty()) objc_autoreleasePoolPop(page->begin());  // pop all of the pools
            if (slowpath(DebugMissingPools || DebugPoolAllocation)) {
                // pop() killed the pages already
            } else {
                // 从 page 开始一直沿着 child 向前把所有的 page 执行 delete
                // kill 只处理 page，不处理 autorelease 对象
                page->kill();  // free all of the pages
            }
        }
        
        // clear TLS value so TLS destruction doesn't loop
        // 清除 TLS 值，以便 TLS 销毁不会循环
        // 把 hotPage 置为 nil
        // static pthread_key_t const key = AUTORELEASE_POOL_KEY;
        // tls_set_direct(key, (void *)page);
        // 把 key 置为 nil
        setHotPage(nil);
    }

   // void *p 转为 AutoreleasePoolPage *，主要用于把指向 begin() 的指针转为 AutoreleasePoolPage *。
    static AutoreleasePoolPage *pageForPointer(const void *p) 
    {
        // 指针转为 unsigned long
        return pageForPointer((uintptr_t)p);
    }

    static AutoreleasePoolPage *pageForPointer(uintptr_t p) 
    {
        // result 临时变量
        AutoreleasePoolPage *result;
        
        // 首先 page 创建时 malloc_zone_memalign(malloc_default_zone(), SIZE, SIZE);
        // 是根据 SIZE 进行内存对齐的，所以 每个 page 的起始地址一定是 SIZE 的整数倍
        // p 对 1024 取模
        uintptr_t offset = p % SIZE;

        // 对 4096 取模，所以 offset 的值应该是在 0~4095 之间
        // sizeof(AutoreleasePoolPage) 的值应该和 sizeof(AutoreleasePoolPageData) 一样的，都是 56
        // 同时由于 p 入参进来至少是从 page 的 begin() 位置开始的，所以说至少从 page 的起始地址偏移 56 后开始的，
        // 所以这个 offset 的范围是 [56 4095] 区间内
        ASSERT(offset >= sizeof(AutoreleasePoolPage));

        // p 减掉 offset，p 倒退到 page 的起点位置
        result = (AutoreleasePoolPage *)(p - offset);
        
        // 验证 result 是否 magic.check() 和 thread == objc_thread_self()，两个必须满足的的条件
        result->fastcheck();

        return result;
    }


    //每个线程都有自己的存储空间。这里是根据 key 在当前线程的存储空间里面保存一个空池。
    static inline bool haveEmptyPoolPlaceholder()
    {
        // key 是一个静态局部变量
        // static pthread_key_t const key = AUTORELEASE_POOL_KEY;
        // # define AUTORELEASE_POOL_KEY ((tls_key_t)__PTK_FRAMEWORK_OBJC_KEY3)
        // # define EMPTY_POOL_PLACEHOLDER ((id*)1)
        
        // 在当前线程根据 key 找到一个空池
        id *tls = (id *)tls_get_direct(key);
        
        // 如果未找到则返回 false
        return (tls == EMPTY_POOL_PLACEHOLDER);
    }

    static inline id* setEmptyPoolPlaceholder()
    {
        // 当前线程没有存储 key 对应的内容，否则执行断言
         // 这里会覆盖原始值，所以必须保证 key 下面现在没有存储 page
        ASSERT(tls_get_direct(key) == nil);
        
        // 把 EMPTY_POOL_PLACEHOLDER 根据 key 保存在当前线程的存储空间内
        tls_set_direct(key, (void *)EMPTY_POOL_PLACEHOLDER);
        
        // 返回 EMPTY_POOL_PLACEHOLDER，（(id *)1）
        return EMPTY_POOL_PLACEHOLDER;
    }

    static inline AutoreleasePoolPage *hotPage() 
    {
        // 当前的 hotPage 是根据 key 保存在当前线程的存储空间内的
        AutoreleasePoolPage *result = (AutoreleasePoolPage *)
            tls_get_direct(key);
        // 如果等于 EMPTY_POOL_PLACEHOLDER 的话，返回 nil
        if ((id *)result == EMPTY_POOL_PLACEHOLDER) return nil;
        // result 执行 check 判断是否符合 AutoreleasePoolPage 的约束规则
        if (result) result->fastcheck();
        return result;
    }

    static inline void setHotPage(AutoreleasePoolPage *page) 
    {
        // page 入参检测，判断是否符合 AutoreleasePoolPage magic 的约束规则
        if (page) page->fastcheck();
        // 根据 key 把 page 保存在当前线程的存储空间内，作为 hotPage
        tls_set_direct(key, (void *)page);
    }

    //首先找到 hotPage 然后沿着它的 parent 走，直到最后 parent 为 nil，最后一个 AutoreleasePoolPage 就是 coldPage，返回它。这里看出来其实 coldPage 就是双向 page 链表的第一个 page。
    static inline AutoreleasePoolPage *coldPage() 
    {
        AutoreleasePoolPage *result = hotPage();
        if (result) {
            // 循环一直沿着 parent 指针找，直到第一个 AutoreleasePoolPage
            while (result->parent) {
                // 沿着 parent 更新 result
                result = result->parent;
                // 检测 result 符合 page 规则
                result->fastcheck();
            }
        }
        return result;
    }

    //把对象快速放进自动释放池。
    static inline id *autoreleaseFast(id obj)
    {
        AutoreleasePoolPage *page = hotPage();
        if (page && !page->full()) {
            // 如果 page 存在并且 page 未满，则直接调用 add 函数把 obj 添加到 page
            return page->add(obj);
        } else if (page) {
            // 如果 page 满了，则调用 autoreleaseFullPage 构建新 AutoreleasePoolPage，并把 obj 添加进去
            return autoreleaseFullPage(obj, page);
        } else {
            // 连 hotPage 都不存在，可能就一 EMPTY_POOL_PLACEHOLDER 在线程的存储空间内保存
            // 如果 page 不存在，即当前线程还不存在自动释放池，构建新 AutoreleasePoolPage，并把 obj 添加进去
            return autoreleaseNoPage(obj);
        }
    }

    static __attribute__((noinline))
    id *autoreleaseFullPage(id obj, AutoreleasePoolPage *page)
    {
        // The hot page is full. 
        // Step to the next non-full page, adding a new page if necessary.
        // Then add the object to that page.
        // 如果 hotpage 满了，转到下一个未满的 page，如果不存在的话添加一个新的 page。
        // 然后把 object 添加到新 page 里。
        
        // page 必须是 hotPage
        ASSERT(page == hotPage());
        // page 满了，或者自动释放池按顺序弹出时暂停，并允许堆调试器跟踪自动释放池
        ASSERT(page->full()  ||  DebugPoolAllocation);

        // do while 循环里面分为两种情况
        // 1. 沿着 child 往前走，如果能找到一个非满的 page，则可以把 obj 放进去
        // 2. 如果 child 不存在或者所有的 child 都满了，
        //    则构建一个新的 AutoreleasePoolPage 并拼接在 AutoreleasePool 的双向链表中，
        //    并把 obj 添加进新 page 里面
        do {
            if (page->child) page = page->child;
            else page = new AutoreleasePoolPage(page);
        } while (page->full());

        // 设置 page 为 hotPage
        setHotPage(page);
        // 把 obj 添加进 page 里面，返回值是 next 之前指向的位置 (objc_object **)
        return page->add(obj);
    }

    static __attribute__((noinline))
    id *autoreleaseNoPage(id obj)
    {
        // "No page" could mean no pool has been pushed
        // or an empty placeholder pool has been pushed and has no contents yet
        // "No page" 可能意味着没有构建任何池，或者只有一个 EMPTY_POOL_PLACEHOLDER 占位
            
        // hotPage 不存在，否则执行断言
        ASSERT(!hotPage());

        bool pushExtraBoundary = false;
        if (haveEmptyPoolPlaceholder()) {
            // 如果线程里面存储的是 EMPTY_POOL_PLACEHOLDER
            // We are pushing a second pool over the empty placeholder pool
            // or pushing the first object into the empty placeholder pool.
            // 我们正在将第二个池推入空的占位符池，或者将第一个对象推入空的占位符池。
            // Before doing that, push a pool boundary on behalf of the pool 
            // that is currently represented by the empty placeholder.
            // 在此之前，代表当前由空占位符表示的池来推动池边界
            pushExtraBoundary = true;
        }
        else if (obj != POOL_BOUNDARY  &&  DebugMissingPools) {
            
            // 警告在没有自动释放池的情况下进行 autorelease，
            // 这可能导致内存泄漏（可能是因为没有释放池，然后对象缺少一次 objc_release 执行，导致内存泄漏）
            // 如果 obj 不为 nil 并且 DebugMissingPools。
            
            
            // We are pushing an object with no pool in place, 
            // and no-pool debugging was requested by environment.
            
            // 我们正在没有自动释放池的情况下把一个对象往池里推，
            // 并且打开了 environment 的 no-pool debugging，此时会在控制台给一个提示信息。
            // 线程内连 EMPTY_POOL_PLACEHOLDER 都没有存储，并且如果 DebugMissingPools 打开了，则控制台输出如下信息
            _objc_inform("MISSING POOLS: (%p) Object %p of class %s "
                         "autoreleased with no pool in place - "
                         "just leaking - break on "
                         "objc_autoreleaseNoPool() to debug", 
                         objc_thread_self(), (void*)obj, object_getClassName(obj));
            // obj 不为 nil，并且线程内连 EMPTY_POOL_PLACEHOLDER 都没有存储
            // 执行 objc_autoreleaseNoPool，且它是个 hook 函数
            objc_autoreleaseNoPool(obj);
            return nil;
        }
        else if (obj == POOL_BOUNDARY  &&  !DebugPoolAllocation) {
            
            // 当自动释放池顺序弹出时暂停，并允许堆调试器跟踪自动释放池
            // 如果 obj 为空，并且没有打开 DebugPoolAllocation
            
            
            // We are pushing a pool with no pool in place,
            // and alloc-per-pool debugging was not requested.
            // 在没有池的情况下，我们设置一个空池占位，并且不要求为池分配空间和调试。（空池占位只是一个 ((id*)1)）
            // Install and return the empty pool placeholder.
            // 根据 key 在当前线程的存储空间内保存 EMPTY_POOL_PLACEHOLDER 占位
            return setEmptyPoolPlaceholder();
        }

        // We are pushing an object or a non-placeholder'd pool.
        // 构建非占位的池
        // Install the first page.
        // 构建自动释放池的第一个真正意义的 page
        AutoreleasePoolPage *page = new AutoreleasePoolPage(nil);
        setHotPage(page); // 设置为 hotPage
        
        // Push a boundary on behalf of the previously-placeholder'd pool.
        // 代表先前占位符的池推边界。
            
        // 如果之前有一个 EMPTY_POOL_PLACEHOLDER 在当前线程的存储空间里面占位的话
        if (pushExtraBoundary) {
            // 池边界前进一步
            // 可以理解为把 next 指针往前推进了一步，并在 next 之前的指向下放了一个 nil
            page->add(POOL_BOUNDARY);
        }
        
        // Push the requested object or pool.
        // 把 objc 放进自动释放池
        return page->add(obj);
    }


    static __attribute__((noinline))
    id *autoreleaseNewPage(id obj)
    {
        AutoreleasePoolPage *page = hotPage();
        // 如果 hotPage 存在则调用 autoreleaseFullPage 把 obj 放进 page 里面
        if (page) return autoreleaseFullPage(obj, page);
        // 如果 hotPage 不存在，则调用 autoreleaseNoPage 把 obj 放进自动释放池（进行新建 page）
        else return autoreleaseNoPage(obj);
    }

public:
    static inline id autorelease(id obj)
    {
        ASSERT(obj); // 如果对象不存在则执行断言
        ASSERT(!obj->isTaggedPointer()); // 如果对象是 Tagged Pointer 则执行断言
        id *dest __unused = autoreleaseFast(obj); // 调用 autoreleaseFast(obj) 函数，把 obj 快速放进自动释放池
        // 1. if (obj != POOL_BOUNDARY  &&  DebugMissingPools) 时 return nil
        // 2. if (obj == POOL_BOUNDARY  &&  !DebugPoolAllocation) 时 return EMPTY_POOL_PLACEHOLDER
        // 3. *dest == obj 正常添加
        ASSERT(!dest  ||  dest == EMPTY_POOL_PLACEHOLDER  ||  *dest == obj);
        return obj;
    }


    //如果自动释放池不存在，构建一个新的 page。push 函数的作用可以理解为，调用 AutoreleasePoolPage::push 在当前线程的存储空间保存一个 EMPTY_POOL_PLACEHOLDER。
    static inline void *push() 
    {
        id *dest;
        if (slowpath(DebugPoolAllocation)) {
            // Each autorelease pool starts on a new pool page.
            // 每个自动释放池从一个新的 page 开始
            // 调用 autoreleaseNewPage
            dest = autoreleaseNewPage(POOL_BOUNDARY);
        } else {
            // 构建一个占位池
            dest = autoreleaseFast(POOL_BOUNDARY);
        }
        ASSERT(dest == EMPTY_POOL_PLACEHOLDER || *dest == POOL_BOUNDARY);
        return dest;
    }

    __attribute__((noinline, cold))
    static void badPop(void *token)
    {
        // Error. For bincompat purposes this is not 
        // fatal in executables built with old SDKs.
        // 出于 bin 的兼容目的，不能在旧 SDKs 上构建和执行，否则 _objc_fatal。
        if (DebugPoolAllocation || sdkIsAtLeast(10_12, 10_0, 10_0, 3_0, 2_0)) {
            // OBJC_DEBUG_POOL_ALLOCATION or new SDK. Bad pop is fatal.
            // OBJC_DEBUG_POOL_ALLOCATION or new SDK. Bad pop is fatal.
            // 无效或者过早释放的自动释放池。
            _objc_fatal
                ("Invalid or prematurely-freed autorelease pool %p.", token);
        }

        // Old SDK. Bad pop is warned once.
        // 如果是 旧 SDKs，发生一次警告。
        // 静态局部变量，保证下面的 if 只能进入一次
        static bool complained = false;
        if (!complained) {
            complained = true;
            _objc_inform_now_and_on_crash
                ("Invalid or prematurely-freed autorelease pool %p. "
                 "Set a breakpoint on objc_autoreleasePoolInvalid to debug. "
                 "Proceeding anyway because the app is old "
                 "(SDK version " SDK_FORMAT "). Memory errors are likely.",
                     token, FORMAT_SDK(sdkVersion()));
        }
        // 执行最开始的 hook
        objc_autoreleasePoolInvalid(token);
    }

    // 这里有一个模版参数 (bool 类型的 allowDebug)，
    // 直接传值，有点类似 sotreWeak 里的新值和旧值的模版参数
    // 这个 void *token 的参数在函数实现里面没有用到....
    template<bool allowDebug>
    static void
    popPage(void *token, AutoreleasePoolPage *page, id *stop)
    {
        // OPTION( PrintPoolHiwat, OBJC_PRINT_POOL_HIGHWATER,
        // "log high-water marks for autorelease pools")
        // 打印自动释放池的 high-water 标记
        // 如果允许 debug 并且打开了 OBJC_PRINT_POOL_HIGHWATER，则打印自动释放池的 hiwat（high-water “最高水位”）

        if (allowDebug && PrintPoolHiwat) printHiwat();

        // 把 stop 后面添加进自动释放池的对象全部执行一次 objc_release 操作
        page->releaseUntil(stop);

        // memory: delete empty children
        // 删除空的 page
        // OPTION( DebugPoolAllocation, OBJC_DEBUG_POOL_ALLOCATION,
        // "halt when autorelease pools are popped out of order,
        // and allow heap debuggers to track autorelease pools")
        // 当自动释放池弹出顺序时停止，并允许堆调试器跟踪自动释放池

        if (allowDebug && DebugPoolAllocation  &&  page->empty()) {
            // 如果允许 Debug 且开启了 DebugPoolAllocation 并且 page 是空的
            // special case: delete everything during page-per-pool debugging
            // 特殊情况：删除每个 page 调试期间的所有内容
            
            AutoreleasePoolPage *parent = page->parent;
            // 把 page 以及 page 之后增加的 page 都执行 delete
            page->kill();
            setHotPage(parent); // 把 page 的 parent 设置为 hotPage
        } else if (allowDebug && DebugMissingPools  &&  page->empty()  &&  !page->parent) {
            //OPTION( DebugMissingPools, OBJC_DEBUG_MISSING_POOLS,
            // "warn about autorelease with no pool in place, which may be a leak")
            // 警告自动释放没有池占位， 这可能是一个泄漏

            // special case: delete everything for pop(top) when debugging missing autorelease pools
            // 在调试缺少自动释放池时，删除 pop（顶部）的所有内容
            // 把 page 以及 page 之后增加的 page 都执行 delete
            page->kill();
            setHotPage(nil); // 设置 hotPage 为 nil
        } else if (page->child) {
            // 如果 page 的 child 存在
                    
            // hysteresis: keep one empty child if page is more than half full
            // 如果 page 存储的自动释放对象超过了一半，则保留一个 empty child

            if (page->lessThanHalfFull()) {
                // 如果 page 内部保存的自动释放对象的数量少于一半
                // 把 page 以及 page 之后增加的 page 都执行 delete
                page->child->kill();
            }
            else if (page->child->child) {
                // 如果 page 的 child 的 child 存在
                // 则把 page->child->child 以及它之后增加的 page 全部执行 delete
                page->child->child->kill();
            }
        }
    }

    // __attribute__((cold)) 表示函数不经常调用
    __attribute__((noinline, cold))
    static void
    popPageDebug(void *token, AutoreleasePoolPage *page, id *stop)
    { // 模版参数 allowDebug 传递的是 true
        popPage<true>(token, page, stop);
    }

    static inline void
    pop(void *token)
    {
        AutoreleasePoolPage *page;
        id *stop;
        if (token == (void*)EMPTY_POOL_PLACEHOLDER) {
            // Popping the top-level placeholder pool.
            // 弹出顶级 EMPTY_POOL_PLACEHOLDER 占位符池
                    
            // 取出 hotPage
            page = hotPage();
            if (!page) {
                // 如果 hotPage 不存在，则表示目前就一 EMPTY_POOL_PLACEHOLDER，说明池还没有使用过
                // Pool was never used. Clear the placeholder.
                // Pool 从未使用过。清除占位符。
                return setHotPage(nil);
            }
            // Pool was used. Pop its contents normally.
            // Pool 是使用过了。正常弹出其内容。
            // Pool pages remain allocated for re-use as usual.
            // Pool pages 保持分配以照常使用.
            
            // 第一个 page
            page = coldPage();
            // 把第一个 page 的 begin 赋值给 token
            token = page->begin();
        } else {
            // token 转为 page
            page = pageForPointer(token);
        }

        stop = (id *)token;
        if (*stop != POOL_BOUNDARY) {
            if (stop == page->begin()  &&  !page->parent) {
                // Start of coldest page may correctly not be POOL_BOUNDARY:
                // 1. top-level pool is popped, leaving the cold page in place
                // 2. an object is autoreleased with no pool
            } else {
                // Error. For bincompat purposes this is not 
                // fatal in executables built with old SDKs.
                return badPop(token);
            }
        }
        // allowDebug 为 true
        if (slowpath(PrintPoolHiwat || DebugPoolAllocation || DebugMissingPools)) {
            return popPageDebug(token, page, stop);
        }
        // 释放对象删除 page
        return popPage<false>(token, page, stop);
    }

    static void init()
    {
        // key tls_dealloc 释放对象删除 page
        int r __unused = pthread_key_init_np(AutoreleasePoolPage::key, 
                                             AutoreleasePoolPage::tls_dealloc);
        ASSERT(r == 0);
    }

    //打印当前 page 里面的 autorelease 对象。
    __attribute__((noinline, cold))
    void print()
    {
        // 打印 hotPage 和 coldPage
        _objc_inform("[%p]  ................  PAGE %s %s %s", this, 
                     full() ? "(full)" : "", 
                     this == hotPage() ? "(hot)" : "", 
                     this == coldPage() ? "(cold)" : "");
        // 打印当前池里的 autorelease 对象
        check(false);
        for (id *p = begin(); p < next; p++) {
            if (*p == POOL_BOUNDARY) {
                _objc_inform("[%p]  ################  POOL %p", p, p);
            } else {
                _objc_inform("[%p]  %#16lx  %s", 
                             p, (unsigned long)*p, object_getClassName(*p));
            }
        }
    }

   ///打印自动释放池里面的所有 autorelease 对象。
    __attribute__((noinline, cold))
    static void printAll()  // 这是一个静态非内联并较少被调用的函数
    {
        _objc_inform("##############");
        // 打印自动释放池所处的线程
        _objc_inform("AUTORELEASE POOLS for thread %p", objc_thread_self());

        // 统计自动释放池里面的所有对象
        AutoreleasePoolPage *page;
        // coldePage 是第一个 page
        // 沿着 child 指针一直向前，遍历所有的 page
        ptrdiff_t objects = 0;
        for (page = coldPage(); page; page = page->child) {
            // 这里是把每个 page 里的 autorelease 对象的数量全部加起来
            objects += page->next - page->begin();
        }
        // 打印自动释放池里面等待 objc_release 的所有 autorelease 对象的数量
        _objc_inform("%llu releases pending.", (unsigned long long)objects);

        if (haveEmptyPoolPlaceholder()) {
            // 如果目前只是空占位池的话，打印空池
            _objc_inform("[%p]  ................  PAGE (placeholder)", 
                         EMPTY_POOL_PLACEHOLDER);
            _objc_inform("[%p]  ################  POOL (placeholder)", 
                         EMPTY_POOL_PLACEHOLDER);
        }
        else {
            // 循环打印每个 page 里面的 autorelease 对象
            for (page = coldPage(); page; page = page->child) {
                page->print();
            }
        }
        // 打印分割线
        _objc_inform("##############");
    }

    __attribute__((noinline, cold))
    static void printHiwat()
    {
        // Check and propagate high water mark
        // Ignore high water marks under 256 to suppress noise.
        // 检查并传播 high water 忽略 256 以下的 high water 以抑制噪音。
        AutoreleasePoolPage *p = hotPage();
        // COUNT 固定情况下是 4096 / 8 = 512
       // p->depth 是 hotPage 的深度，第一个 page 的 depth 是 0，
       // 然后每次增加一个 page 该 page 的 depth 加 1
       // p->next - p->begin() 是该 page 内存储的 autorelease 对象的个数
       // 那么 mark 大概就是从第一个 page 到 hotpage 的 page
       // 的数量乘以 512 然后加上 hotPage 里面保存的 autorelease 对象的数量
        
        uint32_t mark = p->depth*COUNT + (uint32_t)(p->next - p->begin());
        // 如果 mark 大于 p->hiwat 并且 mark 大于 256
        if (mark > p->hiwat  &&  mark > 256) {
            // 沿着 parent 链遍历每个 page，把每个 page 的 hiwat 置为 mark
            for( ; p; p = p->parent) {
                p->unprotect(); // 可读可写
                p->hiwat = mark; // 修改 hiwat 为 mark
                p->protect(); // 只可读
            }

            _objc_inform("POOL HIGHWATER: new high water mark of %u "
                         "pending releases for thread %p:",
                         mark, objc_thread_self());

            // int backtrace(void**,int) __OSX_AVAILABLE_STARTING(__MAC_10_5, __IPHONE_2_0);
            // 函数原型
            // #include <execinfo.h>
            // int backtrace(void **buffer, int size);
            // 该函数获取当前线程的调用堆栈，获取的信息将会被存放在 buffer 中，
            // 它是一个指针数组，参数 size 用来指定 buffer
            // 中可以保存多少个 void * 元素。函数的返回值是实际返回的 void * 元素个数。
            // buffer 中的 void * 元素实际是从堆栈中获取的返回地址。

            void *stack[128];
            int count = backtrace(stack, sizeof(stack)/sizeof(stack[0]));
            
            // char** backtrace_symbols(void* const*,int) __OSX_AVAILABLE_STARTING(__MAC_10_5, __IPHONE_2_0);
            // 函数原型
            // char **backtrace_symbols(void *const *buffer, int size);
            // 该函数将 backtrace 函数获取的信息转化为一个字符串数组，
            // 参数 buffer 是 backtrace 获取的堆栈指针，
            // size 是 backtrace 返回值。
            // 函数返回值是一个指向字符串数组的指针，它包含 char* 元素个数为 size。
            // 每个字符串包含了一个相对于 buffer 中对应元素的可打印信息，
            // 包括函数名、函数偏移地址和实际返回地址。
            // backtrace_symbols 生成的字符串占用的内存是 malloc 出来的，
            // 但是是一次性 malloc 出来的，释放是只需要一次性释放返回的二级指针即可
            char **sym = backtrace_symbols(stack, count);
            for (int i = 0; i < count; i++) {
                _objc_inform("POOL HIGHWATER:     %s", sym[i]);
            }
            free(sym);
        }
    }

#undef POOL_BOUNDARY
};

/***********************************************************************
* Slow paths for inline control
**********************************************************************/

#if SUPPORT_NONPOINTER_ISA

NEVER_INLINE id 
objc_object::rootRetain_overflow(bool tryRetain)
{
    return rootRetain(tryRetain, true);
}


NEVER_INLINE uintptr_t
objc_object::rootRelease_underflow(bool performDealloc)
{
    return rootRelease(performDealloc, true);
}


// Slow path of clearDeallocating() 
// for objects with nonpointer isa
// that were ever weakly referenced 
// or whose retain count ever overflowed to the side table.
NEVER_INLINE void
objc_object::clearDeallocating_slow()
{
    ASSERT(isa.nonpointer  &&  (isa.weakly_referenced || isa.has_sidetable_rc));

    //获取引用计数表
    SideTable& table = SideTables()[this];
    table.lock();
    if (isa.weakly_referenced) {
        //擦除弱引用
        weak_clear_no_lock(&table.weak_table, (id)this);
    }
    if (isa.has_sidetable_rc) {
        //擦除引用计数
        table.refcnts.erase(this);
    }
    table.unlock();
}

#endif

__attribute__((noinline,used))
id 
objc_object::rootAutorelease2()
{
    ASSERT(!isTaggedPointer());
    return AutoreleasePoolPage::autorelease((id)this);
}


BREAKPOINT_FUNCTION(
    void objc_overrelease_during_dealloc_error(void)
);


NEVER_INLINE uintptr_t
objc_object::overrelease_error()
{
    _objc_inform_now_and_on_crash("%s object %p overreleased while already deallocating; break on objc_overrelease_during_dealloc_error to debug", object_getClassName((id)this), this);
    objc_overrelease_during_dealloc_error();
    return 0; // allow rootRelease() to tail-call this
}


/***********************************************************************
* Retain count operations for side table.
**********************************************************************/


#if DEBUG
// Used to assert that an object is not present in the side table.
bool
objc_object::sidetable_present()
{
    bool result = false;
    SideTable& table = SideTables()[this];

    table.lock();

    RefcountMap::iterator it = table.refcnts.find(this);
    if (it != table.refcnts.end()) result = true;

    if (weak_is_registered_no_lock(&table.weak_table, (id)this)) result = true;

    table.unlock();

    return result;
}
#endif

#if SUPPORT_NONPOINTER_ISA

void 
objc_object::sidetable_lock()
{
    SideTable& table = SideTables()[this];
    table.lock();
}

void 
objc_object::sidetable_unlock()
{
    SideTable& table = SideTables()[this];
    table.unlock();
}


// Move the entire retain count to the side table, 
// as well as isDeallocating and weaklyReferenced.
void 
objc_object::sidetable_moveExtraRC_nolock(size_t extra_rc, 
                                          bool isDeallocating, 
                                          bool weaklyReferenced)
{
    ASSERT(!isa.nonpointer);        // should already be changed to raw pointer
    SideTable& table = SideTables()[this];

    size_t& refcntStorage = table.refcnts[this];
    size_t oldRefcnt = refcntStorage;
    // not deallocating - that was in the isa
    ASSERT((oldRefcnt & SIDE_TABLE_DEALLOCATING) == 0);  
    ASSERT((oldRefcnt & SIDE_TABLE_WEAKLY_REFERENCED) == 0);  

    uintptr_t carry;
    size_t refcnt = addc(oldRefcnt, extra_rc << SIDE_TABLE_RC_SHIFT, 0, &carry);
    if (carry) refcnt = SIDE_TABLE_RC_PINNED;
    if (isDeallocating) refcnt |= SIDE_TABLE_DEALLOCATING;
    if (weaklyReferenced) refcnt |= SIDE_TABLE_WEAKLY_REFERENCED;

    refcntStorage = refcnt;
}


// Move some retain counts to the side table from the isa field.
// Returns true if the object is now pinned.
bool 
objc_object::sidetable_addExtraRC_nolock(size_t delta_rc)
{
    ASSERT(isa.nonpointer);
    SideTable& table = SideTables()[this];

    size_t& refcntStorage = table.refcnts[this];
    size_t oldRefcnt = refcntStorage;
    // isa-side bits should not be set here
    ASSERT((oldRefcnt & SIDE_TABLE_DEALLOCATING) == 0);
    ASSERT((oldRefcnt & SIDE_TABLE_WEAKLY_REFERENCED) == 0);

    if (oldRefcnt & SIDE_TABLE_RC_PINNED) return true;

    uintptr_t carry;
    size_t newRefcnt = 
        addc(oldRefcnt, delta_rc << SIDE_TABLE_RC_SHIFT, 0, &carry);
    if (carry) {
        refcntStorage =
            SIDE_TABLE_RC_PINNED | (oldRefcnt & SIDE_TABLE_FLAG_MASK);
        return true;
    }
    else {
        refcntStorage = newRefcnt;
        return false;
    }
}


// Move some retain counts from the side table to the isa field.
// Returns the actual count subtracted, which may be less than the request.
size_t 
objc_object::sidetable_subExtraRC_nolock(size_t delta_rc)
{
    ASSERT(isa.nonpointer);
    SideTable& table = SideTables()[this];

    RefcountMap::iterator it = table.refcnts.find(this);
    if (it == table.refcnts.end()  ||  it->second == 0) {
        // Side table retain count is zero. Can't borrow.
        return 0;
    }
    size_t oldRefcnt = it->second;

    // isa-side bits should not be set here
    ASSERT((oldRefcnt & SIDE_TABLE_DEALLOCATING) == 0);
    ASSERT((oldRefcnt & SIDE_TABLE_WEAKLY_REFERENCED) == 0);

    size_t newRefcnt = oldRefcnt - (delta_rc << SIDE_TABLE_RC_SHIFT);
    ASSERT(oldRefcnt > newRefcnt);  // shouldn't underflow
    it->second = newRefcnt;
    return delta_rc;
}


size_t 
objc_object::sidetable_getExtraRC_nolock()
{
    ASSERT(isa.nonpointer);
    SideTable& table = SideTables()[this];
    RefcountMap::iterator it = table.refcnts.find(this);
    if (it == table.refcnts.end()) return 0;
    else return it->second >> SIDE_TABLE_RC_SHIFT;
}


// SUPPORT_NONPOINTER_ISA
#endif


id
objc_object::sidetable_retain()
{
#if SUPPORT_NONPOINTER_ISA
    ASSERT(!isa.nonpointer);
#endif
    SideTable& table = SideTables()[this];
    
    table.lock();
    size_t& refcntStorage = table.refcnts[this];
    if (! (refcntStorage & SIDE_TABLE_RC_PINNED)) {
        refcntStorage += SIDE_TABLE_RC_ONE;
    }
    table.unlock();

    return (id)this;
}


bool
objc_object::sidetable_tryRetain()
{
#if SUPPORT_NONPOINTER_ISA
    ASSERT(!isa.nonpointer);
#endif
    SideTable& table = SideTables()[this];

    // NO SPINLOCK HERE
    // _objc_rootTryRetain() is called exclusively by _objc_loadWeak(), 
    // which already acquired the lock on our behalf.

    // fixme can't do this efficiently with os_lock_handoff_s
    // if (table.slock == 0) {
    //     _objc_fatal("Do not call -_tryRetain.");
    // }

    bool result = true;
    auto it = table.refcnts.try_emplace(this, SIDE_TABLE_RC_ONE);
    auto &refcnt = it.first->second;
    if (it.second) {
        // there was no entry
    } else if (refcnt & SIDE_TABLE_DEALLOCATING) {
        result = false;
    } else if (! (refcnt & SIDE_TABLE_RC_PINNED)) {
        refcnt += SIDE_TABLE_RC_ONE;
    }
    
    return result;
}

#pragma mark - retainCount
uintptr_t
objc_object::sidetable_retainCount()
{
    //根据对象地址获取SideTable
    SideTable& table = SideTables()[this];

    //初始值为1
    size_t refcnt_result = 1;
    
    table.lock();
    //哈希查找-获取引用计数
    RefcountMap::iterator it = table.refcnts.find(this);
    if (it != table.refcnts.end()) {
        // this is valid for SIDE_TABLE_RC_PINNED too
        // 1 + 左移两位
        refcnt_result += it->second >> SIDE_TABLE_RC_SHIFT;
    }
    table.unlock();
    return refcnt_result;
}


bool 
objc_object::sidetable_isDeallocating()
{
    SideTable& table = SideTables()[this];

    // NO SPINLOCK HERE
    // _objc_rootIsDeallocating() is called exclusively by _objc_storeWeak(), 
    // which already acquired the lock on our behalf.


    // fixme can't do this efficiently with os_lock_handoff_s
    // if (table.slock == 0) {
    //     _objc_fatal("Do not call -_isDeallocating.");
    // }

    RefcountMap::iterator it = table.refcnts.find(this);
    return (it != table.refcnts.end()) && (it->second & SIDE_TABLE_DEALLOCATING);
}


bool 
objc_object::sidetable_isWeaklyReferenced()
{
    bool result = false;

    SideTable& table = SideTables()[this];
    table.lock();

    RefcountMap::iterator it = table.refcnts.find(this);
    if (it != table.refcnts.end()) {
        result = it->second & SIDE_TABLE_WEAKLY_REFERENCED;
    }

    table.unlock();

    return result;
}


void 
objc_object::sidetable_setWeaklyReferenced_nolock()
{
#if SUPPORT_NONPOINTER_ISA
    ASSERT(!isa.nonpointer);
#endif

    SideTable& table = SideTables()[this];

    table.refcnts[this] |= SIDE_TABLE_WEAKLY_REFERENCED;
}


// rdar://20206767
// return uintptr_t instead of bool so that the various raw-isa 
// -release paths all return zero in eax
uintptr_t
objc_object::sidetable_release(bool performDealloc)
{
#if SUPPORT_NONPOINTER_ISA
    ASSERT(!isa.nonpointer);
#endif
    SideTable& table = SideTables()[this];

    bool do_dealloc = false;

    table.lock();
    auto it = table.refcnts.try_emplace(this, SIDE_TABLE_DEALLOCATING);
    auto &refcnt = it.first->second;
    if (it.second) {
        do_dealloc = true;
    } else if (refcnt < SIDE_TABLE_DEALLOCATING) {
        // SIDE_TABLE_WEAKLY_REFERENCED may be set. Don't change it.
        do_dealloc = true;
        refcnt |= SIDE_TABLE_DEALLOCATING;
    } else if (! (refcnt & SIDE_TABLE_RC_PINNED)) {
        refcnt -= SIDE_TABLE_RC_ONE;
    }
    table.unlock();
    if (do_dealloc  &&  performDealloc) {
        ((void(*)(objc_object *, SEL))objc_msgSend)(this, @selector(dealloc));
    }
    return do_dealloc;
}


void 
objc_object::sidetable_clearDeallocating()
{
    SideTable& table = SideTables()[this];

    // clear any weak table items
    // clear extra retain count and deallocating bit
    // (fixme warn or abort if extra retain count == 0 ?)
    table.lock();
    RefcountMap::iterator it = table.refcnts.find(this);
    if (it != table.refcnts.end()) {
        if (it->second & SIDE_TABLE_WEAKLY_REFERENCED) {
            weak_clear_no_lock(&table.weak_table, (id)this);
        }
        table.refcnts.erase(it);
    }
    table.unlock();
}


/***********************************************************************
* Optimized retain/release/autorelease entrypoints
**********************************************************************/


#if __OBJC2__

#pragma mark - 引用计数+1 retain
__attribute__((aligned(16), flatten, noinline))
id 
objc_retain(id obj)
{
    if (!obj) return obj;
    if (obj->isTaggedPointer()) return obj;
    return obj->retain();
}


__attribute__((aligned(16), flatten, noinline))
void 
objc_release(id obj)
{
    if (!obj) return;
    if (obj->isTaggedPointer()) return;
    return obj->release();
}


__attribute__((aligned(16), flatten, noinline))
id
objc_autorelease(id obj)
{
    if (!obj) return obj;
    if (obj->isTaggedPointer()) return obj;
    return obj->autorelease();
}


// OBJC2
#else
// not OBJC2


id objc_retain(id obj) { return [obj retain]; }
void objc_release(id obj) { [obj release]; }
id objc_autorelease(id obj) { return [obj autorelease]; }


#endif


/***********************************************************************
* Basic operations for root class implementations a.k.a. _objc_root*()
**********************************************************************/

bool
_objc_rootTryRetain(id obj) 
{
    ASSERT(obj);

    return obj->rootTryRetain();
}

bool
_objc_rootIsDeallocating(id obj) 
{
    ASSERT(obj);

    return obj->rootIsDeallocating();
}


void 
objc_clear_deallocating(id obj) 
{
    ASSERT(obj);

    if (obj->isTaggedPointer()) return;
    obj->clearDeallocating();
}


bool
_objc_rootReleaseWasZero(id obj)
{
    ASSERT(obj);

    return obj->rootReleaseShouldDealloc();
}


NEVER_INLINE id
_objc_rootAutorelease(id obj)
{
    ASSERT(obj);
    return obj->rootAutorelease();
}

uintptr_t
_objc_rootRetainCount(id obj)
{
    ASSERT(obj);

    return obj->rootRetainCount();
}


NEVER_INLINE id
_objc_rootRetain(id obj)
{
    ASSERT(obj);

    return obj->rootRetain();
}

NEVER_INLINE void
_objc_rootRelease(id obj)
{
    ASSERT(obj);

    obj->rootRelease();
}


// Call [cls alloc] or [cls allocWithZone:nil], with appropriate 
// shortcutting optimizations.
static ALWAYS_INLINE id
callAlloc(Class cls, bool checkNil, bool allocWithZone=false)
{
#if __OBJC2__
    if (slowpath(checkNil && !cls)) return nil;
    if (fastpath(!cls->ISA()->hasCustomAWZ())) {
        //大多数不会重写allocwithzone方法
        return _objc_rootAllocWithZone(cls, nil);
    }
#endif

    // No shortcuts available.
    if (allocWithZone) {
        return ((id(*)(id, SEL, struct _NSZone *))objc_msgSend)(cls, @selector(allocWithZone:), nil);
    }
    return ((id(*)(id, SEL))objc_msgSend)(cls, @selector(alloc));
}


// Base class implementation of +alloc. cls is not nil.
// Calls [cls allocWithZone:nil].
id
_objc_rootAlloc(Class cls)
{
    return callAlloc(cls, false/*checkNil*/, true/*allocWithZone*/);
}

// Calls [cls alloc].
id
objc_alloc(Class cls)
{
    return callAlloc(cls, true/*checkNil*/, false/*allocWithZone*/);
}

// Calls [cls allocWithZone:nil].
id 
objc_allocWithZone(Class cls)
{
    return callAlloc(cls, true/*checkNil*/, true/*allocWithZone*/);
}

// Calls [[cls alloc] init].
id
objc_alloc_init(Class cls)
{
    return [callAlloc(cls, true/*checkNil*/, false/*allocWithZone*/) init];
}

// Calls [cls new]
id
objc_opt_new(Class cls)
{
#if __OBJC2__
    if (fastpath(cls && !cls->ISA()->hasCustomCore())) {
        return [callAlloc(cls, false/*checkNil*/, true/*allocWithZone*/) init];
    }
#endif
    return ((id(*)(id, SEL))objc_msgSend)(cls, @selector(new));
}

// Calls [obj self]
id
objc_opt_self(id obj)
{
#if __OBJC2__
    if (fastpath(!obj || obj->isTaggedPointer() || !obj->ISA()->hasCustomCore())) {
        return obj;
    }
#endif
    return ((id(*)(id, SEL))objc_msgSend)(obj, @selector(self));
}

// Calls [obj class]
Class
objc_opt_class(id obj)
{
#if __OBJC2__
    if (slowpath(!obj)) return nil;
    Class cls = obj->getIsa();
    if (fastpath(!cls->hasCustomCore())) {
        return cls->isMetaClass() ? obj : cls;
    }
#endif
    return ((Class(*)(id, SEL))objc_msgSend)(obj, @selector(class));
}

// Calls [obj isKindOfClass]
BOOL
objc_opt_isKindOfClass(id obj, Class otherClass)
{
#if __OBJC2__
    if (slowpath(!obj)) return NO;
    Class cls = obj->getIsa();
    if (fastpath(!cls->hasCustomCore())) {
        for (Class tcls = cls; tcls; tcls = tcls->superclass) {
            if (tcls == otherClass) return YES;
        }
        return NO;
    }
#endif
    return ((BOOL(*)(id, SEL, Class))objc_msgSend)(obj, @selector(isKindOfClass:), otherClass);
}

// Calls [obj respondsToSelector]
BOOL
objc_opt_respondsToSelector(id obj, SEL sel)
{
#if __OBJC2__
    if (slowpath(!obj)) return NO;
    Class cls = obj->getIsa();
    if (fastpath(!cls->hasCustomCore())) {
        return class_respondsToSelector_inst(obj, sel, cls);
    }
#endif
    return ((BOOL(*)(id, SEL, SEL))objc_msgSend)(obj, @selector(respondsToSelector:), sel);
}

//销毁对象
#pragma mark - 销毁对象
void
_objc_rootDealloc(id obj)
{
    ASSERT(obj);

    obj->rootDealloc();
}

void
_objc_rootFinalize(id obj __unused)
{
    ASSERT(obj);
    _objc_fatal("_objc_rootFinalize called with garbage collection off");
}


id
_objc_rootInit(id obj)
{
    // In practice, it will be hard to rely on this function.
    // Many classes do not properly chain -init calls.
    return obj;
}


malloc_zone_t *
_objc_rootZone(id obj)
{
    (void)obj;
#if __OBJC2__
    // allocWithZone under __OBJC2__ ignores the zone parameter
    return malloc_default_zone();
#else
    malloc_zone_t *rval = malloc_zone_from_ptr(obj);
    return rval ? rval : malloc_default_zone();
#endif
}

uintptr_t
_objc_rootHash(id obj)
{
    return (uintptr_t)obj;
}

#pragma mark - objc_autoreleasePoolPush
void *
objc_autoreleasePoolPush(void)
{
    return AutoreleasePoolPage::push();
}

NEVER_INLINE
void
objc_autoreleasePoolPop(void *ctxt)
{
    AutoreleasePoolPage::pop(ctxt);
}


void *
_objc_autoreleasePoolPush(void)
{
    return objc_autoreleasePoolPush();
}

void
_objc_autoreleasePoolPop(void *ctxt)
{
    objc_autoreleasePoolPop(ctxt);
}

void 
_objc_autoreleasePoolPrint(void)
{
    AutoreleasePoolPage::printAll();
}


// Same as objc_release but suitable for tail-calling 
// if you need the value back and don't want to push a frame before this point.
__attribute__((noinline))
static id 
objc_releaseAndReturn(id obj)
{
    objc_release(obj);
    return obj;
}

// Same as objc_retainAutorelease but suitable for tail-calling 
// if you don't want to push a frame before this point.
__attribute__((noinline))
static id 
objc_retainAutoreleaseAndReturn(id obj)
{
    return objc_retainAutorelease(obj);
}


// Prepare a value at +1 for return through a +0 autoreleasing convention.
id 
objc_autoreleaseReturnValue(id obj)
{
    if (prepareOptimizedReturn(ReturnAtPlus1)) return obj;

    return objc_autorelease(obj);
}

// Prepare a value at +0 for return through a +0 autoreleasing convention.
id 
objc_retainAutoreleaseReturnValue(id obj)
{
    if (prepareOptimizedReturn(ReturnAtPlus0)) return obj;

    // not objc_autoreleaseReturnValue(objc_retain(obj)) 
    // because we don't need another optimization attempt
    return objc_retainAutoreleaseAndReturn(obj);
}

// Accept a value returned through a +0 autoreleasing convention for use at +1.
id
objc_retainAutoreleasedReturnValue(id obj)
{
    if (acceptOptimizedReturn() == ReturnAtPlus1) return obj;

    return objc_retain(obj);
}

// Accept a value returned through a +0 autoreleasing convention for use at +0.
id
objc_unsafeClaimAutoreleasedReturnValue(id obj)
{
    if (acceptOptimizedReturn() == ReturnAtPlus0) return obj;

    return objc_releaseAndReturn(obj);
}

id
objc_retainAutorelease(id obj)
{
    return objc_autorelease(objc_retain(obj));
}

void
_objc_deallocOnMainThreadHelper(void *context)
{
    id obj = (id)context;
    [obj dealloc];
}

// convert objc_objectptr_t to id, callee must take ownership.
id objc_retainedObject(objc_objectptr_t pointer) { return (id)pointer; }

// convert objc_objectptr_t to id, without ownership transfer.
id objc_unretainedObject(objc_objectptr_t pointer) { return (id)pointer; }

// convert id to objc_objectptr_t, no ownership transfer.
objc_objectptr_t objc_unretainedPointer(id object) { return object; }


void arr_init(void) 
{
    AutoreleasePoolPage::init();
    SideTablesMap.init();
    _objc_associations_init();
}


#if SUPPORT_TAGGED_POINTERS

// Placeholder for old debuggers. When they inspect an 
// extended tagged pointer object they will see this isa.

@interface __NSUnrecognizedTaggedPointer : NSObject
@end

__attribute__((objc_nonlazy_class))
@implementation __NSUnrecognizedTaggedPointer
-(id) retain { return self; }
-(oneway void) release { }
-(id) autorelease { return self; }
@end

#endif

__attribute__((objc_nonlazy_class))
@implementation NSObject

+ (void)initialize {
}

+ (id)self {
    return (id)self;
}

- (id)self {
    return self;
}

+ (Class)class {
    return self;
}

- (Class)class {
    return object_getClass(self);
}

+ (Class)superclass {
    return self->superclass;
}

- (Class)superclass {
    return [self class]->superclass;
}

+ (BOOL)isMemberOfClass:(Class)cls {
    return self->ISA() == cls;
}

- (BOOL)isMemberOfClass:(Class)cls {
    return [self class] == cls;
}

+ (BOOL)isKindOfClass:(Class)cls {
    for (Class tcls = self->ISA(); tcls; tcls = tcls->superclass) {
        if (tcls == cls) return YES;
    }
    return NO;
}

- (BOOL)isKindOfClass:(Class)cls {
    for (Class tcls = [self class]; tcls; tcls = tcls->superclass) {
        if (tcls == cls) return YES;
    }
    return NO;
}

+ (BOOL)isSubclassOfClass:(Class)cls {
    for (Class tcls = self; tcls; tcls = tcls->superclass) {
        if (tcls == cls) return YES;
    }
    return NO;
}

+ (BOOL)isAncestorOfObject:(NSObject *)obj {
    for (Class tcls = [obj class]; tcls; tcls = tcls->superclass) {
        if (tcls == self) return YES;
    }
    return NO;
}

+ (BOOL)instancesRespondToSelector:(SEL)sel {
    return class_respondsToSelector_inst(nil, sel, self);
}

+ (BOOL)respondsToSelector:(SEL)sel {
    return class_respondsToSelector_inst(self, sel, self->ISA());
}

- (BOOL)respondsToSelector:(SEL)sel {
    return class_respondsToSelector_inst(self, sel, [self class]);
}

+ (BOOL)conformsToProtocol:(Protocol *)protocol {
    if (!protocol) return NO;
    for (Class tcls = self; tcls; tcls = tcls->superclass) {
        if (class_conformsToProtocol(tcls, protocol)) return YES;
    }
    return NO;
}

- (BOOL)conformsToProtocol:(Protocol *)protocol {
    if (!protocol) return NO;
    for (Class tcls = [self class]; tcls; tcls = tcls->superclass) {
        if (class_conformsToProtocol(tcls, protocol)) return YES;
    }
    return NO;
}

+ (NSUInteger)hash {
    return _objc_rootHash(self);
}

- (NSUInteger)hash {
    return _objc_rootHash(self);
}

+ (BOOL)isEqual:(id)obj {
    return obj == (id)self;
}

- (BOOL)isEqual:(id)obj {
    return obj == self;
}


+ (BOOL)isFault {
    return NO;
}

- (BOOL)isFault {
    return NO;
}

+ (BOOL)isProxy {
    return NO;
}

- (BOOL)isProxy {
    return NO;
}


+ (IMP)instanceMethodForSelector:(SEL)sel {
    if (!sel) [self doesNotRecognizeSelector:sel];
    return class_getMethodImplementation(self, sel);
}

+ (IMP)methodForSelector:(SEL)sel {
    if (!sel) [self doesNotRecognizeSelector:sel];
    return object_getMethodImplementation((id)self, sel);
}

- (IMP)methodForSelector:(SEL)sel {
    if (!sel) [self doesNotRecognizeSelector:sel];
    return object_getMethodImplementation(self, sel);
}

+ (BOOL)resolveClassMethod:(SEL)sel {
    return NO;
}

+ (BOOL)resolveInstanceMethod:(SEL)sel {
    return NO;
}

// Replaced by CF (throws an NSException)
+ (void)doesNotRecognizeSelector:(SEL)sel {
    _objc_fatal("+[%s %s]: unrecognized selector sent to instance %p", 
                class_getName(self), sel_getName(sel), self);
}

// Replaced by CF (throws an NSException)
- (void)doesNotRecognizeSelector:(SEL)sel {
    _objc_fatal("-[%s %s]: unrecognized selector sent to instance %p", 
                object_getClassName(self), sel_getName(sel), self);
}


+ (id)performSelector:(SEL)sel {
    if (!sel) [self doesNotRecognizeSelector:sel];
    return ((id(*)(id, SEL))objc_msgSend)((id)self, sel);
}

+ (id)performSelector:(SEL)sel withObject:(id)obj {
    if (!sel) [self doesNotRecognizeSelector:sel];
    return ((id(*)(id, SEL, id))objc_msgSend)((id)self, sel, obj);
}

+ (id)performSelector:(SEL)sel withObject:(id)obj1 withObject:(id)obj2 {
    if (!sel) [self doesNotRecognizeSelector:sel];
    return ((id(*)(id, SEL, id, id))objc_msgSend)((id)self, sel, obj1, obj2);
}

- (id)performSelector:(SEL)sel {
    if (!sel) [self doesNotRecognizeSelector:sel];
    return ((id(*)(id, SEL))objc_msgSend)(self, sel);
}

- (id)performSelector:(SEL)sel withObject:(id)obj {
    if (!sel) [self doesNotRecognizeSelector:sel];
    return ((id(*)(id, SEL, id))objc_msgSend)(self, sel, obj);
}

- (id)performSelector:(SEL)sel withObject:(id)obj1 withObject:(id)obj2 {
    if (!sel) [self doesNotRecognizeSelector:sel];
    return ((id(*)(id, SEL, id, id))objc_msgSend)(self, sel, obj1, obj2);
}


// Replaced by CF (returns an NSMethodSignature)
+ (NSMethodSignature *)instanceMethodSignatureForSelector:(SEL)sel {
    _objc_fatal("+[NSObject instanceMethodSignatureForSelector:] "
                "not available without CoreFoundation");
}

// Replaced by CF (returns an NSMethodSignature)
+ (NSMethodSignature *)methodSignatureForSelector:(SEL)sel {
    _objc_fatal("+[NSObject methodSignatureForSelector:] "
                "not available without CoreFoundation");
}

// Replaced by CF (returns an NSMethodSignature)
- (NSMethodSignature *)methodSignatureForSelector:(SEL)sel {
    _objc_fatal("-[NSObject methodSignatureForSelector:] "
                "not available without CoreFoundation");
}

+ (void)forwardInvocation:(NSInvocation *)invocation {
    [self doesNotRecognizeSelector:(invocation ? [invocation selector] : 0)];
}

- (void)forwardInvocation:(NSInvocation *)invocation {
    [self doesNotRecognizeSelector:(invocation ? [invocation selector] : 0)];
}

+ (id)forwardingTargetForSelector:(SEL)sel {
    return nil;
}

- (id)forwardingTargetForSelector:(SEL)sel {
    return nil;
}


// Replaced by CF (returns an NSString)
+ (NSString *)description {
    return nil;
}

// Replaced by CF (returns an NSString)
- (NSString *)description {
    return nil;
}

+ (NSString *)debugDescription {
    return [self description];
}

- (NSString *)debugDescription {
    return [self description];
}


+ (id)new {
    return [callAlloc(self, false/*checkNil*/) init];
}

+ (id)retain {
    return (id)self;
}

// Replaced by ObjectAlloc
- (id)retain {
    return _objc_rootRetain(self);
}


+ (BOOL)_tryRetain {
    return YES;
}

// Replaced by ObjectAlloc
- (BOOL)_tryRetain {
    return _objc_rootTryRetain(self);
}

+ (BOOL)_isDeallocating {
    return NO;
}

- (BOOL)_isDeallocating {
    return _objc_rootIsDeallocating(self);
}

+ (BOOL)allowsWeakReference { 
    return YES; 
}

+ (BOOL)retainWeakReference {
    return YES; 
}

- (BOOL)allowsWeakReference { 
    return ! [self _isDeallocating]; 
}

- (BOOL)retainWeakReference { 
    return [self _tryRetain]; 
}

+ (oneway void)release {
}

// Replaced by ObjectAlloc
- (oneway void)release {
    _objc_rootRelease(self);
}

+ (id)autorelease {
    return (id)self;
}

// Replaced by ObjectAlloc
- (id)autorelease {
    return _objc_rootAutorelease(self);
}

+ (NSUInteger)retainCount {
    return ULONG_MAX;
}

- (NSUInteger)retainCount {
    return _objc_rootRetainCount(self);
}

+ (id)alloc {
    return _objc_rootAlloc(self);
}

// Replaced by ObjectAlloc
+ (id)allocWithZone:(struct _NSZone *)zone {
    return _objc_rootAllocWithZone(self, (malloc_zone_t *)zone);
}

// Replaced by CF (throws an NSException)
+ (id)init {
    return (id)self;
}

- (id)init {
    return _objc_rootInit(self);
}

// Replaced by CF (throws an NSException)
+ (void)dealloc {
}


// Replaced by NSZombies
- (void)dealloc {
    _objc_rootDealloc(self);
}

// Previously used by GC. Now a placeholder for binary compatibility.
- (void) finalize {
}

+ (struct _NSZone *)zone {
    return (struct _NSZone *)_objc_rootZone(self);
}

- (struct _NSZone *)zone {
    return (struct _NSZone *)_objc_rootZone(self);
}

+ (id)copy {
    return (id)self;
}

+ (id)copyWithZone:(struct _NSZone *)zone {
    return (id)self;
}

- (id)copy {
    return [(id)self copyWithZone:nil];
}

+ (id)mutableCopy {
    return (id)self;
}

+ (id)mutableCopyWithZone:(struct _NSZone *)zone {
    return (id)self;
}

- (id)mutableCopy {
    return [(id)self mutableCopyWithZone:nil];
}

@end


