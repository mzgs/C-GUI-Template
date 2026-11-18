#import <Foundation/Foundation.h>
#include <math.h>
#include <string.h>
#include "settings.h"

static NSURL *settings_url(const char *path) {
    if (path) return [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    NSURL *support = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                                           inDomains:NSUserDomainMask].firstObject;
    NSString *name = NSBundle.mainBundle.infoDictionary[@"CFBundleName"];
    if (!support || !name.length) return nil;
    NSURL *folder = [support URLByAppendingPathComponent:name isDirectory:YES];
    return [folder URLByAppendingPathComponent:@"settings.json" isDirectory:NO];
}

static double json_number(id value) {
    if (![value isKindOfClass:[NSNumber class]] ||
        CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID()) return NAN;
    return [value doubleValue];
}

void app_settings_load(app_settings *settings, const char *path) {
    @autoreleasepool {
        NSURL *url = settings_url(path);
        if (!url) return;
        NSError *error = nil;
        NSData *data = [NSData dataWithContentsOfURL:url options:0 error:&error];
        if (!data) {
            if (error.code != NSFileReadNoSuchFileError)
                NSLog(@"Could not read settings at %@: %@", url.path, error.localizedDescription);
            return;
        }
        id document = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
        if (![document isKindOfClass:[NSDictionary class]]) {
            NSLog(@"Invalid settings JSON at %@", url.path);
            return;
        }

        id value = document[@"example_string"];
        if ([value isKindOfClass:[NSString class]]) {
            const char *string = [value UTF8String];
            if (string && strlen(string) < sizeof settings->example_string &&
                strlen(string) == [value lengthOfBytesUsingEncoding:NSUTF8StringEncoding])
                strcpy(settings->example_string, string);
        }
        double number = json_number(document[@"example_number"]);
        if (number >= EXAMPLE_NUMBER_MIN && number <= EXAMPLE_NUMBER_MAX)
            settings->example_number = number;

        const int mod_mask = CUI_MOD_SHIFT | CUI_MOD_CTRL | CUI_MOD_ALT | CUI_MOD_SUPER;
        for (size_t i = 0; i < sizeof settings->keybindings / sizeof settings->keybindings[0]; ++i) {
            NSString *name = @(settings->keybindings[i].name);
            double key = json_number(document[[name stringByAppendingString:@"_key"]]);
            double mods = json_number(document[[name stringByAppendingString:@"_mods"]]);
            if (key >= CUI_KEY_NONE && key < CUI_KEY_COUNT && key == floor(key) &&
                mods >= 0 && mods <= mod_mask && mods == floor(mods) &&
                (key != CUI_KEY_NONE || mods == 0))
                settings->keybindings[i].combo = (cui_key_combo){ (int)mods, (cui_key)key };
        }
    }
}

bool app_settings_save(const app_settings *settings, const char *path) {
    @autoreleasepool {
        NSURL *url = settings_url(path);
        NSString *string = @(settings->example_string);
        if (!url || !string) return false;

        NSMutableDictionary *document = [@{
            @"example_string": string,
            @"example_number": @(settings->example_number),
        } mutableCopy];
        for (size_t i = 0; i < sizeof settings->keybindings / sizeof settings->keybindings[0]; ++i) {
            NSString *name = @(settings->keybindings[i].name);
            cui_key_combo combo = settings->keybindings[i].combo;
            document[[name stringByAppendingString:@"_key"]] = @(combo.key);
            document[[name stringByAppendingString:@"_mods"]] = @(combo.mods);
        }
        NSError *error = nil;
        NSData *data = [NSJSONSerialization dataWithJSONObject:document options:NSJSONWritingPrettyPrinted error:&error];
        if (data &&
            [[NSFileManager defaultManager] createDirectoryAtURL:url.URLByDeletingLastPathComponent
                                      withIntermediateDirectories:YES attributes:nil error:&error] &&
            [data writeToURL:url options:NSDataWritingAtomic error:&error]) return true;

        NSLog(@"Could not save settings at %@: %@", url.path, error.localizedDescription);
        return false;
    }
}
