// sound.h - JuggleSim's one sound: a short, bright "plip", like a water droplet, played when
// something the user just did was refused or broke the pattern (so they notice). It's made here
// rather than read from a file, so there's nothing to ship with the program.
#pragma once

#include <vector>

// The plip as a complete WAV file in memory (16-bit mono PCM), made the first time it's asked for.
const std::vector<unsigned char>& plipWav();
