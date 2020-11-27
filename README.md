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
