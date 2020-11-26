//
//  Student.m
//  debug-objc
//
//  Created by Rhino on 2020/11/25.
//

#import "Student.h"

@interface Student()

@property(nonatomic, copy) NSString *score;

@end


@implementation Student

+ (void)load
{
    NSLog(@"%s",__func__);
}

- (void)eat{
    
    NSLog(@"%s",__func__);
}


@end
