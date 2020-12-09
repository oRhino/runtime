## GCD与NSOperationQueue

Parse

GCD
- 基于C语言高效,灵活
- 处理简单的操作或者是较系统级别的比如:监视进程或者监视文件夹内文件的变动


NSOperationQueue
- 控制并发数量
- 提供暂停,恢复,取消队列任务的接口,支持KVO
- 任务与任务之间的依赖


Dispatch Source 
它是BSD系内核惯有功能kqueue的包装。kqueue 是在 XNU 内核中发生各种事件时，在应用程序编程方执行处理的技术。其 CPU 负荷非常小，尽量不占用资源。kqueue 可以说是应用程序处理 XNU 内核中发生的各种事件的方法中最优秀的一种。

名称    内容
DISPATCH_SOURCE_TYPE_DATA_ADD    变量增加
DISPATCH_SOURCE_TYPE_DATA_OR    变量OR
DISPATCH_SOURCE_TYPE_MACH_SEND    MACH端口发送
DISPATCH_SOURCE_TYPE_MACH_RECV    MACH端口接收
DISPATCH_SOURCE_TYPE_PROC    监测到与进程相关的事件
DISPATCH_SOURCE_TYPE_READ    可读取文件映像
DISPATCH_SOURCE_TYPE_SIGNAL    接收信号
DISPATCH_SOURCE_TYPE_TIMER    定时器
DISPATCH_SOURCE_TYPE_VNODE    文件系统有变更
DISPATCH_SOURCE_TYPE_WRITE    可写入文件映像


// 指定DISPATCH_SOURCE_TYPE_DATA_ADD，做成Dispatch Source(分派源)。
// 设定Main Dispatch Queue 为追加处理的Dispatch Queue

   _processingQueueSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_DATA_ADD, 0, 0,
                                                   dispatch_get_main_queue());
   __block NSUInteger totalComplete = 0;
   dispatch_source_set_event_handler(_processingQueueSource, ^{       //当处理事件被最终执行时，计算后的数据可以通过dispatch_source_get_data来获取。这个数据的值在每次响应事件执行后会被重置，所以totalComplete的值是最终累积的值。
       NSUInteger value = dispatch_source_get_data(_processingQueueSource);
       totalComplete += value;      
       NSLog(@"进度：%@", @((CGFloat)totalComplete/100));     
       NSLog(@"线程号：%@", [NSThread currentThread]);
   });   //分派源创建时默认处于暂停状态，在分派源分派处理程序之前必须先恢复。
   [self resume];   
   
   //2.
   //恢复源后，就可以通过dispatch_source_merge_data向Dispatch Source(分派源)发送事件:
   dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);  
   dispatch_async(queue, ^{      
      for (NSUInteger index = 0; index < 100; index++) {
           dispatch_source_merge_data(_processingQueueSource, 1);       
           NSLog(@"线程号：%@", [NSThread currentThread]);
           usleep(20000);//0.02秒
       }
   });

- (void)resume {  
    if (self.running) {       return; }   
    NSLog(@"恢复Dispatch Source(分派源)");   self.running = YES;
    dispatch_resume(_processingQueueSource);
}

- (void)pause {  
  if (!self.running) {   return; }   
   NSLog(@"暂停Dispatch Source(分派源)");   
   self.running = NO;
   dispatch_suspend(_processingQueueSource);
}

