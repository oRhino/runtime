//
//  Student+Learning.m
//  debug-objc
//
//  Created by Rhino on 2020/11/25.
//

#import "Student+Learning.h"

@implementation Student (Learning)

+ (void)load
{
    NSLog(@"%s",__func__);
}

- (void)learning{
    NSLog(@"%s",__func__);
}

- (void)eat{
    
    NSLog(@"category___%s",__func__);
}


@end

