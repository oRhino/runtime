//
//  main.m
//  debug-objc
//
//  Created by Closure on 2018/12/4.
//

#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import "Student.h"


@interface TestTarget : NSObject

@end

@implementation TestTarget

+ (BOOL)resolveInstanceMethod:(SEL)sel{
    
    NSLog(@"%@",sel);
//    class_addMethod([self class], @selector(eat:), (IMP)eat, "v");
    return [super resolveInstanceMethod:sel];
}

@end

@interface NSObject (LG)

@end

@implementation NSObject (LG)

//- (void)hello{
//    NSLog(@"hello");
//}

- (id)forwardingTargetForSelector:(SEL)aSelector{
    
    return [TestTarget alloc];
}


- (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector{
    
    return  [NSMethodSignature signatureWithObjCTypes:"v@"];
}
+ (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector{
    
    return  [NSMethodSignature signatureWithObjCTypes:"v@"];
}

- (void)forwardInvocation:(NSInvocation *)anInvocation{
    
}

+ (void)forwardInvocation:(NSInvocation *)anInvocation{
    
}


@end

typedef void(^Block)(void);

@interface Person : NSObject

@property (nonatomic,copy)NSString *name;

@property(nonatomic, weak) Block doWork;
@end

@implementation Person


@end


int main(int argc, const char * argv[]) {
    @autoreleasepool {
        
//        NSLog(@"Hello, World! %@", [NSString class]);
        
//        Person *p = [[Person alloc]init];
//
//        {
//            p.doWork = ^{
//                NSLog(@"00000000");
//            };
//
//            NSLog(@"%@",p.doWork);
//        }
//
//        p.doWork();
//        p.name = @"余小菊";
////        [p performSelector:NSSelectorFromString(@"hello")];
//        [Person performSelector:NSSelectorFromString(@"hello")];
        
//        Person *p = [Person alloc];
//        NSLog(@"%p,%p,%@",p,&p,p);
//        id __weak obj = p;
        
        
//        void (^ __weak dowork)(void) = nil;
//        {
//            int a = 0;
//            void(^ doWork1)(void) = ^{
//                a;
//                NSLog(@"1111");
//            };
//            dowork = doWork1;
//        }
//        
//        NSLog(@"%@",dowork);
//        dowork();
        
        
        Student *s = [[Student alloc]init];
        NSLog(@"%@,%p",s,&s);//0x100754a00>,0x7ffeefbff508
        
        __weak id obj = s;
        NSLog(@"%@,%p",obj,&obj);

        
        [s eat];
        
    }
    return 0;
}
