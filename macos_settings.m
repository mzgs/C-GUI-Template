#import <Cocoa/Cocoa.h>
#include <stdbool.h>

bool macos_settings_path(char *path, size_t capacity) {
    @autoreleasepool {
        NSURL *support = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                                               inDomains:NSUserDomainMask].firstObject;
        NSString *name = NSBundle.mainBundle.infoDictionary[@"CFBundleName"];
        if (!name.length) name = @APP_NAME;
        if (!support || !name.length) return false;
        NSURL *folder = [support URLByAppendingPathComponent:name isDirectory:YES];
        NSURL *file = [folder URLByAppendingPathComponent:@"settings.json" isDirectory:NO];
        return [file getFileSystemRepresentation:path maxLength:capacity];
    }
}

int macos_system_dark(void) {
    if (@available(macOS 10.14, *)) {
        @autoreleasepool {
            NSAppearance *appearance = NSApp ? [NSApp effectiveAppearance] : nil;
            if (appearance) {
                NSAppearanceName match = [appearance bestMatchFromAppearancesWithNames:@[
                    NSAppearanceNameAqua, NSAppearanceNameDarkAqua
                ]];
                return [match isEqualToString:NSAppearanceNameDarkAqua] ? 1 : 0;
            }
            NSString *style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
            return [style isEqualToString:@"Dark"] ? 1 : 0;
        }
    }
    return -1;
}
