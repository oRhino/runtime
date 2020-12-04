/*
 * Copyright (c) 2011-2014 Apple Inc. All rights reserved.
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

#ifndef __OS_OBJECT__
#define __OS_OBJECT__

#ifdef __APPLE__
#include <Availability.h>
#include <os/availability.h>
#include <TargetConditionals.h>
#include <os/base.h>
#elif defined(_WIN32)
#include <os/generic_win_base.h>
#elif defined(__unix__)
#include <os/generic_unix_base.h>
#endif

/*!
 * @header
 *
 * @preprocinfo
 * By default, libSystem objects such as GCD and XPC objects are declared as
 * Objective-C types when building with an Objective-C compiler. This allows
 * them to participate in ARC, in RR management by the Blocks runtime and in
 * leaks checking by the static analyzer, and enables them to be added to Cocoa
 * collections.
 *
 * NOTE: this requires explicit cancellation of dispatch sources and xpc
 *       connections whose handler blocks capture the source/connection object,
 *       resp. ensuring that such captures do not form retain cycles (e.g. by
 *       declaring the source as __weak).
 *
 * To opt-out of this default behavior, add -DOS_OBJECT_USE_OBJC=0 to your
 * compiler flags.
 *
 * This mode requires a platform with the modern Objective-C runtime, the
 * Objective-C GC compiler option to be disabled, and at least a Mac OS X 10.8
 * or iOS 6.0 deployment target.
 *
 * 默认情况下，在使用 Objective-C 编译器进行构建时，libSystem 对象（例如 GCD 和 XPC 对象）被声明为 Objective-C 类型，这使他们可以参与 ARC，通过 Blocks 运行时参与 RR 管理以及通过静态分析器参与泄漏检查，并将它们添加到 Cocoa 集合中。
  注意：这需要显式取消 dispatch sources 和 xpc 连接来处理 blocks 捕获 source/connection 对象。 确保此类捕获不会形成循环引用（例如，通过将来源声明为 __weak）。
  要选择退出此默认行为，请将 DOS_OBJECT_USE_OBJC = 0 添加到的编译器标志中即可。
  此模式要求平台具有现代的 Objective-C runtime，要禁用的 Objective-C GC 编译器选项，以及至少 Mac OS X 10.8 或 iOS 6.0 的版本要求。

 */

//OS_OBJECT_HAVE_OBJC_SUPPORT 仅在 macOS 10.8（i386 则是 10.12）以上或者 iOS 6.0 值为 1， 其它情况为 0。
#ifndef OS_OBJECT_HAVE_OBJC_SUPPORT
#if !defined(__OBJC__) || defined(__OBJC_GC__)
#  define OS_OBJECT_HAVE_OBJC_SUPPORT 0
#elif !defined(TARGET_OS_MAC) || !TARGET_OS_MAC
#  define OS_OBJECT_HAVE_OBJC_SUPPORT 0
#elif TARGET_OS_IOS && __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_6_0
#  define OS_OBJECT_HAVE_OBJC_SUPPORT 0
#elif TARGET_OS_MAC && !TARGET_OS_IPHONE
#  if __MAC_OS_X_VERSION_MIN_REQUIRED < __MAC_10_8
#  define OS_OBJECT_HAVE_OBJC_SUPPORT 0
#  elif defined(__i386__) && __MAC_OS_X_VERSION_MIN_REQUIRED < __MAC_10_12
#  define OS_OBJECT_HAVE_OBJC_SUPPORT 0
#  else
#  define OS_OBJECT_HAVE_OBJC_SUPPORT 1
#  endif
#else
#  define OS_OBJECT_HAVE_OBJC_SUPPORT 1
#endif
#endif // OS_OBJECT_HAVE_OBJC_SUPPORT

//OS_OBJECT_USE_OBJC
// 在 OS_OBJECT_HAVE_OBJC_SUPPORT 为 1 的情况下，在 macOS/iOS __swift__ 情况下都是 1。
#if OS_OBJECT_HAVE_OBJC_SUPPORT
#if defined(__swift__) && __swift__ && !OS_OBJECT_USE_OBJC
#define OS_OBJECT_USE_OBJC 1
#endif
#ifndef OS_OBJECT_USE_OBJC
#define OS_OBJECT_USE_OBJC 1
#endif
#elif defined(OS_OBJECT_USE_OBJC) && OS_OBJECT_USE_OBJC
/* Unsupported platform for OS_OBJECT_USE_OBJC=1 */
#undef OS_OBJECT_USE_OBJC
#define OS_OBJECT_USE_OBJC 0
#else
#define OS_OBJECT_USE_OBJC 0
#endif

//OS_OBJECT_SWIFT3
// 在 __swift__ 宏存在时，OS_OBJECT_SWIFT3 都为 1。
#ifndef OS_OBJECT_SWIFT3
#ifdef __swift__
#define OS_OBJECT_SWIFT3 1
#else // __swift__
#define OS_OBJECT_SWIFT3 0
#endif // __swift__
#endif // OS_OBJECT_SWIFT3

//OS_OBJC_INDEPENDENT_CLASS
// OS_OBJECT_USE_OBJC 为 1 的情况下，存在 objc_independent_class 属性，则 OS_OBJC_INDEPENDENT_CLASS 是： __attribute__((objc_independent_class)) 否则只是一个空的宏定义。

#if OS_OBJECT_USE_OBJC
#import <objc/NSObject.h>
#if __has_attribute(objc_independent_class)
#define OS_OBJC_INDEPENDENT_CLASS __attribute__((objc_independent_class))
#endif // __has_attribute(objc_independent_class)
#ifndef OS_OBJC_INDEPENDENT_CLASS
#define OS_OBJC_INDEPENDENT_CLASS
#endif

//为 name 添加一个 OS_ 前缀，如 OS_OBJECT_CLASS(object) 宏展开是 OS_object。
#define OS_OBJECT_CLASS(name) OS_##name
// 用于协议声明，__VA_ARGS__ 是多参的宏展开时连续按序拼接各个参。
#define OS_OBJECT_DECL_PROTOCOL(name, ...) \
		@protocol OS_OBJECT_CLASS(name) __VA_ARGS__ \
		@end
//类声明并遵循指定的协议。
#define OS_OBJECT_CLASS_IMPLEMENTS_PROTOCOL_IMPL(name, proto) \
		@interface name () <proto> \
		@end
//给 name 和 proto 添加 OS_ 前缀。
#define OS_OBJECT_CLASS_IMPLEMENTS_PROTOCOL(name, proto) \
		OS_OBJECT_CLASS_IMPLEMENTS_PROTOCOL_IMPL( \
				OS_OBJECT_CLASS(name), OS_OBJECT_CLASS(proto))
//声明一个 OS_name 的协议，然后声明指向 NSObject 遵循 OS_name 协议的类型指针的别名 name_t。
#define OS_OBJECT_DECL_IMPL(name, ...) \
		OS_OBJECT_DECL_PROTOCOL(name, __VA_ARGS__) \
		typedef NSObject<OS_OBJECT_CLASS(name)> \
				* OS_OBJC_INDEPENDENT_CLASS name##_t
// 声明 OS_name 类型，name 后面的参表示其继承的父类，然后有一个 init 函数。
#define OS_OBJECT_DECL_BASE(name, ...) \
		@interface OS_OBJECT_CLASS(name) : __VA_ARGS__ \
		- (instancetype)init OS_SWIFT_UNAVAILABLE("Unavailable in Swift"); \
		@end
//先声明一个类 OS_name 然后声明一个指向该类指针的别名 name_t。
#define OS_OBJECT_DECL_IMPL_CLASS(name, ...) \
		OS_OBJECT_DECL_BASE(name, ## __VA_ARGS__) \
		typedef OS_OBJECT_CLASS(name) \
				* OS_OBJC_INDEPENDENT_CLASS name##_t
//继承自 <NSObject> 协议的 OS_name 协议。
#define OS_OBJECT_DECL(name, ...) \
		OS_OBJECT_DECL_IMPL(name, <NSObject>)
// 指定 OS_name 协议继承自 OS_super 协议。
#define OS_OBJECT_DECL_SUBCLASS(name, super) \
		OS_OBJECT_DECL_IMPL(name, <OS_OBJECT_CLASS(super)>)

// 如果存在 ns_returns_retained 属性，则 OS_OBJECT_RETURNS_RETAINED 宏定义为 __attribute__((__ns_returns_retained__))，否则仅是一个空的宏定义。
#if __has_attribute(ns_returns_retained)
#define OS_OBJECT_RETURNS_RETAINED __attribute__((__ns_returns_retained__))
#else
#define OS_OBJECT_RETURNS_RETAINED
#endif

//如果存在 ns_consumed 属性，则 OS_OBJECT_CONSUMED 宏定义为 __attribute__((__ns_consumed__))，否则仅是一个空的宏定义。
#if __has_attribute(ns_consumed)
#define OS_OBJECT_CONSUMED __attribute__((__ns_consumed__))
#else
#define OS_OBJECT_CONSUMED
#endif

//如果是 objc_arc 环境，则 OS_OBJECT_BRIDGE 宏定义为 __bridge，在 ARC 下对象类型转为 void * 时，需要加 __bridge，MRC 下则不需要。
#if __has_feature(objc_arc)
#define OS_OBJECT_BRIDGE __bridge
#define OS_WARN_RESULT_NEEDS_RELEASE
#else
#define OS_OBJECT_BRIDGE
#define OS_WARN_RESULT_NEEDS_RELEASE OS_WARN_RESULT
#endif
#if __has_attribute(objc_runtime_visible) && \
		((defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && \
		__MAC_OS_X_VERSION_MIN_REQUIRED < __MAC_10_12) || \
		(defined(__IPHONE_OS_VERSION_MIN_REQUIRED) && \
		!defined(__TV_OS_VERSION_MIN_REQUIRED) && \
		!defined(__WATCH_OS_VERSION_MIN_REQUIRED) && \
		__IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_10_0) || \
		(defined(__TV_OS_VERSION_MIN_REQUIRED) && \
		__TV_OS_VERSION_MIN_REQUIRED < __TVOS_10_0) || \
		(defined(__WATCH_OS_VERSION_MIN_REQUIRED) && \
		__WATCH_OS_VERSION_MIN_REQUIRED < __WATCHOS_3_0))
/*
 * To provide backward deployment of ObjC objects in Swift on pre-10.12
 * SDKs, OS_object classes can be marked as OS_OBJECT_OBJC_RUNTIME_VISIBLE.
 * When compiling with a deployment target earlier than OS X 10.12 (iOS 10.0, 
 * tvOS 10.0, watchOS 3.0) the Swift compiler will only refer to this type at
 * runtime (using the ObjC runtime).
 */
#define OS_OBJECT_OBJC_RUNTIME_VISIBLE __attribute__((objc_runtime_visible))
#else
#define OS_OBJECT_OBJC_RUNTIME_VISIBLE
#endif
#ifndef OS_OBJECT_USE_OBJC_RETAIN_RELEASE
#if defined(__clang_analyzer__)
#define OS_OBJECT_USE_OBJC_RETAIN_RELEASE 1
#elif __has_feature(objc_arc) && !OS_OBJECT_SWIFT3
#define OS_OBJECT_USE_OBJC_RETAIN_RELEASE 1
#else
#define OS_OBJECT_USE_OBJC_RETAIN_RELEASE 0
#endif
#endif
#if OS_OBJECT_SWIFT3
#define OS_OBJECT_DECL_SWIFT(name) \
		OS_EXPORT OS_OBJECT_OBJC_RUNTIME_VISIBLE \
		OS_OBJECT_DECL_IMPL_CLASS(name, NSObject)
#define OS_OBJECT_DECL_SUBCLASS_SWIFT(name, super) \
		OS_EXPORT OS_OBJECT_OBJC_RUNTIME_VISIBLE \
		OS_OBJECT_DECL_IMPL_CLASS(name, OS_OBJECT_CLASS(super))
OS_EXPORT OS_OBJECT_OBJC_RUNTIME_VISIBLE
OS_OBJECT_DECL_BASE(object, NSObject);
#endif // OS_OBJECT_SWIFT3
#else
/*! @parseOnly */
#define OS_OBJECT_RETURNS_RETAINED
/*! @parseOnly */
#define OS_OBJECT_CONSUMED
/*! @parseOnly */
#define OS_OBJECT_BRIDGE
/*! @parseOnly */
#define OS_WARN_RESULT_NEEDS_RELEASE OS_WARN_RESULT
/*! @parseOnly */
#define OS_OBJECT_OBJC_RUNTIME_VISIBLE
#define OS_OBJECT_USE_OBJC_RETAIN_RELEASE 0
#endif

#if OS_OBJECT_SWIFT3
#define OS_OBJECT_DECL_CLASS(name) \
		OS_OBJECT_DECL_SUBCLASS_SWIFT(name, object)
#elif OS_OBJECT_USE_OBJC
#define OS_OBJECT_DECL_CLASS(name) \
		OS_OBJECT_DECL(name)
#else
#define OS_OBJECT_DECL_CLASS(name) \
		typedef struct name##_s *name##_t
#endif

#define OS_OBJECT_GLOBAL_OBJECT(type, object) ((OS_OBJECT_BRIDGE type)&(object))

__BEGIN_DECLS

/*!
 * @function os_retain
 *
 * @abstract
 * Increment the reference count of an os_object.
 *
 * @discussion
 * On a platform with the modern Objective-C runtime this is exactly equivalent
 * to sending the object the -[retain] message.
 *
 * @param object
 * The object to retain.
 *
 * @result
 * The retained object.
 */
API_AVAILABLE(macos(10.10), ios(8.0))
OS_EXPORT OS_SWIFT_UNAVAILABLE("Can't be used with ARC")
void*
os_retain(void *object);
#if OS_OBJECT_USE_OBJC
#undef os_retain
#define os_retain(object) [object retain]
#endif

/*!
 * @function os_release
 *
 * @abstract
 * Decrement the reference count of a os_object.
 *
 * @discussion
 * On a platform with the modern Objective-C runtime this is exactly equivalent
 * to sending the object the -[release] message.
 *
 * @param object
 * The object to release.
 */
API_AVAILABLE(macos(10.10), ios(8.0))
OS_EXPORT
void OS_SWIFT_UNAVAILABLE("Can't be used with ARC")
os_release(void *object);
#if OS_OBJECT_USE_OBJC
#undef os_release
#define os_release(object) [object release]
#endif

__END_DECLS

#endif
