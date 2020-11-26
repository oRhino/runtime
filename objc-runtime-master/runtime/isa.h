/*
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 2018 Apple Inc.  All Rights Reserved.
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
/********************************************************************
 * 
 *  isa.h - Definitions of isa fields for C and assembly code.
 *
 ********************************************************************/

#ifndef _OBJC_ISA_H_
#define _OBJC_ISA_H_

#include "objc-config.h"


#if (!SUPPORT_NONPOINTER_ISA && !SUPPORT_PACKED_ISA && !SUPPORT_INDEXED_ISA) ||\
    ( SUPPORT_NONPOINTER_ISA &&  SUPPORT_PACKED_ISA && !SUPPORT_INDEXED_ISA) ||\
    ( SUPPORT_NONPOINTER_ISA && !SUPPORT_PACKED_ISA &&  SUPPORT_INDEXED_ISA)
    // good config
#else
#   error bad config
#endif

/*
标记是否是纯的ISA指针，还是非指针型的NOPOINTER_ISA指针indexed
标记是否有关联对象has_assoc
标记对象是否使用到的C++相关内容，在ARC环境下标记对象是否通过ARC来管理的has_cxx_dtor
标记当前对象的类对象的指针地址shiftcls
magic
标记对象是否有弱引用指针weakly_referenced
标记对象是否正在进行dealloc操作deallocating
标记是否有sitetable结构用于存储引用计数has_sidetable_rc
标记对象的引用计数(首先会存储在该字段中，当到达上限后，在存入对应的引用计数表中)extra_rc
*/


#if SUPPORT_PACKED_ISA

    // extra_rc must be the MSB-most field (so it matches carry/overflow flags)
    // nonpointer must be the LSB (fixme or get rid of it)
    // shiftcls must occupy the same bits that a real class pointer would
    // bits + RC_ONE is equivalent to extra_rc + 1
    // RC_HALF is the high bit of extra_rc (i.e. half of its range)

    // future expansion:
    // uintptr_t fast_rr : 1;     // no r/r overrides
    // uintptr_t lock : 2;        // lock for atomic property, @synch
    // uintptr_t extraBytes : 1;  // allocated with extra bytes

# if __arm64__
#   define ISA_MASK        0x0000000ffffffff8ULL
#   define ISA_MAGIC_MASK  0x000003f000000001ULL
#   define ISA_MAGIC_VALUE 0x000001a000000001ULL
#   define ISA_BITFIELD                                                      \
      uintptr_t nonpointer        : 1;                                       \
      uintptr_t has_assoc         : 1;                                       \
      uintptr_t has_cxx_dtor      : 1;                                       \
      uintptr_t shiftcls          : 33; /*MACH_VM_MAX_ADDRESS 0x1000000000*/ \
      uintptr_t magic             : 6;                                       \
      uintptr_t weakly_referenced : 1;                                       \
      uintptr_t deallocating      : 1;                                       \
      uintptr_t has_sidetable_rc  : 1;                                       \
      uintptr_t extra_rc          : 19
#   define RC_ONE   (1ULL<<45)
#   define RC_HALF  (1ULL<<18)

# elif __x86_64__
#   define ISA_MASK        0x00007ffffffffff8ULL
#   define ISA_MAGIC_MASK  0x001f800000000001ULL
#   define ISA_MAGIC_VALUE 0x001d800000000001ULL
#   define ISA_BITFIELD                                                        \
      uintptr_t nonpointer        : 1;                                         \
      uintptr_t has_assoc         : 1;                                         \
      uintptr_t has_cxx_dtor      : 1;                                         \
      uintptr_t shiftcls          : 44; /*MACH_VM_MAX_ADDRESS 0x7fffffe00000*/ \
      uintptr_t magic             : 6;                                         \
      uintptr_t weakly_referenced : 1;                                         \
      uintptr_t deallocating      : 1;                                         \
      uintptr_t has_sidetable_rc  : 1;                                         \
      uintptr_t extra_rc          : 8
#   define RC_ONE   (1ULL<<56)
#   define RC_HALF  (1ULL<<7)

# else
#   error unknown architecture for packed isa
# endif

// SUPPORT_PACKED_ISA
#endif


#if SUPPORT_INDEXED_ISA

# if  __ARM_ARCH_7K__ >= 2  ||  (__arm64__ && !__LP64__)
    // armv7k or arm64_32

#   define ISA_INDEX_IS_NPI_BIT  0
#   define ISA_INDEX_IS_NPI_MASK 0x00000001
#   define ISA_INDEX_MASK        0x0001FFFC
#   define ISA_INDEX_SHIFT       2
#   define ISA_INDEX_BITS        15
#   define ISA_INDEX_COUNT       (1 << ISA_INDEX_BITS)
#   define ISA_INDEX_MAGIC_MASK  0x001E0001
#   define ISA_INDEX_MAGIC_VALUE 0x001C0001
#   define ISA_BITFIELD                         \
      uintptr_t nonpointer        : 1;          \
      uintptr_t has_assoc         : 1;          \
      uintptr_t indexcls          : 15;         \
      uintptr_t magic             : 4;          \
      uintptr_t has_cxx_dtor      : 1;          \
      uintptr_t weakly_referenced : 1;          \
      uintptr_t deallocating      : 1;          \
      uintptr_t has_sidetable_rc  : 1;          \
      uintptr_t extra_rc          : 7
#   define RC_ONE   (1ULL<<25)
#   define RC_HALF  (1ULL<<6)

# else
#   error unknown architecture for indexed isa
# endif

// SUPPORT_INDEXED_ISA
#endif


// _OBJC_ISA_H_
#endif
