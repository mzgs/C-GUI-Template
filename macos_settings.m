#import <Foundation/Foundation.h>
#include <stdbool.h>

bool macos_settings_path(char *path, size_t capacity) {
    @autoreleasepool {
        NSURL *support = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                                               inDomains:NSUserDomainMask].firstObject;
        NSString *name = NSBundle.mainBundle.infoDictionary[@"CFBundleName"];
        if (!support || !name.length) return false;
        NSURL *folder = [support URLByAppendingPathComponent:name isDirectory:YES];
        NSURL *file = [folder URLByAppendingPathComponent:@"settings.json" isDirectory:NO];
        return [file getFileSystemRepresentation:path maxLength:capacity];
    }
}
