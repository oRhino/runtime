/*
 * Copyright (c) 2010-2011 Apple Inc. All rights reserved.
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

#ifndef _OBJC_WEAK_H_
#define _OBJC_WEAK_H_

#include <objc/objc.h>
#include "objc-config.h"

__BEGIN_DECLS

/*
The weak table is a hash table governed by a single spin lock.
An allocated blob of memory, most often an object, but under GC any such 
allocation, may have its address stored in a __weak marked storage location 
through use of compiler generated write-barriers or hand coded uses of the 
register weak primitive. Associated with the registration can be a callback 
block for the case when one of the allocated chunks of memory is reclaimed. 
The table is hashed on the address of the allocated memory.  When __weak 
marked memory changes its reference, we count on the fact that we can still 
see its previous reference.

So, in the hash table, indexed by the weakly referenced item, is a list of 
all locations where this address is currently being stored.
 
For ARC, we also keep track of whether an arbitrary object is being 
deallocated by briefly placing it in the table just prior to invoking 
dealloc, and removing it via objc_clear_deallocating just prior to memory 
reclamation.

*/

// The address of a __weak variable.
// These pointers are stored disguised so memory analysis tools
// don't see lots of interior pointers from the weak table into objects.

//__weak 变量的地址（objc_object **）。这些指针是伪装存储的，因此内存分析工具不会看到从 weak table 到 objects 的大量内部指针。
// 这里 T 是 objc_object *，那么 DisguisedPtr 里的 T* 就是 objc_object**，即为指针的指针
//用于伪装 __weak 变量的地址，即用于伪装 objc_object * 的地址。
typedef DisguisedPtr<objc_object *> weak_referrer_t;

//用于在不同的平台下标识位域长度。这里是用于 struct weak_entry_t 中的 num_refs 的位域长度。
#if __LP64__
#define PTR_MINUS_2 62
#else
#define PTR_MINUS_2 30
#endif

/**
 * The internal structure stored in the weak references table. 
 * It maintains and stores
 * a hash set of weak references pointing to an object.
 * If out_of_line_ness != REFERRERS_OUT_OF_LINE then the set
 * is instead a small inline array.
 */
//内部结构存储在弱引用表中。它维护并存储指向对象的弱引用的哈希集。
//如果 out_of_line_ness != REFERRERS_OUT_OF_LINE(0b10)，则该集合为小型内联数组（长度为 4 的 weak_referrer_t 数组）。
#define WEAK_INLINE_COUNT 4

// out_of_line_ness field overlaps with the low two bits of inline_referrers[1].
// inline_referrers[1] is a DisguisedPtr of a pointer-aligned address.
// The low two bits of a pointer-aligned DisguisedPtr will always be 0b00
// (disguised nil or 0x80..00) or 0b11 (any other address).
// Therefore out_of_line_ness == 0b10 is used to mark the out-of-line state.

//out_of_line_ness 字段与 inline_referrers [1] 的低两位内存空间重叠。inline_referrers [1] 是指针对齐地址的 DisguisedPtr。指针对齐的 DisguisedPtr 的低两位始终为 0b00（伪装为 nil 或 0x80..00）或 0b11（任何其他地址）。因此，out_of_line_ness == 0b10 可用于标记 out-of-line 状态，即 struct weak_entry_t 内部是使用哈希表存储 weak_referrer_t 而不再使用那个长度为 4 的 weak_referrer_t 数组。

#define REFERRERS_OUT_OF_LINE 2

//整体的功能是保存一个对象，然后保存一组该对象的弱引用。
//哈希表存储的数据是 weak_referrer_t，实质上是弱引用变量的地址，即 objc_object **new_referrer，通过操作指针的指针，就可以使得弱引用变量在对象析构后指向 nil。这里必须保存弱引用变量的地址，才能把它的指向置为 nil

struct weak_entry_t {
    // referent 中存放的是化身为整数的 objc_object 实例的地址，下面保存的一众弱引用变量都指向这个 objc_object 实例
    DisguisedPtr<objc_object> referent;
    
    // 当指向 referent 的弱引用个数小于等于 4 时使用 inline_referrers 数组保存这些弱引用变量的地址，
    // 大于4以后用referrers这个哈希数组保存。
    
    // 共用 32 个字节内存空间的联合体
    union {
        struct {
            weak_referrer_t *referrers; // 保存 weak_referrer_t 的哈希数组
            // out_of_line_ness 和 num_refs 构成位域存储，共占 64 位
            uintptr_t        out_of_line_ness : 2; // 标记使用哈希数组还是 inline_referrers 保存 weak_referrer_t
            uintptr_t        num_refs : PTR_MINUS_2; // 当前 referrers 内保存的 weak_referrer_t 的数量
            uintptr_t        mask; // referrers 哈希数组总长度减 1，会参与哈希函数计算
            // 可能会发生 hash 冲突的最大次数，用于判断是否出现了逻辑错误，（hash 表中的冲突次数绝对不会超过该值）
            // 该值在新建 weak_entry_t 和插入新的 weak_referrer_t 时会被更新，它一直记录的都是最大偏移值
            uintptr_t        max_hash_displacement;
        };
        //小于等于4,使用数组
        struct {
            // out_of_line_ness field is low bits of inline_referrers[1]
            // out_of_line_ness 和 inline_referrers[1] 的低两位的内存空间重合
            // 长度为 4 的 weak_referrer_t（Dsiguised<objc_object *>）数组
            weak_referrer_t  inline_referrers[WEAK_INLINE_COUNT];
        };
    };

    // 返回 true 表示使用 referrers 哈希数组 false 表示使用 inline_referrers 数组保存 weak_referrer_t
    bool out_of_line() {
        return (out_of_line_ness == REFERRERS_OUT_OF_LINE);
    }

    // 重载操作符
    // 赋值操作，直接使用 memcpy 函数拷贝 other 内存里面的内容到 this 中，
    // 而不是用复制构造函数什么的形式实现，应该也是为了提高效率考虑的...
    weak_entry_t& operator=(const weak_entry_t& other) {
        memcpy(this, &other, sizeof(other));
        return *this;
    }

    // weak_entry_t 的构造函数
    // newReferent 是原始对象的指针
    // newReferrer 是指向 newReferent 的弱引用变量的地址
    
    // 初始化列表 referent(newReferent) 会调用: DisguisedPtr(T* ptr) : value(disguise(ptr)) { } 构造函数，
    // 调用 disguise 函数把 newReferent 转化为一个整数赋值给 value
    weak_entry_t(objc_object *newReferent, objc_object **newReferrer)
        : referent(newReferent)
    {
        // 把 newReferrer 放在数组 0 位，也会调用 DisguisedPtr 构造函数，把 newReferrer 转化为整数保存
        inline_referrers[0] = newReferrer;
        for (int i = 1; i < WEAK_INLINE_COUNT; i++) {
            // 循环把 inline_referrers 数组的剩余 3 位都置为 nil
            inline_referrers[i] = nil;
        }
    }
};
//使用定长/哈希数组的切换，应该是考虑到实例对象的弱引用变量个数一般比较少，这时候使用定长数组不需要再动态的申请内存空间（union 中两个结构体共用 32  个字节内存）而是使用 weak_entry_t 初始化时一次分配的一块连续的内存空间，这会得到运行效率上的提升。


/**
 * The global weak references table. Stores object ids as keys,
 * and weak_entry_t structs as their values.
 */
//weak_table_t 是全局的保存弱引用的哈希表。以 object ids 为 keys，以 weak_entry_t 为 values。
#pragma mark - 弱引用表
struct weak_table_t {
    weak_entry_t *weak_entries; // 存储 weak_entry_t 的哈希数组
    size_t    num_entries; // 当前 weak_entries 内保存的 weak_entry_t 的数量，哈希数组内保存的元素个数
    uintptr_t mask; // 哈希数组的总长度减 1，会参与 hash 函数计算
    
    // 记录所有项的最大偏移量，即发生 hash 冲突的最大次数
    // 用于判断是否出现了逻辑错误，hash 表中的冲突次数绝对不会超过这个值，
    // 下面关于 weak_entry_t 的操作函数中会看到这个成员变量的使用，这里先对它有一些了解即可，
    // 因为会有 hash 碰撞的情况，而 weak_table_t 采用了开放寻址法来解决，
    // 所以某个 weak_entry_t 实际存储的位置并不一定是 hash 函数计算出来的位置

    uintptr_t max_hash_displacement;
};

/// Adds an (object, weak pointer) pair to the weak table.
id weak_register_no_lock(weak_table_t *weak_table, id referent, 
                         id *referrer, bool crashIfDeallocating);

/// Removes an (object, weak pointer) pair from the weak table.
void weak_unregister_no_lock(weak_table_t *weak_table, id referent, id *referrer);

#if DEBUG
/// Returns true if an object is weakly referenced somewhere.
bool weak_is_registered_no_lock(weak_table_t *weak_table, id referent);
#endif

/// Called on object destruction. Sets all remaining weak pointers to nil.
void weak_clear_no_lock(weak_table_t *weak_table, id referent);

__END_DECLS

#endif /* _OBJC_WEAK_H_ */
