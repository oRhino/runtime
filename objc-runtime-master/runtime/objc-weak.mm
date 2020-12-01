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

#include "objc-private.h"

#include "objc-weak.h"

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <libkern/OSAtomic.h>

//用于获取 weak_entry_t 或 weak_table_t 的哈希数组当前分配的总容量。
#define TABLE_SIZE(entry) (entry->mask ? entry->mask + 1 : 0)

/*
 在 weak_entry_t 中当对象的弱引用数量不超过 4 的时候是使用 weak_referrer_t inline_referrers[WEAK_INLINE_COUNT] 这个固定长度为 4 的数组存放 weak_referrer_t。当长度大于 4 以后使用 weak_referrer_t *referrers 这个哈希数组存放 weak_referrer_t 数据。


weak_table_t 的哈希数组初始长度是 64，当存储占比超过 3/4 后，哈希数组会扩容为总容量的 2 倍，然后会把之前的数据重新哈希化放在新空间内。当一些数据从哈希数组中移除后，为了提高查找效率势必要对哈希数组总长度做缩小操作，规则是当哈希数组总容量超过 1024 且已使用部分少于总容量 1/16 时，缩小为总容量的 1/8，缩小后同样会把原始数据重新哈希化放在新空间。（缩小和扩展都是使用 calloc 函数开辟新空间，cache_t 扩容后是直接忽略旧数据，这里可以比较记忆。）。牢记以上只是针对 weak_table_t 的哈希数组而言的。


weak_entry_t 则是首先用固定长度为 4 的数组，当有新的弱引用进来时，会首先判断当前是使用的 定长数组还是哈希数组，如果此时使用的还是定长数组的话先判断定长数组还有没有空位，如果没有空位的话会为哈希数组申请长度为 4 的并用一个循环把定长数组中的数据放在哈希数组，这里看似是按下标循环存放，其实下面会重新进行哈希化，然后是判断对哈希数组进行扩容，也是如果超过总占比的 3/4 进行扩容为总容量的 2 倍，所以 weak_entry_t 的哈希数组第一次扩容后是 8。然后下面区别就来了 weak_entry_t 的哈希数组是没有缩小机制的，移除弱引用的操作其实只是把弱引用的指向置为 nil，做移除操作是判断如果定长数组为空或者哈希数组为空，则会把 weak_table_t 哈希数组中的 weak_entry_t 移除，然后就是对 weak_table_t 做一些缩小容量的操作。


weak_entry_t 和 weak_table_t 可以共用 TABLE_SIZE 因为是它们对 mask 的使用机制是完全一样的。这里 weak_entry_t 之所以不缩小，且起始用定长数组，都是对其的优化，因为本来一个对象的弱引用数量就不会太多
 */

//往指定的 weak_entry_t 里面添加 new_referrer( weak 变量的地址)。这里只是声明，具体实现在后面，这个声明只是为了给下面的其他函数的提前调用作的声明。
static void append_referrer(weak_entry_t *entry, objc_object **new_referrer);

BREAKPOINT_FUNCTION(
    void objc_weak_error(void)
);

//表示 weak_table_t 中的的某个 weak_entry_t 发生了内存错误,在发生 hash 冲突时 index 持续增加直到和 begin 相等时被调用。
static void bad_weak_table(weak_entry_t *entries)
{
    _objc_fatal("bad weak table at %p. This may be a runtime bug or a "
                "memory error somewhere else.", entries); //退出程序或者中止运行并打印原因
}

/** 
 * Unique hash function for object pointers only.
 *  唯一的哈希函数仅适用于对象指针。
 * @param key The object pointer
 * 
 * @return Size unrestricted hash of pointer.
 */
static inline uintptr_t hash_pointer(objc_object *key) {
    // 把指针强转为 unsigned long，然后调用 ptr_hash 函数
    return ptr_hash((uintptr_t)key);
}

/** 
 * Unique hash function for weak object pointers only.
 * 
 * @param key The weak object pointer. 
 * 
 * @return Size unrestricted hash of pointer.
 */
//对一个 objc_object 对象的指针求哈希值，用于从 weak_table_t 哈希表中取得对象对应的 weak_entry_t。
//对一个 objc_object 对象的指针的指针（此处指 weak 变量的地址）求哈希值，用于从 weak_entry_t 哈希表中取得 weak_referrer_t 把其保存的弱引用变量的指向置为 nil 或者从哈希表中移除等。
static inline uintptr_t w_hash_pointer(objc_object **key) {
    return ptr_hash((uintptr_t)key);
}

/** 
 * Grow the entry's hash table of referrers. Rehashes each
 * of the referrers.
 * 
 * @param entry Weak pointer hash set for a particular object.
 */
//对 weak_entry_t 的哈希数组进行扩容，并插入一个新的 new_referrer，原有数据重新哈希化放在新空间内。
__attribute__((noinline, used))
static void grow_refs_and_insert(weak_entry_t *entry, 
                                 objc_object **new_referrer)
{
    //// DEBUG 下的断言，确保当前 weak_entry_t 使用的是 hash 数组模式
    ASSERT(entry->out_of_line());

    //// 新容量为旧容量的 2 倍
    size_t old_size = TABLE_SIZE(entry);
    size_t new_size = old_size ? old_size * 2 : 8;

    // 记录当前已使用容量
    size_t num_refs = entry->num_refs;
    // 记录旧哈希数组起始地址，在最后要进行释放
    weak_referrer_t *old_refs = entry->referrers;
    // mask 依然是总容量减 1
    entry->mask = new_size - 1;
    
    //// 为新 hash 数组申请空间
    // 长度为：总容量 * sizeof(weak_referrer_t)（8）个字节
    entry->referrers = (weak_referrer_t *)
        calloc(TABLE_SIZE(entry), sizeof(weak_referrer_t));
    
    // 默认为 0
    entry->num_refs = 0;
    entry->max_hash_displacement = 0;
    
    for (size_t i = 0; i < old_size && num_refs > 0; i++) {
        if (old_refs[i] != nil) {
            // 把旧哈希数组里的数据都放进新哈希数组内
            append_referrer(entry, old_refs[i]);
            // 旧的哈希数组的长度自减
            num_refs--;
        }
    }
    // Insert
    // 然后把入参传入的 new_referrer，插入新哈希数组，前面的铺垫都是在做 "数据转移"
    append_referrer(entry, new_referrer);
    // 把旧哈希数据释放
    if (old_refs) free(old_refs);
}

/** 
 * Add the given referrer to set of weak pointers in this entry.
 * 添加给定的 referrer 到 weak_entry_t 的哈希数组（或定长为 4 的内部数组）。
 * Does not perform duplicate checking (b/c weak pointers are never
 * added to a set twice). 
 * 不执行重复检查，weak 指针永远不会添加两次。
 * @param entry The entry holding the set of weak pointers. 
 * @param new_referrer The new weak pointer to be added.
 */
//添加给定的 referrer 到 weak_entry_t 的哈希数组（或定长为 4 的内部数组）
static void append_referrer(weak_entry_t *entry, objc_object **new_referrer)
{
    if (! entry->out_of_line()) {
        // Try to insert inline.
        // 如果 weak_entry 尚未使用哈希数组，走这里
        for (size_t i = 0; i < WEAK_INLINE_COUNT; i++) {
            // 找到空位把 new_referrer 放进去
            if (entry->inline_referrers[i] == nil) {
                entry->inline_referrers[i] = new_referrer;
                return;
            }
        }

        // Couldn't insert inline. Allocate out of line.
        // 如果 inline_referrers 存满了，则要转到 referrers 哈希数组
        // 为哈希数组申请空间
        weak_referrer_t *new_referrers = (weak_referrer_t *)
            calloc(WEAK_INLINE_COUNT, sizeof(weak_referrer_t));
        
        // This constructed table is invalid, but grow_refs_and_insert
        // will fix it and rehash it.
        // 此构造的 table 无效，grow_refs_and_insert 将修复它并重新哈希
                
        // 把 inline_referrers 内部的数据放进 hash 数组
        // 这里看似是直接循环按下标放的，其实后面会进行扩容和哈希化

        for (size_t i = 0; i < WEAK_INLINE_COUNT; i++) {
            new_referrers[i] = entry->inline_referrers[i];
        }
        // 给 referrers 赋值
        entry->referrers = new_referrers;
        // 表示目前弱引用是 4
        entry->num_refs = WEAK_INLINE_COUNT;
        
        // out_of_line_ness 置为 REFERRERS_OUT_OF_LINE，
        // 标记 weak_entry_t 开始使用哈希数组保存弱引用的指针
        entry->out_of_line_ness = REFERRERS_OUT_OF_LINE;
        
        // 看到这里有一个减 1 的操作
        // mask 赋值，总容量减 1
        entry->mask = WEAK_INLINE_COUNT-1;
        // 此时哈希冲突偏移为 0
        entry->max_hash_displacement = 0;
    }

    // 对于动态数组的扩容处理
    // 断言： 此时一定使用的动态数组
    ASSERT(entry->out_of_line());

    // #define TABLE_SIZE(entry) (entry->mask ? entry->mask + 1 : 0)
    // mask 又加了 1
    // 如果大于总容量的 3/4
    if (entry->num_refs >= TABLE_SIZE(entry) * 3/4) {
        // weak_entry_t 哈希数组扩容并插入 new_referrer
        return grow_refs_and_insert(entry, new_referrer);
    }
    
    // 不需要扩容，则进行正常插入
    size_t begin = w_hash_pointer(new_referrer) & (entry->mask);
    size_t index = begin;
    size_t hash_displacement = 0;
    while (entry->referrers[index] != nil) {
        
        hash_displacement++;
        index = (index+1) & entry->mask;
        // 在 index == begin 之前一定能找到空位置，因为前面已经有一个超过 3/4 占用后的扩容机制，
        if (index == begin) bad_weak_table(entry);
    }
    
    // 更新最大偏移值
    if (hash_displacement > entry->max_hash_displacement) {
        entry->max_hash_displacement = hash_displacement;
    }
    // 找到空位置放入弱引用的指针
    weak_referrer_t &ref = entry->referrers[index];
    ref = new_referrer;
    
    // 自增
    entry->num_refs++;
}

/** 
 * Remove old_referrer from set of referrers, if it's present.
 * Does not remove duplicates, because duplicates should not exist. 
 * 
 * @todo this is slow if old_referrer is not present. Is this ever the case? 
 *
 * @param entry The entry holding the referrers.
 * @param old_referrer The referrer to remove. 
 */
//从 weak_entry_t 的哈希数组（或定长为 4 的内部数组）中删除弱引用的地址。
static void remove_referrer(weak_entry_t *entry, objc_object **old_referrer)
{
    // 如果目前使用的是定长为 4 的内部数组
    if (! entry->out_of_line()) {
        // 循环找到 old_referrer 的位置，把它的原位置放置 nil，表示把 old_referrer 从数组中移除了
        for (size_t i = 0; i < WEAK_INLINE_COUNT; i++) {
            if (entry->inline_referrers[i] == old_referrer) {
                entry->inline_referrers[i] = nil;
                return;
            }
        }
        // 如果当前 weak_entry_t 不包含传入的 old_referrer
        // 则明显发生了错误，执行 objc_weak_error 函数
        _objc_inform("Attempted to unregister unknown __weak variable "
                     "at %p. This is probably incorrect use of "
                     "objc_storeWeak() and objc_loadWeak(). "
                     "Break on objc_weak_error to debug.\n", 
                     old_referrer);
        objc_weak_error();
        return;
    }

    // 从 hash 数组中找到 old_referrer 并置为 nil（移除 old_referrer）
    size_t begin = w_hash_pointer(old_referrer) & (entry->mask);
    size_t index = begin;
    size_t hash_displacement = 0;
    while (entry->referrers[index] != old_referrer) {
        index = (index+1) & entry->mask;
        if (index == begin) bad_weak_table(entry);
        hash_displacement++;
        if (hash_displacement > entry->max_hash_displacement) {
            _objc_inform("Attempted to unregister unknown __weak variable "
                         "at %p. This is probably incorrect use of "
                         "objc_storeWeak() and objc_loadWeak(). "
                         "Break on objc_weak_error to debug.\n", 
                         old_referrer);
            objc_weak_error();
            return;
        }
    }
    // 把 old_referrer 所在的位置置为 nil，num_refs 自减
    entry->referrers[index] = nil;
    entry->num_refs--;
}

/** 
 * Add new_entry to the object's table of weak references.
 * 添加 new_entry 到对象的弱引用表中。
 * Does not check whether the referent is already in the table.
 * 不检查引用对象是否已在表中。
 */
//把 weak_entry_t 添加到 weak_table_t->weak_entries 中。
static void weak_entry_insert(weak_table_t *weak_table, weak_entry_t *new_entry)
{
    // 哈希数组的起始地址
    weak_entry_t *weak_entries = weak_table->weak_entries;
    ASSERT(weak_entries != nil);

    // 调用 hash 函数找到 new_entry 在 weak_table_t 的哈希数组中的位置，可能会发生 hash 冲突
    // & mask 的原理同上
    size_t begin = hash_pointer(new_entry->referent) & (weak_table->mask);
    size_t index = begin;
    size_t hash_displacement = 0;
    while (weak_entries[index].referent != nil) {
        // 如果发生哈希冲突，+1 继续向下探测
        index = (index+1) & weak_table->mask;
        // 如果 index 每次加 1 加到值等于 begin 还是没有找到空位置，则触发 bad_weak_table 致命错误
        if (index == begin) bad_weak_table(weak_entries);
        // 记录偏移值，用于更新 max_hash_displacement
        hash_displacement++;
    }
    // new_entry 放入哈希数组
    weak_entries[index] = *new_entry;
    // 更新 num_entries
    weak_table->num_entries++;

    // 此步操作正记录了 weak_table_t 哈希数组发生哈希冲突时的最大偏移值
    if (hash_displacement > weak_table->max_hash_displacement) {
        weak_table->max_hash_displacement = hash_displacement;
    }
}
// weak_resize对哈希数组进行的扩大或缩小，首先根据 new_size 申请相应大小的内存，new_entries 指针指向这块新申请的内存。设置 weak_table 的 mask 为 new_size - 1。此处 mask 的作用是记录 weak_table 总容量的内存边界，此外 mask 还用在哈希函数中保证 index 不会哈希数组越界。
// weak_table_t 的哈希数组可能会发生哈希碰撞，而 weak_table_t 使用了开放寻址法来处理碰撞。如果发生碰撞的话，将寻找相邻（如果已经到最尾端的话，则从头开始）的下一个空位。max_hash_displacement 记录当前 weak_table 发生过的最大的偏移值。此值会在其他地方用到，例如：weak_entry_for_referent 函数，寻找给定的 referent 的在弱引用表中的 entry 时，如果在循环过程中 hash_displacement 的值超过了 weak_table->max_hash_displacement 则表示，不存在要找的 weak_entry_t。

static void weak_resize(weak_table_t *weak_table, size_t new_size)
{
    //// old_size = mask + 1; 表示原哈希数组的总长度
    size_t old_size = TABLE_SIZE(weak_table);

    //// 旧的 weak_entries 哈希数组起始地址
    weak_entry_t *old_entries = weak_table->weak_entries;
    
    // 为新的 weak_entries 哈希数组申请指定长度的空间，并把起始地址返回
    // 内存空间总容量为: new_size * sizeof(weak_entry_t)
    weak_entry_t *new_entries = (weak_entry_t *)
        calloc(new_size, sizeof(weak_entry_t));

    // 更新 mask ，仍是总长度减 1
    weak_table->mask = new_size - 1;
    // 更新 hash 数组起始地址
    weak_table->weak_entries = new_entries;
    
    // 最大哈希冲突偏移值，默认为 0
    weak_table->max_hash_displacement = 0;
    // 当前哈希数组的占用数量，默认为 0
    weak_table->num_entries = 0;  // restored by weak_entry_insert below
    
    // 以上 weak_table_t 的两个成员变量会在下面的 weak_entry_insert 函数中得到更新
        
    // 如果有旧 weak_entry_t 需要放到新空间内
    if (old_entries) {
        weak_entry_t *entry;
        // 旧哈希数组的末尾
        weak_entry_t *end = old_entries + old_size;
        
        // 循环调用 weak_entry_insert 把旧哈希数组中的 weak_entry_t 插入到新的哈希数组中
        for (entry = old_entries; entry < end; entry++) {
            if (entry->referent) {
                weak_entry_insert(weak_table, entry);
            }
        }
        // 最后释放旧的哈希数组的空间
        free(old_entries);
    }
}

// Grow the given zone's table of weak references if it is full.
// 如果给定区域的弱引用表已满，则对其进行扩展。
//该函数用于扩充 weak_table_t 的 weak_entry_t *weak_entries 的长度，扩充条件是 num_entries 超过了 mask + 1 的 3/4。看到 weak_entries 的初始化长度是 64，每次扩充的长度则是 mask + 1 的 2 倍，扩容完毕后会把原哈希数组中的 weak_entry_t 重新哈希化插入到新空间内，并更新 weak_tabl_t 各成员变量。占据的内存空间的总容量则是 (mask + 1) * sizeof(weak_entry_t) 字节。
//综上 mask + 1 总是 2 的 N 次方。（2^6 = 64，即 N >= 6）

static void weak_grow_maybe(weak_table_t *weak_table)
{
    // #define TABLE_SIZE(entry) (entry->mask ? entry->mask + 1 : 0)
    // mask + 1 表示当前 weak_table 哈希数组的总长度
    size_t old_size = TABLE_SIZE(weak_table);

    // Grow if at least 3/4 full.
    // 如果目前哈希数组中存储的 weak_entry_t 的数量超过了总长度的 3/4，则进行扩容
    if (weak_table->num_entries >= old_size * 3 / 4) {
        // 如果是 weak_table 是新建的，则初始其哈希数组长度为 64，如果是非空，则扩容为之前长度的两倍
        weak_resize(weak_table, old_size ? old_size*2 : 64);
    }
}

// Shrink the table if it is mostly empty.
//在 weak_entry_remove 函数中调用，旨在 weak_entry_t 从 weak_table_t 的哈希数组中移除后，缩小 weak_entry_t *weak_entries 的空间

// 即当 weak_table_t 的 weak_entry_t *weak_entries 数组大部分空间为空的情况下，缩小 weak_entries 的长度
// 缩小 weak_entry_t *weak_entries 容量的条件是目前的总长度超过了 1024 并且容量占用小于 1/16，weak_entries 空间缩小到当前空间的 1/8。

static void weak_compact_maybe(weak_table_t *weak_table)
{
    // #define TABLE_SIZE(entry) (entry->mask ? entry->mask + 1 : 0)
    // 统计当前哈希数组的总长度
    size_t old_size = TABLE_SIZE(weak_table);

    // Shrink if larger than 1024 buckets and at most 1/16 full.
    // old_size 超过了 1024 并且 低于 1/16 的空间占用k率则进行缩小
    if (old_size >= 1024  && old_size / 16 >= weak_table->num_entries) {
        // 缩小容量为 ols_size 的 1/8
        weak_resize(weak_table, old_size / 8);
        
        // 缩小为 1/8 和上面的空间占用少于 1/16，两个条件合并在一起，保证缩小后的容量占用少于 1/2
        // leaves new table no more than 1/2 full
    }
}


/**
 * Remove entry from the zone's table of weak references.
 */
//从 weak_table_t 的哈希数组中删除指定的 weak_entry_t。
static void weak_entry_remove(weak_table_t *weak_table, weak_entry_t *entry)
{
    // remove entry
    // 如果 weak_entry_t 当前使用动态 hash 数组，则释放其内存
    if (entry->out_of_line()) free(entry->referrers);
    
    // 把从 entry 开始的 sizeof(*entry) 个字节空间置为 0
    bzero(entry, sizeof(*entry));

    // num_entries 自减
    weak_table->num_entries--;

    // 缩小 weak_table_t 的哈希数组容量
    weak_compact_maybe(weak_table);
}


/** 
 * Return the weak reference table entry for the given referent. 
 * If there is no entry for referent, return NULL. 
 * Performs a lookup.
 *
 * @param weak_table  通过 &SideTables()[referent],可从全局的 SideTables 中找到 referent 所处的 SideTable->weak_table_t
 * @param referent The object. Must not be nil.  返回值是 weak_entry_t 指针，weak_entry_t 中保存了 referent 的所有弱引用变量的地址
 * 
 * @return The table of weak referrers to this object. 
 */
//根据给定的 referent 和 weak_table_t 哈希表，查找其中的 weak_entry_t 并返回，如果未找到则返回 NULL。

static weak_entry_t *
weak_entry_for_referent(weak_table_t *weak_table, objc_object *referent)
{
    ASSERT(referent);

    // weak_table_t 中哈希数组的入口
    weak_entry_t *weak_entries = weak_table->weak_entries;

    if (!weak_entries) return nil;
    
    // 哈希函数：hash_pointer 函数返回值与 mask 做与操作，防止 index 越界
    size_t begin = hash_pointer(referent) & weak_table->mask;
    //首先是 mask 的值一直是 2 的 N 次方减 1 ，根据 weak_grow_maybe 函数，我们会看到哈希数组（weak_entry_t *weak_entries）的长度最小是 64，即 2 的 6 次方（N >= 6），以后的每次扩容是之前的长度乘以 2，即总长度永远是 2 的 N 次方，然后 mask 是 2 的 N 次方减 1，转为二进制的话：mask 一直是: 0x0111111(64 - 1，N = 6)、0x01111111(128 -1，N = 7)...., 即 mask 的二进制表示中后 N 位总是 1，之前的位总是 0，所以任何数与 mask 做与操作的结果总是在 [0, mask] 这个区间内。例如任何数与 0x0111111(64 - 1，N = 6) 做与操作的话结果总是在 [0, 63] 这个区间内。而这个正是 weak_entry_t *weak_entries 数组的下标范围。

    size_t index = begin;
    size_t hash_displacement = 0;
    
    // 如果未发生哈希冲突的话，这 weak_table->weak_entries[index] 就是要找的 weak_entry_t
    while (weak_table->weak_entries[index].referent != referent) {
        // 如果发生了哈希冲突，+1 继续往下探测（开放寻址法）
        index = (index+1) & weak_table->mask;
        
        // 如果 index 每次加 1 加到值等于 begin 还没有找到 weak_entry_t，则触发 bad_weak_table 致命错误
        if (index == begin) bad_weak_table(weak_table->weak_entries);
        
        // 记录探测偏移了多远
        hash_displacement++;
        // 如果探测偏移超过了 weak_table_t 的 max_hash_displacement，
        // 说明在 weak_table 中没有 referent 的 weak_entry_t，则直接返回 nil
        if (hash_displacement > weak_table->max_hash_displacement) {
            return nil;
        }
    }
    // 到这里遍找到了 weak_entry_t，然后取它的地址返回
    return &weak_table->weak_entries[index];
}

/** 
 * Unregister an already-registered weak reference.
 * This is used when referrer's storage is about to go away, but referent
 * isn't dead yet. (Otherwise, zeroing referrer later would be a
 * bad memory access.)
 * Does nothing if referent/referrer is not a currently active weak reference.
 * Does not zero referrer.
 * 
 * FIXME currently requires old referent value to be passed in (lame)
 * FIXME unregistration should be automatic if referrer is collected
 * 注销以前注册的弱引用。该方法用于 referrer 的存储即将消失，但是 referent 还正常存在。（否则，referrer 被释放后，可能会造成一个错误的内存访问，即对象还没有释放，但是 weak 变量已经释放了，这时候再去访问 weak 变量会导致野指针访问。）如果  referent/referrer 不是当前有效的弱引用，则不执行任何操作。
 
 * @param weak_table The global weak table.
 * @param referent The object.
 * @param referrer The weak reference.
 */
/// 从 referent 对应的 weak_entry_t 的哈希数组（或定长为 4 的内部数组）中注销指定的弱引用。
void
weak_unregister_no_lock(weak_table_t *weak_table, id referent_id, 
                        id *referrer_id)
{
    // id 转化为 objc_object * 对象的指针
    objc_object *referent = (objc_object *)referent_id;
    // referrer_id 是指向 weak 变量的地址，所以这里是 **
    objc_object **referrer = (objc_object **)referrer_id;

    weak_entry_t *entry;

    if (!referent) return;
    // 从 weak_table 中找到 referent 的 weak_entry_t
    if ((entry = weak_entry_for_referent(weak_table, referent))) {
        // 找到了这个 entry，就删除 weak_entry_t 的哈希数组（或定长为 4 的内部数组）中的 referrer
        remove_referrer(entry, referrer);
        bool empty = true;
        
        // 注销 referrer 以后判断是否需要删除对应的 weak_entry_t，
        // 如果 weak_entry_t 目前使用哈希数组，且 num_refs 不为 0，
        // 表示此时哈希数组还不为空，不需要删除
        if (entry->out_of_line()  &&  entry->num_refs != 0) {
            empty = false;
        }
        else {
            // 循环判断 weak_entry_t 内部定长为 4 的数组内是否还有 weak_referrer_t
            for (size_t i = 0; i < WEAK_INLINE_COUNT; i++) {
                if (entry->inline_referrers[i]) {
                    empty = false; 
                    break;
                }
            }
        }

        // 如果 entry 中的弱引用的地址都已经清空了，则连带也删除这个 entry，类似数组已经空了，则把数组也删了
        if (empty) {
            weak_entry_remove(weak_table, entry);
        }
    }

    // Do not set *referrer = nil. objc_storeWeak() requires that the 
    // value not change.
}

/** 
 * Registers a new (object, weak pointer) pair. Creates a new weak
 * object entry if it does not exist.
 * 注册一个新的 (对象，weak 指针) 对。
 * 创建一个新的 weak object entry（weak_entry_t），如果它不存在的话。
 * @param weak_table The global weak table. referent_id 所处的 weak_table_t 表
 * @param referent The object pointed to by the weak reference. weak 弱引用指向的对象
 * @param referrer The weak pointer address. weak 指针地址
 */
/// 把一个对象和对象的弱引用的指针注册到 weak_table_t 的 weak_entry_t 中。

//1.首先判断 referent 是否是 Tagged Pointer，如果不是则执行接下来的流程。Tagged Pointer 是不支持弱引用的。
//2.判断对象是否释放和对象是否支持弱引用。继承自 NSObject 类 默认支持，NSObject.mm 文件中找到 allowsWeakReference 函数，看到类方法默认返回 YES，实例方法，如果对象没有释放则返回 YES。
//3.根据 deallocating（对象是否正在释放的标志和对象是否支持弱引用）和入参 crashIfDeallocating 判断是否中止程序运行。
//4.在 weak_table_t 中去找 referent 对应的 weak_entry_t，如果能找到 entry，则调用 append_referrer 函数把对象弱引用的指针 referrer 插入 weak_entry_t 的哈希数组中（或者是定长为 4 的内部数组中）。
//5.如果没有找到对应的 weak_entry_t，则首先创建一个 new_entry，然后先执行 weak_grow_maybe 扩容，然后调用 weak_entry_insert 把 new_entry 插入 weak_table_t 的哈希数组中。
id 
weak_register_no_lock(weak_table_t *weak_table, id referent_id, 
                      id *referrer_id, bool crashIfDeallocating)
{
    // 对象指针
    objc_object *referent = (objc_object *)referent_id;
    // weak 变量的地址
    objc_object **referrer = (objc_object **)referrer_id;

    // 如果对象不存在或者是一个 Tagged Pointer 的话，直接返回对象。
    if (!referent  ||  referent->isTaggedPointer()) return referent_id;

    // ensure that the referenced object is viable
    // 判断对象是否正在进行释放操作
    bool deallocating;
    if (!referent->ISA()->hasCustomRR()) {
        deallocating = referent->rootIsDeallocating();
    }
    else {
        // 判断入参对象是否能进行 weak 引用 allowsWeakReference
        BOOL (*allowsWeakReference)(objc_object *, SEL) = 
            (BOOL(*)(objc_object *, SEL))
            object_getMethodImplementation((id)referent, 
                                           @selector(allowsWeakReference));
        if ((IMP)allowsWeakReference == _objc_msgForward) {
            return nil;
        }
        // 通过函数指针执行函数
        deallocating =
            ! (*allowsWeakReference)(referent, @selector(allowsWeakReference));
    }

    // 如果对象正在进行释放或者该对象不能进行 weak 引用，且 crashIfDeallocating 为 true，则抛出 crash
    if (deallocating) {
        if (crashIfDeallocating) {
            _objc_fatal("Cannot form weak reference to instance (%p) of "
                        "class %s. It is possible that this object was "
                        "over-released, or is in the process of deallocation.",
                        (void*)referent, object_getClassName((id)referent));
        } else {
            return nil;
        }
    }

    // now remember it and where it is being stored
    // 在 weak_table 中找 referent 对应的 weak_entry_t
    weak_entry_t *entry;
    if ((entry = weak_entry_for_referent(weak_table, referent))) {
        // 如果找到了，调用 append_referrer，把 __weak 变量的地址放进哈希数组
        append_referrer(entry, referrer);
    } 
    else {
        // 如果没有找到 entry，创建一个新的 entry
        weak_entry_t new_entry(referent, referrer);
        // 判断 weak_table_t 是否需要扩容
        weak_grow_maybe(weak_table);
        // 把 weak_entry_t 插入到 weak_table_t 的哈希数组中
        weak_entry_insert(weak_table, &new_entry);
    }

    // Do not set *referrer. objc_storeWeak() requires that the 
    // value not change.
    // 不要设置 *referrer。objc_storeWeak() 要求值不变。
    
    // 返回 referent_id
    return referent_id;
}


//DEBUG 模式下调用的函数。判断一个对象是否注册在 weak_table_t 中，是否注册可以理解为一个对象是否存在弱引用。（已注册 = 存在弱引用，未注册 = 不存在弱引用，当对象存在弱引用时，系统一定会把它注册到 weak_table_t 中，即能在 weak_table_t 的哈希数组中找到 weak_entry_t）。
#if DEBUG
bool
weak_is_registered_no_lock(weak_table_t *weak_table, id referent_id) 
{
    //// 调用 weak_entry_for_referent 判断对象是否存在对应的 entry
    return weak_entry_for_referent(weak_table, (objc_object *)referent_id);
}
#endif


/** 
 * Called by dealloc; nils out all weak pointers that point to the 
 * provided object so that they can no longer be used.
 * 
 * @param weak_table 
 * @param referent The object being deallocated. 
 */
//当对象的 dealloc 函数执行时会调用此函数，主要功能是当对象被释放废弃时，把该对象的弱引用指针全部指向 nil。
//当对象执行 dealloc 时会调用该函数，首先根据入参 referent_id 找到其在 weak_table 中对应的 weak_entry_t，然后遍历 weak_entry_t 的哈希数组或者 inline_referrers 定长数组通过里面存储的 weak 变量的地址，把 weak 变量指向置为 nil，最后把 weak_entry_t 从 weak_table 中移除。
//由 dealloc 调用，清除所有指向所提供对象的弱引用，以便不再使用它们。
void 
weak_clear_no_lock(weak_table_t *weak_table, id referent_id) 
{
    // referent 待销毁的对象
    objc_object *referent = (objc_object *)referent_id;

    // 从 weak_table_t 的哈希数组中找到 referent 对应的 weak_entry_t
    weak_entry_t *entry = weak_entry_for_referent(weak_table, referent);
    // 如果 entry 不存在，则返回
    if (entry == nil) {
        /// XXX shouldn't happen, but does with mismatched CF/objc
        //printf("XXX no entry for clear deallocating %p\n", referent);
        return;
    }
    // zero out references
    // 用于记录 weak_referrer_t
    // typedef DisguisedPtr<objc_object *> weak_referrer_t;
    //// 临时变量，记录 weak_entry_t 的哈希数组的起始地址，（或者是定长 weak_referrer_t 数组的起始地址）
    weak_referrer_t *referrers;
    size_t count;
    
    // 如果目前 weak_entry_t 使用哈希数组
    if (entry->out_of_line()) {
        // 记录哈希数组入口
        referrers = entry->referrers;
        //总长度
        // weak_entry_t 的 mask 和 weak_table_t 的 mask 都是总长度减 1
                
        // 记录长度
        count = TABLE_SIZE(entry);
    } 
    else {
        // 如果目前对象弱引用数量不超过 4 则使用 inline_referrers 数组记录弱引用的指针
        // 记录 inline_referrers 的入口
        referrers = entry->inline_referrers;
        
        // count 是 4
        count = WEAK_INLINE_COUNT;
    }
    
    // 循环把 inline_referrers 数组或者 hash 数组中的 weak 变量指向置为 nil
    for (size_t i = 0; i < count; ++i) {
        // weak 变量的指针的指针
        objc_object **referrer = referrers[i];
        // 如果 weak 变量指向 referent，则把其指向置为 nil
         if (referrer) {
            if (*referrer == referent) {
                *referrer = nil; //置为nil
            }
            else if (*referrer) {
                // 如果 weak_entry_t 里面存放的 weak 变量指向的对象不是 referent，
                // 可能是错误调用 objc_storeWeak 和 objc_loadWeak 函数导致，
                // 执行 objc_weak_error 进行 debug

                _objc_inform("__weak variable at %p holds %p instead of %p. "
                             "This is probably incorrect use of "
                             "objc_storeWeak() and objc_loadWeak(). "
                             "Break on objc_weak_error to debug.\n", 
                             referrer, (void*)*referrer, (void*)referent);
                objc_weak_error();
            }
        }
    }
    // 最后把 entry 从 weak_table_t 中移除
    weak_entry_remove(weak_table, entry);
}

