//
//  Student.h
//  debug-objc
//
//  Created by Rhino on 2020/11/25.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface Student : NSObject

@property(nonatomic, copy) NSString *name;
@property(nonatomic, copy) NSString *sex;


- (void)eat;


@end

NS_ASSUME_NONNULL_END
