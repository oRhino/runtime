# Runtime781
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
