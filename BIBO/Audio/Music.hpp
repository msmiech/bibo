#pragma once
#include <string>
#include <vector>

namespace bibo {
namespace audio {
/**
 * A note of a melody (times in beats)
 */
struct Note {
  float start = 0.0f;
  float length = 0.0f;
  int pitch = 0;  // MIDI note number (60: middle C)
};

/**
 * Reads a melody written as "name:length" per note, e.g. "A4:.5 C5:.5 F5:1 r:1
 * | Bb4:2 ...". The name is the pitch (C to B, # or b, octave) or r for a rest,
 * the length is in beats; | separates bars, which must have 4 beats.
 * @param start Beat of the first note
 * @param error The first error (an unknown note, a bar with a wrong length)
 * @return The notes (without the rests), empty if there is an error
 */
std::vector<Note> parseMelody(const std::string& text, float start,
                              std::string& error);
/**
 * @return The frequency of a MIDI note number in Hz (A4 = 69 = 440 Hz)
 */
float noteFrequency(float pitch);

/**
 * A piece of music as stereo samples (left, right, left, ...) between -1 and 1.
 * The end continues into the beginning, so it can be played in a loop without a
 * gap or a click.
 */
struct Song {
  int sampleRate = 0;
  float beatsPerMinute = 0.0f;
  int bars = 0;  // of 4 beats
  std::vector<float> samples;

  std::size_t frames() const { return samples.size() / 2; }
  float seconds() const {
    return sampleRate > 0
               ? static_cast<float>(frames()) / static_cast<float>(sampleRate)
               : 0.0f;
  }
};

/**
 * The music of the race (about a minute, played in a loop): a cheerful calypso
 * with steel drum, whistle, marimba, bass and percussion, and slapstick effects
 * (slide whistle, spring) at the ends of the phrases. It is synthesized by the
 * game (no sound files), the same every time.
 */
Song composeIslandTune(int sampleRate);
/**
 * The melodies of composeIslandTune (in the notation of parseMelody), 8 bars
 * each
 */
std::vector<std::string> islandTuneMelodies();
}  // namespace audio
}  // namespace bibo
