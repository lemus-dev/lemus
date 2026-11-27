// The audio side: the default output device, one plugin (instrument or effect) and the looped
// sequence played into it sample-accurately, with tempo and transport for plugins that sync to
// the host. MIDI from every connected input goes to the plugin too. With no plugin, or with an
// effect, a small built-in saw synth plays the notes (into the effect's input).
#pragma once

#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>

NS_ASSUME_NONNULL_BEGIN

typedef struct {
	double at, len; // beats
	int note, vel;  // MIDI
} SDNote;

@interface SDEngine : NSObject

@property (readonly) double sampleRate;
@property (readonly) UInt32 bufferFrames;

- (BOOL)start:(NSString *_Nullable *_Nullable)error;

// Formats, the maximum block, the host callbacks (tempo, transport), the input feed for effects,
// then AudioUnitInitialize. Returns the output channel count (1 or 2) or 0 on failure.
- (int)prepareUnit:(AudioUnit)unit effect:(BOOL)effect error:(NSString *_Nullable *_Nullable)error;

// Swaps the plugin the audio thread plays (NULL: none). Returns once the audio thread has let go
// of the old one, which the caller then disposes.
- (void)setUnit:(nullable AudioUnit)unit channels:(int)channels effect:(BOOL)effect midi:(BOOL)midi;

// The effects after the plugin, in order (at most 8). Returns once the audio thread uses the
// new chain, so units no longer in it can be disposed.
- (void)setEffects:(const AudioUnit _Nullable *_Nullable)units channels:(const int *_Nullable)channels bypass:(const BOOL *_Nullable)bypass count:(NSInteger)count;

- (void)setNotes:(const SDNote *_Nullable)notes count:(NSInteger)count loopBeats:(double)beats;

@property (nonatomic) double bpm;
@property (nonatomic) BOOL playing; // starting always starts at the loop's beginning

- (double)positionBeats; // in the loop, or -1 when stopped
- (void)sendMIDI:(uint8_t)status data1:(uint8_t)d1 data2:(uint8_t)d2;
- (void)peaks:(float *)left right:(float *)right; // since the last call
- (void)load:(double *)average peak:(double *)peak; // the plugin's render time / real time, since the last call
@property (readonly) NSInteger midiSources;

@end

NS_ASSUME_NONNULL_END
