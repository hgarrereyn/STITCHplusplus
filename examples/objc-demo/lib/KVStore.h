// A tiny key-value store with planted bugs (STITCH++ example).
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface KVStore : NSObject
/// Lines of `key=value`; nil (and an error) for a line without '='.
+ (nullable instancetype)storeWithText:(NSString *)text error:(NSError **)error;
/// Keys and values are single non-empty lines.
- (BOOL)putValue:(NSString *)value forKey:(NSString *)key error:(NSError **)error;
- (nullable NSString *)stringForKey:(NSString *)key;
- (void)removeKey:(NSString *)key;
/// [key, value] pairs in insertion order.
- (NSArray<NSArray<NSString *> *> *)items;
- (NSDictionary<NSString *, NSString *> *)dictionary;
/// Puts every entry of other into this store; NO (and an error) for an invalid entry.
- (BOOL)mergeStore:(KVStore *)other error:(NSError **)error;
- (NSString *)dump;
/// Byte i of the UTF-8 value of key, or -1.
- (int)byteForKey:(NSString *)key atIndex:(NSUInteger)i;
@end

NS_ASSUME_NONNULL_END
