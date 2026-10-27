#include "Music.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <sstream>

namespace bibo::audio {

namespace {
const float kTau = 6.28318531f;
const float kBeatsPerMinute = 136.0f;
const int kBeatsPerBar = 4;

float Smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

/**
 * Fades out after the note is released (hold: how long the key is held,
 * seconds)
 */
float Release(float t, float hold, float time) {
  return t <= hold ? 1.0f : std::exp(-(t - hold) / time);
}

/**
 * White noise for the percussion, the same every time
 */
class Noise {
 public:
  explicit Noise(std::uint32_t seed)
      : state_(seed * 2654435761u + 0x9E3779B9u) {}
  float Next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return static_cast<float>(state_ >> 8) * (1.0f / 8388608.0f) - 1.0f;
  }

 private:
  std::uint32_t state_;
};

/**
 * Noise without its low frequencies (cymbals, shakers)
 */
class HighNoise {
 public:
  HighNoise(std::uint32_t seed, float cutoff)
      : noise_(seed), smoothing_(cutoff) {}
  float Next() {
    float x = noise_.Next();
    low_ += (x - low_) * smoothing_;
    return x - low_;
  }

 private:
  Noise noise_;
  float smoothing_;
  float low_ = 0.0f;
};

struct Bus {
  std::vector<float> left, right;
};

/**
 * Adds the sounds into the stereo buses of the song. A sound that goes on past
 * the end of the song continues at its beginning.
 */
class Mixer {
 public:
  Mixer(int sample_rate, std::size_t frames)
      : sample_rate(sample_rate), frames(frames) {
    for (Bus* bus : {&dry, &echo}) {
      bus->left.assign(frames, 0.0f);
      bus->right.assign(frames, 0.0f);
    }
  }

  /**
   * @param voice Returns the sound at a time after its start (seconds), gets
   * the time step too
   * @param pan -1 left, 0 middle, 1 right
   * @param echo_send The sound also goes into the echo
   */
  template <typename Voice>
  void Play(float start, float duration, float gain, float pan, bool echo_send,
            Voice voice) {
    std::size_t first = static_cast<std::size_t>(std::lround(
                            start * static_cast<float>(sample_rate))) %
                        frames;
    std::size_t count = std::min(
        static_cast<std::size_t>(duration * static_cast<float>(sample_rate)),
        frames);
    float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f *
                  std::numbers::pi_v<float>;
    float left_gain = std::cos(angle) * gain,
          right_gain = std::sin(angle) * gain;
    float step = 1.0f / static_cast<float>(sample_rate);
    for (std::size_t i = 0; i < count; ++i) {
      float value = voice(static_cast<float>(i) * step, step);
      std::size_t frame = (first + i) % frames;
      dry.left[frame] += value * left_gain;
      dry.right[frame] += value * right_gain;
      if (echo_send) {
        echo.left[frame] += value * left_gain;
        echo.right[frame] += value * right_gain;
      }
    }
  }

  /**
   * Ping-pong echo of the echo bus added to the dry bus. The delay line runs
   * through the loop twice, so the echoes of the end of the song are also heard
   * at its beginning.
   */
  void ApplyEcho(float delay_seconds, float feedback, float wet_gain) {
    std::size_t delay = std::max<std::size_t>(
        1, static_cast<std::size_t>(delay_seconds *
                                    static_cast<float>(sample_rate)));
    std::vector<float> wet_left(frames, 0.0f), wet_right(frames, 0.0f);
    for (std::size_t i = 0; i < 2 * frames; ++i) {
      std::size_t frame = i % frames;
      std::size_t before = (frame + frames - delay % frames) % frames;
      wet_left[frame] = 0.5f * (echo.left[before] + echo.right[before]) +
                        feedback * wet_right[before];
      wet_right[frame] = feedback * wet_left[before];
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
      dry.left[frame] += wet_gain * wet_left[frame];
      dry.right[frame] += wet_gain * wet_right[frame];
    }
  }

  const int sample_rate;
  const std::size_t frames;
  Bus dry, echo;
};

// Instruments: functions of the time since the start of the note (and the time
// step for those that glide)

/**
 * Steel drum: bright partials that die away quickly, the octave is strong
 */
auto SteelDrum(float frequency, float hold) {
  return [=](float t, float) {
    float attack = 1.0f - std::exp(-t / 0.003f);
    float value =
        std::sin(kTau * frequency * t) * std::exp(-2.0f * t) +
        0.5f * std::sin(kTau * 2.0f * frequency * t + 0.5f) *
            std::exp(-3.2f * t) +
        0.18f * std::sin(kTau * 3.01f * frequency * t) * std::exp(-5.0f * t) +
        0.06f * std::sin(kTau * 4.18f * frequency * t) * std::exp(-9.0f * t);
    return attack * value * Release(t, hold, 0.07f);
  };
}

/**
 * Whistling: a pure tone with a little breath and a vibrato that sets in on
 * long notes
 */
auto Whistle(float frequency, float hold, std::uint32_t seed) {
  return [=, phase = 0.0f, noise = Noise(seed)](float t, float dt) mutable {
    float vibrato =
        1.0f + 0.007f * std::sin(kTau * 5.6f * t) * Smooth((t - 0.1f) / 0.25f);
    phase += frequency * vibrato * dt;
    phase -= std::floor(phase);
    float envelope = std::min(1.0f, t / 0.035f) * Release(t, hold, 0.06f);
    return envelope *
           (std::sin(kTau * phase) + 0.12f * std::sin(2.0f * kTau * phase) +
            0.025f * noise.Next());
  };
}

/**
 * Marimba: a soft wooden tone, the bar's overtones (about 4 and 10 times the
 * frequency) fade first
 */
auto Marimba(float frequency) {
  return [=](float t, float) {
    float attack = std::min(1.0f, t / 0.0015f);
    return attack * (std::sin(kTau * frequency * t) * std::exp(-5.0f * t) +
                     0.35f * std::sin(kTau * 3.93f * frequency * t) *
                         std::exp(-14.0f * t) +
                     0.12f * std::sin(kTau * 9.87f * frequency * t) *
                         std::exp(-35.0f * t));
  };
}

/**
 * Plucked bass
 */
auto Bass(float frequency, float hold) {
  return [=](float t, float) {
    float attack = std::min(1.0f, t / 0.004f);
    float value =
        std::sin(kTau * frequency * t) +
        0.45f * std::sin(kTau * 2.0f * frequency * t) * std::exp(-5.0f * t) +
        0.2f * std::sin(kTau * 3.0f * frequency * t) * std::exp(-9.0f * t);
    return attack * value * std::exp(-1.8f * t) * Release(t, hold, 0.05f);
  };
}

auto Kick() {
  return [phase = 0.0f, noise = Noise(7u)](float t, float dt) mutable {
    phase += (48.0f + 120.0f * std::exp(-t / 0.035f)) * dt;
    return std::sin(kTau * phase) * std::exp(-t / 0.16f) *
               std::min(1.0f, t / 0.001f) +
           0.3f * std::exp(-t / 0.002f) * noise.Next();
  };
}

/**
 * Rim shot: a short tone and a crack of noise
 */
auto Rim(std::uint32_t seed) {
  return [=, noise = HighNoise(seed, 0.3f)](float t, float) mutable {
    return 0.6f * std::sin(kTau * 185.0f * t) * std::exp(-t / 0.05f) +
           noise.Next() * std::exp(-t / 0.07f);
  };
}

auto HiHat(std::uint32_t seed, float decay) {
  return [=, noise = HighNoise(seed, 0.6f)](float t, float) mutable {
    return noise.Next() * std::exp(-t / decay);
  };
}

auto Shaker(std::uint32_t seed) {
  return [=, noise = HighNoise(seed, 0.45f)](float t, float) mutable {
    return noise.Next() * std::min(1.0f, t / 0.012f) * std::exp(-t / 0.03f);
  };
}

auto WoodBlock(float frequency) {
  return [=](float t, float) {
    return std::sin(kTau * frequency * t) * std::exp(-t / 0.03f) +
           0.4f * std::sin(kTau * 1.47f * frequency * t) * std::exp(-t / 0.02f);
  };
}

/**
 * Slide whistle: glides from one pitch to another
 */
auto SlideWhistle(float from, float to, float duration) {
  return [=, phase = 0.0f](float t, float dt) mutable {
    float frequency = from * std::pow(to / from, Smooth(t / duration)) *
                      (1.0f + 0.01f * std::sin(kTau * 6.0f * t));
    phase += frequency * dt;
    phase -= std::floor(phase);
    float envelope = std::min(1.0f, t / 0.03f) *
                     std::min(1.0f, std::max(0.0f, duration - t) / 0.08f);
    return envelope *
           (std::sin(kTau * phase) + 0.1f * std::sin(2.0f * kTau * phase));
  };
}

/**
 * A spring that is plucked: boing
 */
auto Spring(float frequency) {
  return [=, phase = 0.0f](float t, float dt) mutable {
    phase += frequency *
             (1.0f + 0.45f * std::sin(kTau * 7.0f * t) * std::exp(-3.5f * t) -
              0.08f * t) *
             dt;
    phase -= std::floor(phase);
    return std::exp(-2.8f * t) * std::min(1.0f, t / 0.003f) *
           (std::sin(kTau * phase) + 0.3f * std::sin(3.0f * kTau * phase));
  };
}

auto Splash(std::uint32_t seed) {
  return [=, noise = HighNoise(seed, 0.5f)](float t, float) mutable {
    return noise.Next() * std::exp(-t / 0.5f) * std::min(1.0f, t / 0.002f);
  };
}

struct Chord {
  int bass;        // root in the bass
  int voicing[3];  // for the marimba
};
const Chord kF{41, {57, 60, 65}};
const Chord kDm{38, {57, 62, 65}};
const Chord kGm{43, {58, 62, 67}};
const Chord kC7{36, {58, 64, 67}};
const Chord kBb{46, {58, 62, 65}};
const Chord kC{36, {55, 60, 64}};
const Chord kAm{45, {57, 60, 64}};

// The melodies (F major): A and A' on the steel drum, B on the whistle, B' on
// both
const char* const kMelodyAStart =
    "A4:.5 C5:.5 F5:1 E5:.5 F5:.5 A5:1 | G5:.5 F5:.5 D5:1 r:.5 D5:.5 F5:.5 "
    "A5:.5 | Bb5:1 A5:.5 G5:.5 F5:.5 G5:.5 D5:1 | "
    "E5:.5 G5:.5 C6:1.5 Bb5:.5 G5:.5 E5:.5 | F5:.5 A5:.5 C6:.75 A5:.25 F5:1 "
    "C5:1 | D5:.5 F5:.5 Bb5:1 A5:.5 Bb5:.5 D6:1 | ";
const char* const kMelodyAEnd =
    "C6:.5 Bb5:.5 G5:.5 E5:.5 C5:.5 D5:.5 E5:.5 G5:.5 | F5:1.5 C5:.5 F5:.5 "
    "r:1.5";
const char* const kMelodyA2End =
    "E5:.5 G5:.5 Bb5:.5 C6:.5 E6:1 D6:.5 C6:.5 | F6:.75 C6:.25 A5:.5 F5:.5 r:2";
const char* const kMelodyBStart =
    "D5:1.5 C5:.5 Bb4:1 D5:1 | E5:1.5 D5:.5 C5:1 E5:1 | C5:1 E5:1 A5:1.5 G5:.5 "
    "| F5:1 E5:.5 D5:.5 A4:2 | "
    "Bb4:1 D5:1 G5:1 F5:.5 E5:.5 | E5:1 G5:1 C6:1.5 Bb5:.5 | A5:1 G5:.5 F5:.5 "
    "C5:1 A4:1 | ";
const char* const kMelodyBEnd = "F4:.5 A4:.5 C5:.5 F5:.5 r:1 C5:.5 E5:.5";
const char* const kMelodyB2End = "F4:.5 A4:.5 C5:.5 F5:.5 r:2";

std::vector<Note> Melody(const std::string& text, float start) {
  std::string error;
  return ParseMelody(
      text, start,
      error);  // the melodies are checked by the tests (IslandTuneMelodies)
}
}  // namespace

std::vector<std::string> IslandTuneMelodies() {
  return {std::string(kMelodyAStart) + kMelodyAEnd,
          std::string(kMelodyAStart) + kMelodyA2End,
          std::string(kMelodyBStart) + kMelodyBEnd,
          std::string(kMelodyBStart) + kMelodyB2End};
}

float NoteFrequency(float pitch) {
  return 440.0f * std::pow(2.0f, (pitch - 69.0f) / 12.0f);
}

std::vector<Note> ParseMelody(const std::string& text, float start,
                              std::string& error) {
  std::vector<Note> notes;
  std::istringstream words(text);
  std::string word;
  float beat = start, bar_start = start;
  int bar = 1;
  auto end_bar = [&]() {
    if (std::abs(beat - bar_start - kBeatsPerBar) > 1e-3f) {
      error = "bar " + std::to_string(bar) + " has " +
              std::to_string(beat - bar_start) + " beats instead of 4";
      return false;
    }
    bar_start = beat;
    ++bar;
    return true;
  };
  while (words >> word) {
    if (word == "|") {
      if (!end_bar()) {
        return {};
      }
      continue;
    }
    std::size_t colon = word.find(':');
    float length = 0.0f;
    if (colon == std::string::npos || colon == 0 ||
        !(std::istringstream(word.substr(colon + 1)) >> length) ||
        length <= 0.0f) {
      error = "\"" + word + "\" is not name:length";
      return {};
    }
    std::string name = word.substr(0, colon);
    if (name != "r") {
      static const int kSteps[] = {9, 11, 0, 2, 4, 5, 7};  // A to G
      char letter =
          static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
      std::size_t next = 1;
      int accidental = 0;
      if (next < name.size() && (name[next] == '#' || name[next] == 'b')) {
        accidental = name[next] == '#' ? 1 : -1;
        ++next;
      }
      int octave = 0;
      if (letter < 'A' || letter > 'G' || next >= name.size() ||
          !(std::istringstream(name.substr(next)) >> octave)) {
        error = "unknown note \"" + name + "\"";
        return {};
      }
      notes.push_back({beat, length,
                       12 * (octave + 1) + kSteps[letter - 'A'] + accidental});
    }
    beat += length;
  }
  if (beat > bar_start + 1e-3f && !end_bar()) {
    return {};
  }
  return notes;
}

Song ComposeIslandTune(int sample_rate) {
  Song song;
  song.sample_rate = sample_rate;
  song.beats_per_minute = kBeatsPerMinute;
  song.bars = 32;
  const float beat = 60.0f / kBeatsPerMinute;
  const std::size_t frames = static_cast<std::size_t>(
      std::lround(static_cast<float>(song.bars * kBeatsPerBar) * beat *
                  static_cast<float>(sample_rate)));
  if (sample_rate <= 0 || frames == 0) {
    return song;
  }
  Mixer mixer(sample_rate, frames);
  auto at = [beat](float beats) { return beats * beat; };

  // Sections of 8 bars: A, A' (another ending), B, B' (into A again)
  const Chord progression_a[8] = {kF, kDm, kGm, kC7, kF, kBb, kC7, kF};
  const Chord progression_b[8] = {kBb, kC, kAm, kDm, kGm, kC7, kF, kF};
  std::vector<Chord> chords;
  for (int section = 0; section < 4; ++section) {
    const Chord* progression = section < 2 ? progression_a : progression_b;
    chords.insert(chords.end(), progression, progression + 8);
  }
  chords.back() = kC7;  // turns around into the beginning

  // Lead: steel drum and whistle (with the echo)
  const float part_a = 0.0f, part_a2 = 32.0f, part_b = 64.0f,
              part_b2 = 96.0f;  // first beats of the sections
  std::uint32_t seed = 1;
  auto play_steel = [&](const std::vector<Note>& notes, int transpose,
                        float gain) {
    for (const Note& note : notes) {
      float hold = at(note.length) * 0.85f;
      mixer.Play(
          at(note.start), hold + 0.45f, gain, 0.15f, true,
          SteelDrum(NoteFrequency(static_cast<float>(note.pitch + transpose)),
                    hold));
    }
  };
  auto play_whistle = [&](const std::vector<Note>& notes, int transpose,
                          float gain) {
    for (const Note& note : notes) {
      float hold = at(note.length) * 0.92f;
      mixer.Play(
          at(note.start), hold + 0.3f, gain, -0.1f, true,
          Whistle(NoteFrequency(static_cast<float>(note.pitch + transpose)),
                  hold, seed++));
    }
  };
  play_steel(Melody(std::string(kMelodyAStart) + kMelodyAEnd, part_a), 0,
             0.36f);
  play_steel(Melody(std::string(kMelodyAStart) + kMelodyA2End, part_a2), 0,
             0.36f);
  play_whistle(Melody(std::string(kMelodyBStart) + kMelodyBEnd, part_b), 0,
               0.24f);
  std::vector<Note> last_b =
      Melody(std::string(kMelodyBStart) + kMelodyB2End, part_b2);
  play_steel(last_b, 0, 0.32f);
  play_whistle(last_b, -12, 0.14f);

  for (std::size_t bar = 0; bar < chords.size(); ++bar) {
    const Chord& chord = chords[bar];
    const float first = static_cast<float>(bar * kBeatsPerBar);
    bool section_b = bar >= 16;

    // Bass: root, fifth and octave in a calypso rhythm
    const struct {
      float beat, length;
      int interval;
      float gain;
    } bass[] = {{0.0f, 1.2f, 0, 0.36f},
                {1.5f, 0.4f, 0, 0.26f},
                {2.0f, 0.9f, 7, 0.32f},
                {3.0f, 0.4f, 12, 0.26f},
                {3.5f, 0.4f, 7, 0.26f}};
    for (const auto& note : bass) {
      float hold = at(note.length);
      mixer.Play(
          at(first + note.beat), hold + 0.25f, note.gain, 0.0f, false,
          Bass(NoteFrequency(static_cast<float>(chord.bass + note.interval)),
               hold));
    }

    // Marimba: chords on the offbeats, arpeggios in the B part
    if (bar < 16 || bar >= 24) {
      for (float offbeat : {0.5f, 1.5f, 2.5f, 3.5f}) {
        for (int pitch : chord.voicing) {
          mixer.Play(at(first + offbeat), 0.7f, bar >= 24 ? 0.05f : 0.07f,
                     -0.45f, false,
                     Marimba(NoteFrequency(static_cast<float>(pitch))));
        }
      }
    }
    if (section_b) {
      const int arpeggio[8] = {chord.voicing[0],      chord.voicing[1],
                               chord.voicing[2],      chord.voicing[0] + 12,
                               chord.voicing[1] + 12, chord.voicing[2],
                               chord.voicing[1],      chord.voicing[2]};
      for (int i = 0; i < 8; ++i) {
        mixer.Play(at(first + 0.5f * static_cast<float>(i)), 0.7f,
                   bar >= 24 ? 0.035f : 0.06f, 0.5f, false,
                   Marimba(NoteFrequency(static_cast<float>(arpeggio[i]))));
      }
    }

    // Percussion
    for (float b : {0.0f, 2.0f}) {
      mixer.Play(at(first + b), 0.45f, 0.45f, 0.0f, false, Kick());
    }
    for (float b : {1.5f, 3.5f}) {
      mixer.Play(at(first + b), 0.45f, 0.22f, 0.0f, false, Kick());
    }
    for (float b : {1.0f, 3.0f}) {
      mixer.Play(at(first + b), 0.3f, 0.17f, 0.1f, false, Rim(seed++));
    }
    for (int i = 0; i < 8; ++i) {
      bool offbeat = i % 2 == 1;
      mixer.Play(at(first + 0.5f * static_cast<float>(i)), 0.25f,
                 offbeat ? 0.07f : 0.04f, 0.35f, false,
                 HiHat(seed++, offbeat && i == 7 ? 0.06f : 0.018f));
    }
    for (int i = 0; i < 16; ++i) {
      mixer.Play(at(first + 0.25f * static_cast<float>(i)), 0.1f,
                 i % 4 == 2 ? 0.035f : 0.02f, -0.3f, false, Shaker(seed++));
    }
  }

  // Slapstick at the ends of the phrases, a splash cymbal at the beginning of
  // the loop
  mixer.Play(at(part_a), 1.5f, 0.05f, 0.3f, false, Splash(99u));
  mixer.Play(at(part_a + 30.5f), at(1.0f), 0.16f, 0.25f, true,
             SlideWhistle(500.0f, 1500.0f, at(1.0f)));  // up
  mixer.Play(at(part_a2 + 30.0f), 1.2f, 0.2f, -0.2f, false,
             Spring(220.0f));  // boing
  for (float b : {30.0f, 30.333f, 30.667f}) {
    mixer.Play(
        at(part_b + b), 0.15f, 0.18f, -0.4f, false,
        WoodBlock(1200.0f + 300.0f * (b - 30.0f) * 3.0f));  // tok tok tok
  }
  mixer.Play(at(part_b2 + 30.0f), at(0.75f), 0.15f, 0.25f, true,
             SlideWhistle(1400.0f, 480.0f, at(0.75f)));  // down
  mixer.Play(at(part_b2 + 30.75f), at(1.25f), 0.15f, 0.25f, true,
             SlideWhistle(480.0f, 1700.0f, at(1.25f)));  // and up again

  mixer.ApplyEcho(at(0.75f), 0.35f, 0.28f);

  // Normalized below full scale
  float peak = 1e-6f;
  for (std::size_t frame = 0; frame < frames; ++frame) {
    peak = std::max({peak, std::abs(mixer.dry.left[frame]),
                     std::abs(mixer.dry.right[frame])});
  }
  float gain = 0.89f / peak;
  song.samples.resize(frames * 2);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    song.samples[frame * 2] = mixer.dry.left[frame] * gain;
    song.samples[frame * 2 + 1] = mixer.dry.right[frame] * gain;
  }
  return song;
}

}  // namespace bibo::audio
