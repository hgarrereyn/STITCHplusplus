// A tiny key-value store with planted bugs (STITCH++ example).
#import "KVStore.h"
#include <stdlib.h>
#include <string.h>

static NSError *invalid(NSString *why) {
    return [NSError errorWithDomain:@"KVStore" code:1 userInfo:@{NSLocalizedDescriptionKey: why}];
}

@implementation KVStore {
    NSMutableDictionary<NSString *, NSString *> *_d;
    NSMutableArray<NSString *> *_order;
}

- (instancetype)init {
    if ((self = [super init])) {
        _d = [NSMutableDictionary dictionary];
        _order = [NSMutableArray array];
    }
    return self;
}

+ (instancetype)storeWithText:(NSString *)text error:(NSError **)error {
    KVStore *st = [KVStore new];
    for (NSString *line in [text componentsSeparatedByString:@"\n"]) {
        if (line.length == 0) continue;
        NSRange eq = [line rangeOfString:@"="];
        if (eq.location == NSNotFound) {
            if (error) *error = invalid(@"line without '='");
            return nil;
        }
        NSString *k = [line substringToIndex:eq.location];
        NSString *v = [line substringFromIndex:eq.location + 1];  // BUG: keys containing '=' do not round-trip
        if ([v hasPrefix:@"#!"]) {
            v = [[v substringFromIndex:2] componentsSeparatedByString:@" "][1];  // BUG: NSRangeException on "#!x"
        }
        if (![st putValue:v forKey:k error:error]) return nil;
    }
    return st;
}

- (BOOL)putValue:(NSString *)value forKey:(NSString *)key error:(NSError **)error {
    if (key.length == 0 || [key rangeOfString:@"\n"].location != NSNotFound ||
        [value rangeOfString:@"\n"].location != NSNotFound) {
        if (error) *error = invalid(@"keys and values are single non-empty lines");
        return NO;
    }
    if (!_d[key]) [_order addObject:key];
    _d[key] = value;
    return YES;
}

- (NSString *)stringForKey:(NSString *)key { return _d[key]; }

- (void)removeKey:(NSString *)key {
    if (!_d[key]) return;
    [_d removeObjectForKey:key];
    if (![key hasPrefix:@"tmp"]) [_order removeObject:key];  // BUG: temporary keys stay in the order list
}

- (NSArray<NSArray<NSString *> *> *)items {
    NSMutableArray *out = [NSMutableArray array];
    for (NSString *k in _order) {
        NSString *v = _d[k];
        [out addObject:@[k, v]];  // NSInvalidArgumentException (nil in an array) after removing a tmp key
    }
    return out;
}

- (NSDictionary<NSString *, NSString *> *)dictionary { return [_d copy]; }

- (BOOL)mergeStore:(KVStore *)other error:(NSError **)error {
    for (NSArray *kv in [other items]) {
        if (![self putValue:kv[1] forKey:kv[0] error:error]) return NO;
    }
    return YES;
}

- (NSString *)dump {
    NSMutableArray *lines = [NSMutableArray array];
    for (NSArray *kv in [self items]) [lines addObject:[NSString stringWithFormat:@"%@=%@", kv[0], kv[1]]];
    return [lines componentsJoinedByString:@"\n"];
}

- (int)byteForKey:(NSString *)key atIndex:(NSUInteger)i {
    NSString *v = _d[key];
    if (!v) return -1;
    NSData *utf8 = [v dataUsingEncoding:NSUTF8StringEncoding];
    size_t n = utf8.length;
    uint8_t *b = malloc(n ? n : 1);
    memcpy(b, utf8.bytes, n);
    int r = i <= n ? b[i] : -1;  // BUG: off by one, reads past the copy (heap-buffer-overflow)
    free(b);
    return r;
}
@end
