// sound.h - JuggleSim's sounds: a short, bright "plip", like a water droplet, played when
// something the user just did was refused or broke the pattern (so they notice); and the
// metronome's tick. They're made here rather than read from files, so there's nothing to ship
// with the program.
#pragma once

#include <vector>

// The plip as a complete WAV file in memory (16-bit mono PCM), made the first time it's asked for.
const std::vector<unsigned char>& plipWav();

// The metronome's tick (a short wooden click), or with `accent` the one on a count's first beat
// (the same click, a little louder), as complete WAV files.
const std::vector<unsigned char>& tickWav(bool accent);
