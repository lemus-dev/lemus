#import "SDEngine.h"

#import <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>
#include <math.h>
#include <os/lock.h>
#include <stdatomic.h>

#define MAX_FRAMES 4096
#define MAX_EVENTS 2048
#define RING 1024
#define VOICES 16
#define MAX_FX 8

typedef struct {
	int offset;
	uint8_t st, d1, d2;
	double offAbs; // note-on: when its note-off is due; note-off: the due time it was made for
} Ev;

typedef struct {
	SDNote *notes;
	int count;
	double loop;
} Pattern;

typedef struct {
	AudioUnit unit;
	int channels;
	bool bypass;
} FxSlot;

typedef struct {
	int note, gate, active;
	double phase, inc;
	float env, vel, lp;
} Voice;

typedef struct {
	os_unfair_lock lock; // the main thread holds it only to swap pointers
	double sr;

	AudioUnit unit;
	int channels;
	bool effect, midi;
	Pattern *pattern;
	double bpm;
	bool playing, wantStart, wantStop, changed;
	double absBeat;    // beats since play started (note-offs are scheduled in it)
	double reportBeat; // the loop position the host callbacks report
	int64_t sampleTime;
	bool held[128];
	double heldOff[128];
	Voice voices[VOICES];
	float in[2][MAX_FRAMES], work[2][MAX_FRAMES];
	float *feed[2]; // what an effect's input callback reads: the synth, or the signal so far
	FxSlot chain[MAX_FX];
	int chainCount;

	os_unfair_lock midiLock; // producers: the main thread and CoreMIDI's
	uint32_t ring[RING];
	int ringHead, ringTail;

	_Atomic double uiBeat;
	_Atomic float peakL, peakR, loadPeak;
	_Atomic uint64_t loadNs, loadFrames;
	double ticksToNs;
} State;

static bool isOff(const Ev *e) { return (e->st & 0xF0) == 0x80; }

// a should be sent after b: by offset, and at one offset note-offs first (touching notes are
// separate; overlapping ones legato, a slide on a 303).
static bool after(const Ev *a, const Ev *b)
{
	if (a->offset != b->offset)
		return a->offset > b->offset;
	return !isOff(a) && isOff(b);
}

static void insertEv(Ev *ev, int *n, int from, Ev e)
{
	if (*n >= MAX_EVENTS)
		return;
	int i = *n;
	while (i > from && after(&ev[i - 1], &e)) {
		ev[i] = ev[i - 1];
		--i;
	}
	ev[i] = e;
	(*n)++;
}

// ---- the built-in synth (no plugin, or an effect's input) ----

static void synthEvent(State *s, uint8_t st, uint8_t d1, uint8_t d2)
{
	const int type = st & 0xF0;
	if (type == 0x90 && d2 > 0) {
		Voice *v = NULL;
		for (int i = 0; i < VOICES && !v; ++i)
			if (!s->voices[i].active)
				v = &s->voices[i];
		if (!v) {
			v = &s->voices[0];
			for (int i = 1; i < VOICES; ++i)
				if (s->voices[i].env < v->env)
					v = &s->voices[i];
		}
		v->note = d1;
		v->inc = 440.0 * pow(2.0, (d1 - 69) / 12.0) / s->sr;
		v->gate = 1;
		v->active = 1;
		v->vel = d2 / 127.0f;
	} else if (type == 0x80 || type == 0x90) {
		for (int i = 0; i < VOICES; ++i)
			if (s->voices[i].note == d1)
				s->voices[i].gate = 0;
	} else if (type == 0xB0 && (d1 == 120 || d1 == 123)) {
		for (int i = 0; i < VOICES; ++i)
			s->voices[i].gate = 0;
	}
}

static void synthRender(State *s, float *L, float *R, int from, int to)
{
	const float att = 1.0f - expf(-1.0f / (0.003f * (float)s->sr));
	const float rel = 1.0f - expf(-1.0f / (0.08f * (float)s->sr));
	for (int k = 0; k < VOICES; ++k) {
		Voice *v = &s->voices[k];
		if (!v->active)
			continue;
		for (int i = from; i < to; ++i) {
			v->env += ((v->gate ? 1.0f : 0.0f) - v->env) * (v->gate ? att : rel);
			v->phase += v->inc;
			if (v->phase >= 1.0)
				v->phase -= 1.0;
			v->lp += ((float)(2.0 * v->phase - 1.0) - v->lp) * 0.3f;
			const float x = v->lp * v->env * v->vel * 0.2f;
			L[i] += x;
			R[i] += x;
		}
		if (!v->gate && v->env < 1e-4f)
			v->active = 0;
	}
}

// ---- the host callbacks a plugin calls from inside AudioUnitRender ----

static OSStatus hcBeatAndTempo(void *ud, Float64 *beat, Float64 *tempo)
{
	State *s = ud;
	if (beat)
		*beat = s->reportBeat;
	if (tempo)
		*tempo = s->bpm;
	return noErr;
}

static OSStatus hcMusicalTime(void *ud, UInt32 *toNextBeat, Float32 *num, UInt32 *den, Float64 *downbeat)
{
	State *s = ud;
	const double b = s->reportBeat, spb = s->sr * 60.0 / s->bpm;
	if (toNextBeat)
		*toNextBeat = (UInt32)((ceil(b) - b) * spb);
	if (num)
		*num = 4;
	if (den)
		*den = 4;
	if (downbeat)
		*downbeat = floor(b / 4.0) * 4.0;
	return noErr;
}

static OSStatus hcTransport2(void *ud, Boolean *playing, Boolean *recording, Boolean *changed, Float64 *sample,
                             Boolean *cycling, Float64 *cycleStart, Float64 *cycleEnd)
{
	State *s = ud;
	if (playing)
		*playing = s->playing;
	if (recording)
		*recording = false;
	if (changed)
		*changed = s->changed;
	if (sample)
		*sample = s->reportBeat * s->sr * 60.0 / s->bpm;
	if (cycling)
		*cycling = s->pattern != NULL;
	if (cycleStart)
		*cycleStart = 0;
	if (cycleEnd)
		*cycleEnd = s->pattern ? s->pattern->loop : 0;
	return noErr;
}

static OSStatus hcTransport(void *ud, Boolean *playing, Boolean *changed, Float64 *sample, Boolean *cycling,
                            Float64 *cycleStart, Float64 *cycleEnd)
{
	return hcTransport2(ud, playing, NULL, changed, sample, cycling, cycleStart, cycleEnd);
}

// An effect's input: the synth's output for this block.
static OSStatus inputFeed(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus,
                          UInt32 frames, AudioBufferList *io)
{
	State *s = ref;
	for (UInt32 b = 0; b < io->mNumberBuffers; ++b) {
		float *src = s->feed[b < 2 ? b : 1];
		if (io->mBuffers[b].mData == NULL)
			io->mBuffers[b].mData = src;
		else
			memcpy(io->mBuffers[b].mData, src, frames * sizeof(float));
		io->mBuffers[b].mDataByteSize = frames * sizeof(float);
	}
	return noErr;
}

// ---- the output device's render callback ----

// Renders one plugin into work, then into L/R (a mono plugin to both). An effect's input
// callback reads s->feed meanwhile, so L/R may be its input.
static void renderUnit(State *s, AudioUnit unit, int channels, UInt32 frames, const AudioTimeStamp *ts, float *L, float *R)
{
	struct {
		AudioBufferList list;
		AudioBuffer second;
	} abl;
	abl.list.mNumberBuffers = (UInt32)channels;
	for (int c = 0; c < channels; ++c) {
		AudioBuffer *buf = c == 0 ? &abl.list.mBuffers[0] : &abl.second;
		buf->mNumberChannels = 1;
		buf->mDataByteSize = frames * sizeof(float);
		buf->mData = s->work[c];
	}
	AudioUnitRenderActionFlags f = 0;
	AudioTimeStamp t = {0};
	t.mSampleTime = (Float64)s->sampleTime;
	t.mHostTime = ts->mHostTime;
	t.mFlags = kAudioTimeStampSampleTimeValid | (ts->mFlags & kAudioTimeStampHostTimeValid);
	if (AudioUnitRender(unit, &f, &t, 0, frames, &abl.list) != noErr)
		return;
	memcpy(L, abl.list.mBuffers[0].mData, frames * sizeof(float));
	memcpy(R, channels > 1 ? abl.second.mData : abl.list.mBuffers[0].mData, frames * sizeof(float));
}

static void releaseHeld(State *s, Ev *ev, int *n)
{
	for (int p = 0; p < 128; ++p)
		if (s->held[p]) {
			s->held[p] = false;
			if (*n < MAX_EVENTS)
				ev[(*n)++] = (Ev){0, 0x80, (uint8_t)p, 0, 0};
		}
}

static OSStatus render(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus,
                       UInt32 frames, AudioBufferList *io)
{
	State *s = ref;
	float *L = io->mBuffers[0].mData;
	float *R = io->mNumberBuffers > 1 ? io->mBuffers[1].mData : NULL;
	memset(L, 0, frames * sizeof(float));
	if (R)
		memset(R, 0, frames * sizeof(float));
	if (frames > MAX_FRAMES)
		return noErr;

	os_unfair_lock_lock(&s->lock);
	static Ev ev[MAX_EVENTS]; // only this thread touches it
	int n = 0;

	// Live notes (the keyboard in the window, MIDI inputs) at the block's start.
	if (os_unfair_lock_trylock(&s->midiLock)) {
		while (s->ringTail != s->ringHead && n < 256) {
			const uint32_t m = s->ring[s->ringTail];
			s->ringTail = (s->ringTail + 1) % RING;
			ev[n++] = (Ev){0, (uint8_t)(m >> 16), (uint8_t)((m >> 8) & 0x7F), (uint8_t)(m & 0x7F), 0};
		}
		os_unfair_lock_unlock(&s->midiLock);
	}

	const double spb = s->sr * 60.0 / s->bpm;
	if (s->wantStop) {
		s->wantStop = false;
		releaseHeld(s, ev, &n);
		s->playing = false;
		s->absBeat = 0;
		s->changed = true;
	}
	if (s->wantStart) {
		s->wantStart = false;
		releaseHeld(s, ev, &n);
		s->playing = true;
		s->absBeat = 0;
		s->changed = true;
	}

	// The sequence's events in [a, b): its due note-offs, and every note-on in the loop.
	const int seqFrom = n;
	const Pattern *pat = s->pattern;
	const double a = s->absBeat, b = a + frames / spb;
	if (s->playing && pat && pat->loop > 0) {
		const double loop = pat->loop;
		for (int p = 0; p < 128; ++p)
			if (s->held[p] && s->heldOff[p] < b) {
				const int off = (int)fmax(0, (s->heldOff[p] - a) * spb);
				insertEv(ev, &n, seqFrom, (Ev){off < (int)frames ? off : (int)frames - 1, 0x80, (uint8_t)p, 0, s->heldOff[p]});
			}
		for (int i = 0; i < pat->count; ++i) {
			const SDNote *note = &pat->notes[i];
			if (note->at < 0 || note->at >= loop || note->note < 0 || note->note > 127)
				continue;
			for (double k = fmax(0, ceil((a - note->at) / loop - 1e-12));; k += 1) {
				const double t = k * loop + note->at;
				if (t >= b)
					break;
				if (t < a)
					continue;
				int off = (int)((t - a) * spb);
				if (off >= (int)frames)
					off = (int)frames - 1;
				const int vel = note->vel < 1 ? 1 : note->vel > 127 ? 127 : note->vel;
				insertEv(ev, &n, seqFrom, (Ev){off, 0x90, (uint8_t)note->note, (uint8_t)vel, t + fmax(note->len, 1.0 / 128)});
			}
		}
	}

	// Walk them in order: a note-on of a held pitch ends it first; a note-off only ends the note
	// it was scheduled for; a note that ends inside the block gets its note-off inserted.
	static Ev out[MAX_EVENTS];
	int m = 0;
	for (int i = 0; i < seqFrom; ++i)
		out[m++] = ev[i];
	for (int i = seqFrom; i < n && m < MAX_EVENTS - 1; ++i) {
		const Ev e = ev[i];
		const int p = e.d1;
		if (isOff(&e)) {
			if (s->held[p] && fabs(s->heldOff[p] - e.offAbs) < 1e-9) {
				s->held[p] = false;
				out[m++] = e;
			}
			continue;
		}
		if (s->held[p])
			out[m++] = (Ev){e.offset, 0x80, (uint8_t)p, 0, 0};
		out[m++] = e;
		s->held[p] = true;
		s->heldOff[p] = e.offAbs;
		if (e.offAbs < b) {
			int off = (int)((e.offAbs - a) * spb);
			if (off >= (int)frames)
				off = (int)frames - 1;
			insertEv(ev, &n, i + 1, (Ev){off, 0x80, (uint8_t)p, 0, e.offAbs});
		}
	}

	if (s->unit && s->midi)
		for (int i = 0; i < m; ++i)
			MusicDeviceMIDIEvent(s->unit, out[i].st, out[i].d1, out[i].d2, (UInt32)out[i].offset);

	const bool synth = s->unit == NULL || s->effect;
	if (synth) {
		float *sl = s->unit ? s->in[0] : L, *sr = s->unit ? s->in[1] : (R ? R : s->in[1]);
		if (s->unit) {
			memset(sl, 0, frames * sizeof(float));
			memset(sr, 0, frames * sizeof(float));
		}
		int pos = 0;
		for (int i = 0; i < m; ++i) {
			if (out[i].offset > pos) {
				synthRender(s, sl, sr, pos, out[i].offset);
				pos = out[i].offset;
			}
			synthEvent(s, out[i].st, out[i].d1, out[i].d2);
		}
		synthRender(s, sl, sr, pos, (int)frames);
	}

	s->reportBeat = s->playing && pat && pat->loop > 0 ? fmod(a, pat->loop) : 0;
	float *outR = R ? R : s->in[1];
	const uint64_t t0 = mach_absolute_time();
	bool rendered = false;
	if (s->unit) {
		s->feed[0] = s->in[0];
		s->feed[1] = s->in[1];
		renderUnit(s, s->unit, s->channels, frames, ts, L, outR);
		rendered = true;
	}
	// The effects, in order: each reads the signal so far and replaces it.
	for (int i = 0; i < s->chainCount; ++i) {
		if (s->chain[i].bypass || !s->chain[i].unit)
			continue;
		s->feed[0] = L;
		s->feed[1] = outR;
		renderUnit(s, s->chain[i].unit, s->chain[i].channels, frames, ts, L, outR);
		rendered = true;
	}
	if (rendered) {
		const double ns = (double)(mach_absolute_time() - t0) * s->ticksToNs;
		atomic_fetch_add(&s->loadNs, (uint64_t)ns);
		atomic_fetch_add(&s->loadFrames, frames);
		const float inst = (float)(ns / (frames / s->sr * 1e9));
		if (inst > atomic_load(&s->loadPeak))
			atomic_store(&s->loadPeak, inst);
	}
	s->changed = false;

	if (s->playing)
		s->absBeat = b;
	s->sampleTime += frames;
	atomic_store(&s->uiBeat, s->playing && pat && pat->loop > 0 ? fmod(s->absBeat, pat->loop) : -1.0);
	os_unfair_lock_unlock(&s->lock);

	float pl = 0, pr = 0;
	for (UInt32 i = 0; i < frames; ++i) {
		pl = fmaxf(pl, fabsf(L[i]));
		if (R)
			pr = fmaxf(pr, fabsf(R[i]));
	}
	if (pl > atomic_load(&s->peakL))
		atomic_store(&s->peakL, pl);
	if (pr > atomic_load(&s->peakR))
		atomic_store(&s->peakR, pr);
	return noErr;
}

static AudioStreamBasicDescription floatFormat(double sr, int channels)
{
	AudioStreamBasicDescription f = {0};
	f.mSampleRate = sr;
	f.mFormatID = kAudioFormatLinearPCM;
	f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
	f.mBytesPerPacket = 4;
	f.mFramesPerPacket = 1;
	f.mBytesPerFrame = 4;
	f.mChannelsPerFrame = (UInt32)channels;
	f.mBitsPerChannel = 32;
	return f;
}

@implementation SDEngine {
	State *_s;
	AudioUnit _output;
	MIDIClientRef _midiClient;
	MIDIPortRef _midiPort;
	NSInteger _midiSources;
}

- (instancetype)init
{
	if ((self = [super init])) {
		_s = calloc(1, sizeof(State));
		_s->lock = OS_UNFAIR_LOCK_INIT;
		_s->midiLock = OS_UNFAIR_LOCK_INIT;
		_s->bpm = 120;
		_s->sr = 48000;
		mach_timebase_info_data_t tb;
		mach_timebase_info(&tb);
		_s->ticksToNs = (double)tb.numer / tb.denom;
		atomic_store(&_s->uiBeat, -1.0);
	}
	return self;
}

- (double)sampleRate { return _s->sr; }

- (BOOL)start:(NSString **)error
{
	AudioComponentDescription d = {kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput, kAudioUnitManufacturer_Apple, 0, 0};
	AudioComponent c = AudioComponentFindNext(NULL, &d);
	if (!c || AudioComponentInstanceNew(c, &_output) != noErr) {
		if (error)
			*error = @"no default output";
		return NO;
	}
	AudioStreamBasicDescription dev = {0};
	UInt32 size = sizeof(dev);
	AudioUnitGetProperty(_output, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &dev, &size);
	if (dev.mSampleRate > 0)
		_s->sr = dev.mSampleRate;
	AudioStreamBasicDescription f = floatFormat(_s->sr, 2);
	AudioUnitSetProperty(_output, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof(f));
	UInt32 maxFrames = MAX_FRAMES;
	AudioUnitSetProperty(_output, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, sizeof(maxFrames));
	AURenderCallbackStruct cb = {render, _s};
	AudioUnitSetProperty(_output, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
	OSStatus e = AudioUnitInitialize(_output);
	if (e == noErr)
		e = AudioOutputUnitStart(_output);
	if (e != noErr) {
		if (error)
			*error = [NSString stringWithFormat:@"audio output failed (%d)", (int)e];
		return NO;
	}
	[self startMIDI];
	return YES;
}

- (UInt32)bufferFrames
{
	AudioDeviceID dev = 0;
	UInt32 size = sizeof(dev);
	if (AudioUnitGetProperty(_output, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev, &size) != noErr)
		return 0;
	UInt32 frames = 0;
	size = sizeof(frames);
	AudioObjectPropertyAddress addr = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
	AudioObjectGetPropertyData(dev, &addr, 0, NULL, &size, &frames);
	return frames;
}

- (void)startMIDI
{
	__weak SDEngine *weakSelf = self;
	if (MIDIClientCreateWithBlock(CFSTR("Simple DAW"), &_midiClient, ^(const MIDINotification *note) {
		    if (note->messageID == kMIDIMsgObjectAdded || note->messageID == kMIDIMsgSetupChanged)
			    dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf connectSources]; });
	    }) != noErr)
		return;
	State *s = _s;
	MIDIInputPortCreateWithProtocol(_midiClient, CFSTR("in"), kMIDIProtocol_1_0, &_midiPort,
	                                ^(const MIDIEventList *list, void *src) {
		                                const MIDIEventPacket *p = &list->packet[0];
		                                for (UInt32 i = 0; i < list->numPackets; ++i) {
			                                for (UInt32 w = 0; w < p->wordCount; ++w) {
				                                const uint32_t word = p->words[w];
				                                if ((word >> 28) != 2) // MIDI 1.0 channel voice
					                                continue;
				                                const uint8_t st = (word >> 16) & 0xFF;
				                                const uint8_t type = st & 0xF0;
				                                if (type != 0x80 && type != 0x90 && type != 0xB0 && type != 0xE0 && type != 0xD0 && type != 0xC0)
					                                continue;
				                                os_unfair_lock_lock(&s->midiLock);
				                                const int next = (s->ringHead + 1) % RING;
				                                if (next != s->ringTail) {
					                                s->ring[s->ringHead] = (uint32_t)st << 16 | (word & 0x7F00) | (word & 0x7F);
					                                s->ringHead = next;
				                                }
				                                os_unfair_lock_unlock(&s->midiLock);
			                                }
			                                p = MIDIEventPacketNext(p);
		                                }
	                                });
	[self connectSources];
}

- (void)connectSources
{
	if (!_midiPort)
		return;
	const ItemCount count = MIDIGetNumberOfSources();
	for (ItemCount i = 0; i < count; ++i)
		MIDIPortConnectSource(_midiPort, MIDIGetSource(i), NULL);
	_midiSources = (NSInteger)count;
}

- (NSInteger)midiSources { return _midiSources; }

- (int)prepareUnit:(AudioUnit)unit effect:(BOOL)effect error:(NSString **)error
{
	UInt32 maxFrames = MAX_FRAMES;
	AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, sizeof(maxFrames));
	int channels = 2;
	AudioStreamBasicDescription f = floatFormat(_s->sr, 2);
	if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof(f)) != noErr) {
		f = floatFormat(_s->sr, 1);
		OSStatus e = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof(f));
		if (e != noErr) {
			if (error)
				*error = [NSString stringWithFormat:@"the plugin takes neither stereo nor mono float output (%d)", (int)e];
			return 0;
		}
		channels = 1;
	}
	if (effect) {
		AudioStreamBasicDescription fi = floatFormat(_s->sr, 2);
		if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fi, sizeof(fi)) != noErr) {
			fi = floatFormat(_s->sr, 1);
			AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fi, sizeof(fi));
		}
		AURenderCallbackStruct cb = {inputFeed, _s};
		AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
	}
	HostCallbackInfo hci = {0};
	hci.hostUserData = _s;
	hci.beatAndTempoProc = hcBeatAndTempo;
	hci.musicalTimeLocationProc = hcMusicalTime;
	hci.transportStateProc = hcTransport;
	hci.transportStateProc2 = hcTransport2;
	AudioUnitSetProperty(unit, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &hci, sizeof(hci));
	const OSStatus e = AudioUnitInitialize(unit);
	if (e != noErr) {
		if (error)
			*error = [NSString stringWithFormat:@"AudioUnitInitialize failed (%d)", (int)e];
		return 0;
	}
	return channels;
}

- (void)setUnit:(AudioUnit)unit channels:(int)channels effect:(BOOL)effect midi:(BOOL)midi
{
	os_unfair_lock_lock(&_s->lock);
	_s->unit = unit;
	_s->channels = channels < 1 ? 1 : channels > 2 ? 2 : channels;
	_s->effect = effect;
	_s->midi = midi;
	memset(_s->held, 0, sizeof(_s->held));
	memset(_s->voices, 0, sizeof(_s->voices));
	_s->changed = true;
	os_unfair_lock_unlock(&_s->lock);
}

- (void)setEffects:(const AudioUnit _Nullable *)units channels:(const int *)channels bypass:(const BOOL *)bypass count:(NSInteger)count
{
	os_unfair_lock_lock(&_s->lock);
	_s->chainCount = (int)MIN(count, MAX_FX);
	for (int i = 0; i < _s->chainCount; ++i)
		_s->chain[i] = (FxSlot){units[i], channels[i] < 1 ? 1 : channels[i] > 2 ? 2 : channels[i], bypass[i] != NO};
	os_unfair_lock_unlock(&_s->lock);
}

- (void)setNotes:(const SDNote *)notes count:(NSInteger)count loopBeats:(double)beats
{
	Pattern *p = calloc(1, sizeof(Pattern));
	p->count = (int)count;
	p->loop = beats;
	if (count > 0) {
		p->notes = malloc(sizeof(SDNote) * (size_t)count);
		memcpy(p->notes, notes, sizeof(SDNote) * (size_t)count);
	}
	os_unfair_lock_lock(&_s->lock);
	Pattern *old = _s->pattern;
	_s->pattern = p;
	os_unfair_lock_unlock(&_s->lock);
	if (old) {
		free(old->notes);
		free(old);
	}
}

- (double)bpm { return _s->bpm; }

- (void)setBpm:(double)bpm
{
	os_unfair_lock_lock(&_s->lock);
	_s->bpm = bpm < 10 ? 10 : bpm > 999 ? 999 : bpm;
	os_unfair_lock_unlock(&_s->lock);
}

- (BOOL)playing { return _s->playing || _s->wantStart; }

- (void)setPlaying:(BOOL)playing
{
	os_unfair_lock_lock(&_s->lock);
	if (playing) {
		_s->wantStart = true;
		_s->wantStop = false;
	} else {
		_s->wantStop = true;
		_s->wantStart = false;
	}
	os_unfair_lock_unlock(&_s->lock);
}

- (double)positionBeats { return atomic_load(&_s->uiBeat); }

- (void)sendMIDI:(uint8_t)status data1:(uint8_t)d1 data2:(uint8_t)d2
{
	os_unfair_lock_lock(&_s->midiLock);
	const int next = (_s->ringHead + 1) % RING;
	if (next != _s->ringTail) {
		_s->ring[_s->ringHead] = (uint32_t)status << 16 | (uint32_t)(d1 & 0x7F) << 8 | (d2 & 0x7F);
		_s->ringHead = next;
	}
	os_unfair_lock_unlock(&_s->midiLock);
}

- (void)peaks:(float *)left right:(float *)right
{
	*left = atomic_exchange(&_s->peakL, 0.0f);
	*right = atomic_exchange(&_s->peakR, 0.0f);
}

- (void)load:(double *)average peak:(double *)peak
{
	const uint64_t ns = atomic_exchange(&_s->loadNs, 0), frames = atomic_exchange(&_s->loadFrames, 0);
	*average = frames ? (double)ns / ((double)frames / _s->sr * 1e9) : 0;
	*peak = atomic_exchange(&_s->loadPeak, 0.0f);
}

@end
