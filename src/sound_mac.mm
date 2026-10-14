// sound_mac.mm - sound on macOS (for platformPlaySound in main_glfw.cpp): a WAV file in memory,
// played with AVFoundation's AVAudioPlayer. Built only on macOS (see mac.mk).
#import <AVFoundation/AVFoundation.h>

#include <cstddef>

void macPlaySound(const unsigned char* wav, size_t bytes) {
    if (!wav || bytes == 0) return;
    @autoreleasepool {
        // Each sound's player is kept until it has finished (looked for here, the next time a
        // sound starts, rather than being told: no callbacks).
        static NSMutableArray* playing = nil;
        if (!playing) playing = [[NSMutableArray alloc] init];
        NSMutableArray* finished = [NSMutableArray array];
        for (AVAudioPlayer* player in playing)
            if (!player.playing) [finished addObject:player];
        [playing removeObjectsInArray:finished];

        NSData* data = [NSData dataWithBytes:wav length:bytes];
        NSError* error = nil;
        AVAudioPlayer* player = [[AVAudioPlayer alloc] initWithData:data error:&error];
        if (!player) return;
        [player play];
        [playing addObject:player];
#if !__has_feature(objc_arc)
        [player release];
#endif
    }
}
