# Runtime781版本
源码学习

- 类的本质
- isa指向
- NONPOINTER
- TAGPOINTER 小对象
- 对象的内存布局(isa,成员变量...),类的布局(isa,superclass,cache_t,bits(rw,rw_e,ro))
- cache_t结构
- rw与rw_e/ro wwdc2020的优化
- 属性(property_t)
- 方法(method_t) /wwdc2020优化 small与big  12字节/24字节
- 协议
- 分类/分类的加载原理
- _objc_init_ 程序启动流程
- 引用计数(Sidetable/weak_table_t)
- autoreleasePool自动释放池
- dealloc
- retain,strong,release,weak...
- self,super
- iskindof,ismemberof
- alloc/init
- class/object_getclass
- 消息发送机制objc_msgsend()
- 消息转发机制(动态决议,快速转发,标准转发)---如何防止unrecognized selector崩溃

- TO DO .....

## 窥探本质的方法

1. 符号断点 Symbolic breakpoint  / control + step in
2. 查看汇编 debug -> debug workflow -> always show disassembly
3. 阅读源码,调试  源码地址: https://opensource.apple.com/tarballs/objc4/

runtime 会打包成动态库 libobjc.A.dylib

## 内存地址与指针地址

Person *p = [Person alloc];
Person *p1 = [p init];
Person *p2 = [p1 init];
Person *p3 = [Person new];

NSLog(@"%@ - %p - %p",p,p,&p);
NSLog(@"%@ - %p - %p",p1,p1,&p1);
NSLog(@"%@ - %p - %p",p2,p2,&p2);
NSLog(@"%@ - %p - %p",p3,p3,&p3);

真机执行结果:
<Person: 0x281606630> - 0x281606630 - 0x16da11bd8
<Person: 0x281606630> - 0x281606630 - 0x16da11bd0
<Person: 0x281606630> - 0x281606630 - 0x16da11bc8
<Person: 0x281606660> - 0x281606660 - 0x16da11bc0

1.p,p1,p2三个指针指向的是同一个对象
2.%@或者%p打印的就是指针变量指向的内容
3.&p打印的就是指针变量的地址,局部变量 在栈区<从高向低>
4.对象是在堆区 0x281606630->0x281606660 <从低到高>

## 编译器优化
#define fastpath(x) (__builtin_expect(bool(x), 1)) 
#define slowpath(x) (__builtin_expect(bool(x), 0))
fastpath(x),x很可能为真， fastpath 可以简称为 真值判断
slowpath(x),x很可能为假，slowpath 可以简称为 假值判断

__builtin_expect指令由gcc引入

1、目的<性能优化>：编译器可以对代码进行优化，减少指令跳转.
2、作用：允许程序员将最有可能执行的分支告诉编译器.
3、指令的写法为：__builtin_expect(EXP, N)。表示 EXP==N的概率很大。
4、fastpath定义中__builtin_expect((x),1)表示 x 的值为真的可能性更大；即 执行if 里面语句的机会更大
5、slowpath定义中的__builtin_expect((x),0)表示 x 的值为假的可能性更大。即执行 else 里面语句的机会更大
6、日常的开发中，也可以通过设置来优化编译器，达到性能优化的目的，设置的路径为：
Build Setting --> Optimization Level --> Debug --> 将None 改为 fastest 或者 smallest


## alloc方法
+alloc
_objc_rootAlloc
callAlloc
_objc_rootAllocWithZone
_class_createInstanceFromZone

根据成员变量计算所需开辟的内存大小.
调用Calloc开辟内存
关联isa关系(nonpointer做不同处理)

## init方法
什么事情也没有做,直接返回自身.
工厂方法设计<交给子类去重写>

## new
等同于 [[alloc] init]


## 关联对象
1. category能否添加属性,为什么?能否添加实例变量,为什么?
2. 关联对象的本质?
3. 关联对象需要手动释放吗?
4. 关联对象什么情况下会导致内存泄漏?

文件:objc-references.mm

category作为runtime运行时特性,是可以添加属性,但是只会生成setter/getter方法,不会添加实例变量.因为对象的内存布局在编译期间就已经确定,如果可以添加实例变量,那么已经生成的对象或者子类对象就要销毁重建,不然无法使用.
但是我们可以通过添加关联属性的方式来为对象添加实例变量.
```
//设置关联值 传入nil表示删除
void
objc_setAssociatedObject(id _Nonnull object, const void * _Nonnull key,
                         id _Nullable value, objc_AssociationPolicy policy)

//获取关联值
id _Nullable
objc_getAssociatedObject(id _Nonnull object, const void * _Nonnull key)       

//删除一个对象的所有关联值
void
objc_removeAssociatedObjects(id _Nonnull object)


```

AssociationsManager管理一个全局唯一的AssociationsHashMap,AssociationsHashMap是一张哈希表,包含所有对象的关联对象表,key为对象的地址<DisguisedPtr对对象进行包装>,value为ObjectAssociationMap,即一个对象的所有关联对象的一张表.ObjectAssociationMap 可以根据创建关联对象的key作为键,去查找对应的关联对象实体ObjcAssociation,ObjcAssociation就包含我们关联属性的内存策略和存储的值.
```
/// 一个对象的表 属性名 :关联对象实体
///key 是 const void * value 是 ObjcAssociation 的哈希表
typedef DenseMap<const void *, ObjcAssociation> ObjectAssociationMap;

/// 对象的地址:对象的所有关联对象表
//key 是 DisguisedPtr<objc_object> value 是 ObjectAssociationMap 的哈希表
//DisguisedPtr<objc_object> 可理解为把 objc_object 地址伪装为一个整数。
typedef DenseMap<DisguisedPtr<objc_object>, ObjectAssociationMap> AssociationsHashMap;
```


关联对象的API内部会根据传入的内存策略对关联对象的内存进行管理,比如设置值时,如果是retain,会释放旧值,retain新值;在对象销毁的时候,会根据对象是否有关联对象标志位(nonpointer才有,其它为true),查找对象对应的关联属性表,进行释放.无需手动管理.
所以如果我们传入的内存策略和引用关系造成循环引用,就会导致内存泄漏.

```
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
```

## AutoreleasePool自动释放池
文件 :NSObject.mm/NSObject-internal.h

1.AutoreleasePool的实现机制是什么?它是什么时候释放内部对象的?
它内部的数据结构是什么样的?哨兵对象的作用是什么.为什么要设计它?
2.那些对象会放入到AutoreleasePool中?
3.autorelease对象在什么时机会被调用release?
4.方法里有局部对象， 出了方法后会立即释放吗?
5.自动释放池的应用?
6.自动释放池和runloop的关系?

自动释放池是一个以栈为结点通过双向链表的形式结合而成的,对应的类是AutoreleasePoolPage,AutoreleasePoolPage的成员变量有线程(一一对应),next指针(表示栈当中下一个可以填充的位置),parent父指针,child子指针(用于连接前一个AutoreleasePoolPage和下一个AutoreleasePoolPage),depth(链表的深度)等.
当iOS程序启动的时候,即将进入runloop(监听kCFRunLoopEntry事件)时,会创建自动释放池,自动释放池的内存包含自身的变量之外会以栈的形式管理添加进来的autorelease对象,栈里面的每个指针要么是等待 autorelease 的对象，要么是 POOL_BOUNDARY<哨兵> 自动释放池边界（实际为 #define POOL_BOUNDARY nil),调用autorelease就相当于把这个对象添加到page中,哨兵对象用于标识此次pop时,需要发送release对象的边界位置,当一个page容量满时,会新创建一个page,然后通过parent,child连接两个page.
每创建一个autoreleasepool 就相当于添加一个哨兵对象,所以autoreleasepool是可以嵌套的.
那么管理的对象什么时候释放呢? 如果是@autoreleasepool {}包起来的,起始会调用autoreleasepoolpush,}结束就会调用autoreleasepoolpop,还有一种情况,它会监听kCFRunLoopBeforeWaiting(准备进入休眠)事件,调用autoreleasepoolpop释放一次,然后autoreleasepoolpush.


那些对象会放入到AutoreleasePool中?
ARC下:
1. __autoreleasing修饰的
```
__autoreleasing id obj = [NSObject new];
```
2. 不是alloc、new、copy、mutable Copy开头的方法创建对象的

```
for (int i = 0; i < 200; ++i) {
    NSString *str = [NSString stringWithFormat:@"Test:%d", i];
    //这里注意不能是tagpointer对象<栈区>
}
```
## ARC/MRC

ARC: Automatic Reference Counting 自动引用计数
MRC:（MannulReference Counting）手动引用计数

### 引用计数原则:

生成并持有对象    alloc/new/copy/mutableCopy等    +1
持有对象    retain    +1
释放对象    release    -1
废弃对象    dealloc    -

### 本质:编译器 + runtime共同协作的结果

### 工作原理: 编译器分析源码中每个对象的生命周期,基于对象的生命周期,添加相应的引用计数操作代码

### ARC优化:
1. ARC优化器会移除多余的retain和release语句,减少重复调用<合并对称的引用计数操作,比如将+1/-1/+1/-1直接置为0>
2. 巧妙的跳过某些情况下 autorelease机制的调用.
 (当方法全部基于ARC实现时,在方法return的时候,ARC会调用objc_autoreleaseReturnValue 以替代MRC下的autorelease.在MRC下需要retian的位置,ARC会调用objc_retainAutoreleasedReturnValue())
 
```
 + (instancetype)createZoo{
    return [self new];
 }
 
 People *people = [People createZoo];
 
 //结果
 + (instancetype)createZoo{
    id tmp = [self new];
    return objc_autoreleaseReturnValue(tmp);
 }
 
 id tmp =  objc_retainAutoreleasedReturnValue([People createZoo]);
 People *people = tmp;
 objc_storeStrong(&people,nil);
 
```
在调用 objc_autoreleaseReturnValue() 时，会在栈上查询 return address 以确定 return value 是否会被直接传给 objc_retainAutoreleasedReturnValue()。 如果没传，说明返回值不能直接从提供方发送给接收方，这时就会调用 autorelease。反之，如果返回值能顺利的从提供方传送给接收方，那么就会直接跳过 autorelease 过程，并且修改 return address 以跳过 objc_retainAutoreleasedReturnValue()过程，这样就跳过了整个 autorelease 和 retain的过程。

即当返回值被返回之后，紧接着就需要被 retain 的时候，没有必要进行 autorelease + retain，直接什么都不要做就好了。
