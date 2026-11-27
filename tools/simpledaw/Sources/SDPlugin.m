#import "SDPlugin.h"

#import <AudioToolbox/AUCocoaUIView.h>
#import <CoreAudioKit/CoreAudioKit.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

static OSType fourcc(id v)
{
	if ([v isKindOfClass:[NSNumber class]])
		return (OSType)[v unsignedIntValue];
	NSData *d = [[v description] dataUsingEncoding:NSMacOSRomanStringEncoding];
	if (d.length != 4)
		return 0;
	const uint8_t *b = d.bytes;
	return (OSType)b[0] << 24 | (OSType)b[1] << 16 | (OSType)b[2] << 8 | b[3];
}

static NSString *cachesDir(void)
{
	return [@"~/Library/Caches/Simple DAW/loads" stringByExpandingTildeInPath];
}

@interface SDPluginInfo ()
@property (readwrite) NSString *name, *displayName, *version, *bundlePath, *executablePath, *factory, *stamp;
@property (readwrite) OSType type, subtype, manufacturer;
@property (readwrite) BOOL isEffect, acceptsMIDI;
@property (readwrite) NSDate *built;
@end

@implementation SDPluginInfo

+ (NSArray<SDPluginInfo *> *)infosForBundle:(NSString *)path named:(NSString *)name
{
	NSMutableArray *out = [NSMutableArray array];
	NSDictionary *plist = [NSDictionary dictionaryWithContentsOfFile:[path stringByAppendingPathComponent:@"Contents/Info.plist"]];
	NSArray *components = plist[@"AudioComponents"];
	NSString *exec = plist[@"CFBundleExecutable"];
	if (![components isKindOfClass:[NSArray class]] || exec == nil)
		return out;
	NSString *execPath = [path stringByAppendingPathComponent:[@"Contents/MacOS" stringByAppendingPathComponent:exec]];
	struct stat st;
	if (stat(execPath.fileSystemRepresentation, &st) != 0)
		return out;
	NSString *base = name ?: path.lastPathComponent.stringByDeletingPathExtension;
	for (NSDictionary *ac in components) {
		if (![ac isKindOfClass:[NSDictionary class]] || ac[@"factoryFunction"] == nil)
			continue;
		const OSType type = fourcc(ac[@"type"]);
		if (type != kAudioUnitType_MusicDevice && type != kAudioUnitType_MusicEffect && type != kAudioUnitType_Effect)
			continue;
		SDPluginInfo *i = [SDPluginInfo new];
		i.name = components.count > 1 ? [NSString stringWithFormat:@"%@/%@", base, ac[@"subtype"]] : base;
		i.displayName = [ac[@"name"] description] ?: base;
		i.version = plist[@"CFBundleShortVersionString"] ?: @"";
		i.bundlePath = path;
		i.executablePath = execPath;
		i.factory = ac[@"factoryFunction"];
		i.type = type;
		i.subtype = fourcc(ac[@"subtype"]);
		i.manufacturer = fourcc(ac[@"manufacturer"]);
		i.isEffect = type != kAudioUnitType_MusicDevice;
		i.acceptsMIDI = type != kAudioUnitType_Effect;
		i.built = [NSDate dateWithTimeIntervalSince1970:st.st_mtimespec.tv_sec + st.st_mtimespec.tv_nsec * 1e-9];
		i.stamp = [NSString stringWithFormat:@"%ld.%09ld-%lld", (long)st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec, (long long)st.st_size];
		[out addObject:i];
	}
	return out;
}

+ (NSArray<SDPluginInfo *> *)scan
{
	NSMutableArray *out = [NSMutableArray array];
	NSMutableSet *seen = [NSMutableSet set];
	NSArray *dirs = @[[@"~/Library/Audio/Plug-Ins/Components" stringByExpandingTildeInPath], @"/Library/Audio/Plug-Ins/Components"];
	for (NSString *dir in dirs) {
		NSArray *names = [[[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil]
			sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
		for (NSString *n in names) {
			if (![n.pathExtension isEqualToString:@"component"])
				continue;
			for (SDPluginInfo *i in [self infosForBundle:[dir stringByAppendingPathComponent:n] named:nil])
				if (![seen containsObject:i.name]) {
					[seen addObject:i.name];
					[out addObject:i];
				}
		}
	}
	return out;
}

@end

@interface SDLoadedPlugin ()
@property (readwrite) SDPluginInfo *info;
@property (readwrite) AudioUnit unit;
@property (readwrite) int channels;
@property (readwrite) NSString *clonePath;
@end

@implementation SDLoadedPlugin

+ (void)removeStaleCopies
{
	NSFileManager *fm = [NSFileManager defaultManager];
	for (NSString *n in [fm contentsOfDirectoryAtPath:cachesDir() error:nil]) {
		// <name>-<pid>-<n>.component: keep a running instance's copies
		NSArray *parts = [n.stringByDeletingPathExtension componentsSeparatedByString:@"-"];
		const pid_t pid = parts.count >= 3 ? (pid_t)[parts[parts.count - 2] intValue] : 0;
		if (pid > 0 && pid != getpid() && kill(pid, 0) == 0)
			continue;
		[fm removeItemAtPath:[cachesDir() stringByAppendingPathComponent:n] error:nil];
	}
}

+ (instancetype)load:(SDPluginInfo *)info engine:(SDEngine *)engine error:(NSString **)error
{
	static UInt32 counter = 0;
	const UInt32 n = ++counter;
	NSFileManager *fm = [NSFileManager defaultManager];
	[fm createDirectoryAtPath:cachesDir() withIntermediateDirectories:YES attributes:nil error:nil];
	NSString *base = info.bundlePath.lastPathComponent.stringByDeletingPathExtension;
	NSString *copy = [cachesDir() stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%d-%u.component", base, getpid(), n]];
	NSError *e = nil;
	if (![fm copyItemAtPath:info.bundlePath toPath:copy error:&e]) {
		if (error)
			*error = [NSString stringWithFormat:@"copy failed: %@", e.localizedDescription];
		return nil;
	}
	NSString *exec = [copy stringByAppendingPathComponent:[@"Contents/MacOS" stringByAppendingPathComponent:info.executablePath.lastPathComponent]];
	void *handle = dlopen(exec.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		if (error)
			*error = [NSString stringWithFormat:@"dlopen: %s", dlerror()];
		return nil;
	}
	void *factory = dlsym(handle, info.factory.UTF8String);
	if (!factory) {
		if (error)
			*error = [NSString stringWithFormat:@"no %@ in the plugin", info.factory];
		return nil;
	}
	// Same type/subtype/manufacturer as the installed one (so saved states match), registered in
	// this process only; a higher version each time, so the newest copy is the one found.
	AudioComponentDescription desc = {info.type, info.subtype, info.manufacturer, 0, 0};
	AudioComponent comp = AudioComponentRegister(&desc, (__bridge CFStringRef)info.displayName, 0x7F000000u + n,
	                                             (AudioComponentFactoryFunction)factory);
	if (!comp) {
		if (error)
			*error = @"AudioComponentRegister failed";
		return nil;
	}
	AudioUnit unit = NULL;
	OSStatus err = AudioComponentInstanceNew(comp, &unit);
	if (err != noErr || !unit) {
		if (error)
			*error = [NSString stringWithFormat:@"AudioComponentInstanceNew failed (%d)", (int)err];
		return nil;
	}
	const int channels = [engine prepareUnit:unit effect:info.isEffect error:error];
	if (channels == 0) {
		AudioComponentInstanceDispose(unit);
		return nil;
	}
	SDLoadedPlugin *p = [SDLoadedPlugin new];
	p.info = info;
	p.unit = unit;
	p.channels = channels;
	p.clonePath = copy;
	return p;
}

- (NSView *)makeView
{
	UInt32 size = 0;
	Boolean writable = false;
	if (AudioUnitGetPropertyInfo(_unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &size, &writable) == noErr &&
	    size >= sizeof(AudioUnitCocoaViewInfo)) {
		AudioUnitCocoaViewInfo *vi = malloc(size);
		if (AudioUnitGetProperty(_unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, vi, &size) == noErr) {
			const UInt32 classes = (UInt32)((size - offsetof(AudioUnitCocoaViewInfo, mCocoaAUViewClass)) / sizeof(CFStringRef));
			NSURL *url = (__bridge_transfer NSURL *)vi->mCocoaAUViewBundleLocation;
			NSString *className = (__bridge_transfer NSString *)vi->mCocoaAUViewClass[0];
			for (UInt32 i = 1; i < classes; ++i)
				if (vi->mCocoaAUViewClass[i])
					CFRelease(vi->mCocoaAUViewClass[i]);
			free(vi);
			// JUCE makes its view class at run time, so the bundle it names may be ours.
			Class cls = [[NSBundle bundleWithURL:url] classNamed:className] ?: NSClassFromString(className);
			if (cls && [cls conformsToProtocol:@protocol(AUCocoaUIBase)]) {
				id<AUCocoaUIBase> factory = [cls new];
				NSView *v = [factory uiViewForAudioUnit:_unit withSize:NSZeroSize];
				if (v)
					return v;
			}
		} else
			free(vi);
	}
	AUGenericView *g = [[AUGenericView alloc] initWithAudioUnit:_unit displayFlags:AUViewTitleDisplayFlag | AUViewPropertiesDisplayFlag | AUViewParametersDisplayFlag];
	g.showsExpertParameters = YES;
	return g;
}

- (NSData *)state
{
	CFPropertyListRef plist = NULL;
	UInt32 size = sizeof(plist);
	if (AudioUnitGetProperty(_unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &size) != noErr || !plist)
		return nil;
	NSData *d = [NSPropertyListSerialization dataWithPropertyList:(__bridge id)plist format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
	CFRelease(plist);
	return d;
}

- (BOOL)restoreState:(NSData *)data
{
	id plist = [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:NULL error:nil];
	if (!plist)
		return NO;
	CFPropertyListRef ref = (__bridge CFPropertyListRef)plist;
	const OSStatus e = AudioUnitSetProperty(_unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &ref, sizeof(ref));
	AudioUnitParameter any = {_unit, kAUParameterListener_AnyParameter, kAudioUnitScope_Global, 0};
	AUParameterListenerNotify(NULL, NULL, &any);
	return e == noErr;
}

- (void)dispose
{
	if (_unit) {
		AudioUnitUninitialize(_unit);
		AudioComponentInstanceDispose(_unit);
		_unit = NULL;
	}
}

@end
