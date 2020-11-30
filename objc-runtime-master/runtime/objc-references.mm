/*
 * Copyright (c) 2004-2007 Apple Inc. All rights reserved.
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
/*
  Implementation of the weak / associative references for non-GC mode.
*/


#include "objc-private.h"
#include <objc/message.h>
#include <map>
#include "DenseMapExtras.h"

// expanded policy bits.
//内存策略
enum {
    OBJC_ASSOCIATION_SETTER_ASSIGN      = 0,
    OBJC_ASSOCIATION_SETTER_RETAIN      = 1,
    OBJC_ASSOCIATION_SETTER_COPY        = 3,            // NOTE:  both bits are set, so we can simply test 1 bit in releaseValue below.
    OBJC_ASSOCIATION_GETTER_READ        = (0 << 8),
    OBJC_ASSOCIATION_GETTER_RETAIN      = (1 << 8),
    OBJC_ASSOCIATION_GETTER_AUTORELEASE = (2 << 8)
};

//一个全局的自旋锁（互斥锁），保证 AssociationsManager 中对 AssociationsHashMap 操作的线程安全。
spinlock_t AssociationsManagerLock;

#pragma mark - 关联对象的实体内容
namespace objc {

class ObjcAssociation {
    uintptr_t _policy; //内存策略
    id _value;  //属性值
public:
    // 构造函数，初始化列表初始化 policy 和 value
    ObjcAssociation(uintptr_t policy, id value) : _policy(policy), _value(value) {}
    // 构造函数，初始化列表，policy 初始化为 0, value 初始化为 nil
    ObjcAssociation() : _policy(0), _value(nil) {}
    // 复制构造函数采用默认
    ObjcAssociation(const ObjcAssociation &other) = default;
    // 赋值操作符采用默认
    ObjcAssociation &operator=(const ObjcAssociation &other) = default;
    // 和other交换 policy 和 value
    ObjcAssociation(ObjcAssociation &&other) : ObjcAssociation() {
        swap(other);
    }

    inline void swap(ObjcAssociation &other) {
        std::swap(_policy, other._policy);
        std::swap(_value, other._value);
    }
    // 内联函数获取 _policy
    inline uintptr_t policy() const { return _policy; }
    // 内联函数获取 _value
    inline id value() const { return _value; }

    // 在 SETTER 时使用：判断是否需要持有 value
    inline void acquireValue() {
        if (_value) {
            switch (_policy & 0xFF) {
            case OBJC_ASSOCIATION_SETTER_RETAIN:
                // retain
                _value = objc_retain(_value);
                break;
            case OBJC_ASSOCIATION_SETTER_COPY:
                // copy
                _value = ((id(*)(id, SEL))objc_msgSend)(_value, @selector(copy));
                break;
            }
        }
    }

    // 在SETTER 时使用：与上面的 acquireValue 函数对应，释放旧值 value
    inline void releaseHeldValue() {
        if (_value && (_policy & OBJC_ASSOCIATION_SETTER_RETAIN)) {
            // release 减少引用计数
            objc_release(_value);
        }
    }
    // 在 GETTER 时使用：根据关联策略判断是否对关联值进行 retain 操作
    inline void retainReturnedValue() {
        if (_value && (_policy & OBJC_ASSOCIATION_GETTER_RETAIN)) {
            objc_retain(_value);
        }
    }
    // 在 GETTER 时使用：判断是否需要放进自动释放池
    inline id autoreleaseReturnedValue() {
        if (slowpath(_value && (_policy & OBJC_ASSOCIATION_GETTER_AUTORELEASE))) {
            return objc_autorelease(_value);
        }
        return _value;
    }
};
/// 一个对象的表 属性名 :关联对象实体
///key 是 const void * value 是 ObjcAssociation 的哈希表
typedef DenseMap<const void *, ObjcAssociation> ObjectAssociationMap;

/// 对象的地址:对象的所有关联对象表
//key 是 DisguisedPtr<objc_object> value 是 ObjectAssociationMap 的哈希表
//DisguisedPtr<objc_object> 可理解为把 objc_object 地址伪装为一个整数。
typedef DenseMap<DisguisedPtr<objc_object>, ObjectAssociationMap> AssociationsHashMap;


// class AssociationsManager manages a lock / hash table singleton pair.
// Allocating an instance acquires the lock
#pragma mark - 关联对象管理者
class AssociationsManager {
    using Storage = ExplicitInitDenseMap<DisguisedPtr<objc_object>, ObjectAssociationMap>;
    static Storage _mapStorage; //静态变量

public:
    //加锁lock，并不代表唯一，只是为了避免多线程重复创建，其实在外面是可以定义多个AssociationsManager
    AssociationsManager()   { AssociationsManagerLock.lock(); } //构造函数
    ~AssociationsManager()  { AssociationsManagerLock.unlock(); } //析构函数

    //获取全局关联对象哈希表
    AssociationsHashMap &get() {
        return _mapStorage.get(); //从静态变量中获取,所以全局唯一
    }

    static void init() {
        _mapStorage.init();
    }
};

AssociationsManager::Storage AssociationsManager::_mapStorage;

} // namespace objc

using namespace objc;

void
_objc_associations_init()
{
    AssociationsManager::init();
}

id
_object_get_associative_reference(id object, const void *key)
{
    // 局部变量
    ObjcAssociation association{};
    
    {
        // 加锁
        AssociationsManager manager;
        // 取得全局唯一的 AssociationsHashMap
        AssociationsHashMap &associations(manager.get());
        // 从全局的 AssociationsHashMap 中取得对象对应的 ObjectAssociationMap
        AssociationsHashMap::iterator i = associations.find((objc_object *)object);
        if (i != associations.end()) {
            // 如果存在
            ObjectAssociationMap &refs = i->second;
            // 从 ObjectAssocationMap 中取得 key 对应的 ObjcAssociation
            ObjectAssociationMap::iterator j = refs.find(key);
            if (j != refs.end()) {
                // 如果存在
                association = j->second;
                // 根据关联策略判断是否需要对 _value 执行 retain 操作
                association.retainReturnedValue();
            }
        }
        // 解锁
    }
    // 返回 _value 并根据关联策略判断是否需要放入自动释放池
    return association.autoreleaseReturnedValue();
}

void
_object_set_associative_reference(id object, const void *key, id value, uintptr_t policy)
{
    // This code used to work when nil was passed for object and key. Some code
    // probably relies on that to not crash. Check and handle it explicitly.
    // rdar://problem/44094390
    if (!object && !value) return; //对象和值都为nil,值可以为nil的

    //禁止使用关联对象
    if (object->getIsa()->forbidsAssociatedObjects())
        _objc_fatal("objc_setAssociatedObject called on instance (%p) of class %s which does not allow associated objects", object, object_getClassName(object));

    // 伪装 object 指针为 disguised
    DisguisedPtr<objc_object> disguised{(objc_object *)object};
    // 根据入参创建一个 association
    ObjcAssociation association{policy, value}; //关联对象实体 <策略,值>

    // retain the new value (if any) outside the lock.
    //在加锁之前根据关联策略判断是否 retain/copy 入参 value
    association.acquireValue();

    //局部作用域
    {
        ////初始化manager变量，相当于自动调用AssociationsManager的析构函数进行初始化
        ////并不是全局唯一，构造函数中加锁只是为了避免重复创建，在这里是可以初始化多个AssociationsManager变量
        AssociationsManager manager;
        // 取得全局的 AssociationsHashMap
        AssociationsHashMap &associations(manager.get());

        if (value) {
            // 这里 DenseMap 对我们而言是一个黑盒，这里只要看 try_emplace 函数
            // 在全局 AssociationsHashMap 中尝试插入 <DisguisedPtr<objc_object>, ObjectAssociationMap>
            // 返回值类型是 std::pair<iterator, bool>
            auto refs_result = associations.try_emplace(disguised, ObjectAssociationMap{});
            // 如果新插入成功
            if (refs_result.second) {
                /* it's the first association we make */
                // 第一次建立 association
                // 设置 uintptr_t has_assoc : 1; 位，标记该对象存在关联对象
                object->setHasAssociatedObjects();
            }

            /* establish or replace the association */
            // 重建或者替换 association
            auto &refs = refs_result.first->second;
            auto result = refs.try_emplace(key, std::move(association));
            if (!result.second) {
                // 替换
                // 如果之前有旧值的话把旧值的成员变量交换到 association
                // 然后在 函数执行结束时把旧值根据对应的策略判断执行 release
                association.swap(result.first->second);
            }
        } else {
            // value 为 nil 的情况，表示要把之前的关联对象置为 nil
            // 也可理解为移除指定的关联对象
            auto refs_it = associations.find(disguised);
            if (refs_it != associations.end()) {
                auto &refs = refs_it->second;
                auto it = refs.find(key);
                if (it != refs.end()) {
                    // 清除指定的关联对象
                    association.swap(it->second);
                    refs.erase(it);
                    if (refs.size() == 0) {
                        // 如果当前 object 的关联对象为空了，则同时从全局的 AssociationsHashMap中移除该对象
                        associations.erase(refs_it);

                    }
                }
            }
        }
        // 析构 mananger 临时变量
        // 这里还有一步连带操作
        // 在其析构函数中 AssociationsManagerLock.unlock() 解锁
    }

    // release the old value (outside of the lock).
    //// 开始时 retain 的是新入参的 value, 这里释放的是旧值，association 内部的 value 已经被替换了
    association.releaseHeldValue();
}

// Unlike setting/getting an associated reference,
// this function is performance sensitive because of
// raw isa objects (such as OS Objects) that can't track
// whether they have associated objects.
// 与 setting/getting 关联引用不同，此函数对性能敏感，
// 因为原始的 isa 对象（例如 OS 对象）无法跟踪它们是否具有关联的对象。
// 删除关联对象
void
_object_remove_assocations(id object)
{
    // 对象对应的 ObjectAssociationMap
    ObjectAssociationMap refs{};

    {// 加锁
        AssociationsManager manager;
        // 取得全局的 AssociationsHashMap
        AssociationsHashMap &associations(manager.get());
        // 取得对象的对应 ObjectAssociationMap，里面包含所有的 (key, ObjcAssociation)
        AssociationsHashMap::iterator i = associations.find((objc_object *)object);
        if (i != associations.end()) {
            // 把 i->second 的内容都转入 refs 对象中
            refs.swap(i->second);
            // 从全局 AssociationsHashMap 移除对象的 ObjectAssociationMap
            associations.erase(i);
        }
        // 解锁
    }

    // release everything (outside of the lock).
    // 遍历对象的 ObjectAssociationMap 中的 (key, ObjcAssociation)
    // 对 ObjcAssociation 的 _value 根据 _policy 进行释放
    for (auto &i: refs) {
        i.second.releaseHeldValue();
    }
}
