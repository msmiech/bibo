#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <string>

#include "Audio/Music.hpp"
#include "Audio/SoundEffects.hpp"

using bibo::audio::ComposeIslandTune;
using bibo::audio::IslandTuneMelodies;
using bibo::audio::kSoundEffectCount;
using bibo::audio::NoteFrequency;
using bibo::audio::ParseMelody;
using bibo::audio::SoundEffect;

using bibo::audio::Note;
using bibo::audio::Song;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("Melodies are read note by note with their bars", "[audio]") {
  std::string error;
  std::vector<Note> notes =
      ParseMelody("A4:.5 C5:.5 F#5:1 r:1 Bb3:1 | E5:4", 8.0f, error);
  CAPTURE(error);
  REQUIRE(notes.size() == 5);
  CHECK(notes[0].pitch == 69);
  CHECK(notes[0].start == 8.0f);
  CHECK(notes[1].pitch == 72);
  CHECK(notes[1].start == 8.5f);
  CHECK(notes[2].pitch == 78);
  CHECK(notes[3].pitch == 58);  // after the rest
  CHECK(notes[3].start == 11.0f);
  CHECK(notes[4].start == 12.0f);
  CHECK(notes[4].length == 4.0f);

  SECTION("A bar must have 4 beats") {
    CHECK(ParseMelody("C5:1 D5:1 E5:1 | F5:4", 0.0f, error).empty());
    CHECK(error.find("bar 1") != std::string::npos);
    CHECK(ParseMelody("C5:4 | F5:2", 0.0f, error).empty());  // the last bar too
  }
  SECTION("Unknown notes") {
    CHECK(ParseMelody("H5:4", 0.0f, error).empty());
    CHECK(ParseMelody("C5 D5:3", 0.0f, error).empty());
    CHECK(ParseMelody("C:4", 0.0f, error).empty());
  }
}

TEST_CASE("Notes have the frequencies of equal temperament", "[audio]") {
  CHECK_THAT(NoteFrequency(69.0f), WithinRel(440.0f, 1e-5f));
  CHECK_THAT(NoteFrequency(81.0f), WithinRel(880.0f, 1e-5f));
  CHECK_THAT(NoteFrequency(60.0f), WithinRel(261.626f, 1e-4f));
}

TEST_CASE("The melodies of the music are 8 bars each", "[audio]") {
  for (const std::string& text : IslandTuneMelodies()) {
    std::string error;
    std::vector<Note> notes = ParseMelody(text, 0.0f, error);
    CAPTURE(text, error);
    REQUIRE_FALSE(notes.empty());
    const Note& last = notes.back();
    CHECK(last.start + last.length <= 32.0f);
    CHECK(std::count(text.begin(), text.end(), '|') == 7);
  }
}

TEST_CASE(
    "The music is a loop of 32 bars that is loud, but not too loud, and has no "
    "click where it starts again",
    "[audio]") {
  const int sample_rate = 8000;  // enough for the test, the game uses 44100
  Song song = ComposeIslandTune(sample_rate);
  REQUIRE(song.bars == 32);
  REQUIRE(song.samples.size() == 2 * song.Frames());
  CHECK_THAT(song.Seconds(),
             WithinAbs(32.0f * 4.0f * 60.0f / song.beats_per_minute, 1e-3f));

  float peak = 0.0f, sum = 0.0f, largest_step = 0.0f;
  for (std::size_t i = 0; i < song.samples.size(); ++i) {
    float sample = song.samples[i];
    REQUIRE(std::isfinite(sample));
    peak = std::max(peak, std::abs(sample));
    sum += sample * sample;
    if (i >= 2) {
      largest_step =
          std::max(largest_step, std::abs(sample - song.samples[i - 2]));
    }
  }
  float rms = std::sqrt(sum / static_cast<float>(song.samples.size()));
  CAPTURE(peak, rms);
  CHECK(peak <= 0.9f);
  CHECK(peak > 0.8f);
  CHECK(rms > 0.08f);  // music, not silence
  // From the end to the beginning it goes on like from one sample to the next
  for (std::size_t channel = 0; channel < 2; ++channel) {
    float wrap = std::abs(song.samples[channel] -
                          song.samples[song.samples.size() - 2 + channel]);
    CAPTURE(channel, wrap, largest_step);
    CHECK(wrap <= largest_step);
  }

  SECTION("It is the same every time") {
    Song again = ComposeIslandTune(sample_rate);
    CHECK(again.samples == song.samples);
  }
}

TEST_CASE(
    "The sound effects are synthesized: short, not silent, without clipping",
    "[audio]") {
  const int sample_rate = 22050;
  for (int effect = 0; effect < kSoundEffectCount; ++effect) {
    REQUIRE(SoundEffectVariants(static_cast<SoundEffect>(effect)) >= 1);
    for (int variant = 0;
         variant < SoundEffectVariants(static_cast<SoundEffect>(effect));
         ++variant) {
      CAPTURE(effect, variant);
      std::vector<float> samples = SynthesizeSoundEffect(
          static_cast<SoundEffect>(effect), variant, sample_rate);
      REQUIRE_FALSE(samples.empty());
      CHECK(samples.size() < static_cast<std::size_t>(3 * sample_rate));
      float peak = 0.0f;
      for (float sample : samples) {
        REQUIRE(std::isfinite(sample));
        peak = std::max(peak, std::abs(sample));
      }
      CHECK(peak > 0.2f);
      CHECK(peak <= 1.0f);
      // Starts and ends quietly (no click)
      CHECK(std::abs(samples.front()) < 0.05f);
      CHECK(std::abs(samples.back()) < 0.05f);
    }
  }
}

TEST_CASE(
    "The beaver makes short sounds that differ from each other, the same every "
    "time",
    "[audio]") {
  const int sample_rate = 22050;
  REQUIRE(SoundEffectVariants(SoundEffect::kBeaver) > 3);
  std::vector<float> first =
      SynthesizeSoundEffect(SoundEffect::kBeaver, 0, sample_rate);
  CHECK(first == SynthesizeSoundEffect(SoundEffect::kBeaver, 0, sample_rate));
  CHECK(first != SynthesizeSoundEffect(SoundEffect::kBeaver, 1, sample_rate));
  // Shorter than the time between two sounds while the beaver talks, so they do
  // not pile up
  for (int variant = 0; variant < SoundEffectVariants(SoundEffect::kBeaver);
       ++variant) {
    CHECK(SynthesizeSoundEffect(SoundEffect::kBeaver, variant, sample_rate)
              .size() < static_cast<std::size_t>(0.21f * sample_rate));
  }
}
