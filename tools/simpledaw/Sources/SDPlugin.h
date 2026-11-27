// Audio Unit (v2) plugins: found by reading the Components folders' Info.plists (nothing is
// loaded to list them), and loaded from a fresh copy of the bundle each time, registered in this
// process only under a version higher than the last. So a rebuilt plugin loads as new code with
// no restart: the old copy's code stays mapped (dyld can't unload it) but is never used again.
#pragma once

#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioToolbox.h>

#import "SDEngine.h"

NS_ASSUME_NONNULL_BEGIN

@interface SDPluginInfo : NSObject
@property (readonly) NSString *name;        // the bundle's name ("ro-mono-303"), or its path when loaded by path
@property (readonly) NSString *displayName; // the component's ("Lemus: ro-mono-303")
@property (readonly) NSString *version;     // CFBundleShortVersionString
@property (readonly) NSString *bundlePath;
@property (readonly) NSString *executablePath;
@property (readonly) NSString *factory;
@property (readonly) OSType type, subtype, manufacturer;
@property (readonly) BOOL isEffect;    // takes audio in (aufx, aumf)
@property (readonly) BOOL acceptsMIDI; // aumu, aumf
@property (readonly) NSDate *built;    // the executable's modification time
@property (readonly) NSString *stamp;  // changes whenever the executable does

+ (NSArray<SDPluginInfo *> *)scan; // ~/Library and /Library Components, the user's first
+ (NSArray<SDPluginInfo *> *)infosForBundle:(NSString *)path named:(nullable NSString *)name;
@end

@interface SDLoadedPlugin : NSObject
@property (readonly) SDPluginInfo *info;
@property (readonly) AudioUnit unit;
@property (readonly) int channels;
@property (readonly) NSString *clonePath;

+ (nullable instancetype)load:(SDPluginInfo *)info engine:(SDEngine *)engine error:(NSString *_Nullable *_Nullable)error;
+ (void)removeStaleCopies;

- (nullable NSView *)makeView; // the plugin's own Cocoa view, else Apple's generic one
- (nullable NSData *)state;    // kAudioUnitProperty_ClassInfo as a binary plist
- (BOOL)restoreState:(NSData *)data;
- (void)dispose;               // after the engine let go of it and its view is gone
@end

NS_ASSUME_NONNULL_END
