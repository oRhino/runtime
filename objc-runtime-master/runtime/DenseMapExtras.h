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

#ifndef DENSEMAPEXTRAS_H
#define DENSEMAPEXTRAS_H

#include "llvm-DenseMap.h"
#include "llvm-DenseSet.h"

namespace objc {

// We cannot use a C++ static initializer to initialize certain globals because
// libc calls us before our C++ initializers run. We also don't want a global
// pointer to some globals because of the extra indirection.
//
// ExplicitInit / LazyInit wrap doing it the hard way.
//我们不能使用 C++ static initializer 去初始化某些全局变量，因为 libc 在 C++ static initializer 调用之前会调用我们。由于额外的间接性，我们也不需要全局指针指向某些全局变量。 ExplicitInit / LazyInit wrap 很难做到。

template <typename Type>
class ExplicitInit {
    // typedef unsigned char uint8_t;
    // alignas(Type) 表示 _storage 内存对齐同 Type，
    // _storage 是长度为 sizeof(Type) 的 unsigned char 类型数组
    alignas(Type) uint8_t _storage[sizeof(Type)];

public:
    // c++11 新增加了变长模板，Ts 是 T 的复数形式，
    // 如果我们要避免这种转换呢？
    // 我们需要一种方法能按照参数原来的类型转发到另一个函数中，这才完美，我们称之为完美转发。
    // std::forward 就可以保存参数的左值或右值特性。
    
    // 初始化
    template <typename... Ts>
    void init(Ts &&... Args) {
        new (_storage) Type(std::forward<Ts>(Args)...);
    }

    Type &get() {
        // 把 _storage 数组起始地址强制转化为 Type *
        return *reinterpret_cast<Type *>(_storage);
    }
};

template <typename Type>
class LazyInit {
    alignas(Type) uint8_t _storage[sizeof(Type)];
    bool _didInit;

public:
    template <typename... Ts>
    Type *get(bool allowCreate, Ts &&... Args) {
        if (!_didInit) {
            if (!allowCreate) {
                return nullptr;
            }
            new (_storage) Type(std::forward<Ts>(Args)...);
            _didInit = true;
        }
        return reinterpret_cast<Type *>(_storage);
    }
};

// Convenience class for Dense Maps & Sets
template <typename Key, typename Value>
class ExplicitInitDenseMap : public ExplicitInit<DenseMap<Key, Value>> { };

template <typename Key, typename Value>
class LazyInitDenseMap : public LazyInit<DenseMap<Key, Value>> { };

template <typename Value>
class ExplicitInitDenseSet : public ExplicitInit<DenseSet<Value>> { };

template <typename Value>
class LazyInitDenseSet : public LazyInit<DenseSet<Value>> { };

} // namespace objc

#endif /* DENSEMAPEXTRAS_H */
