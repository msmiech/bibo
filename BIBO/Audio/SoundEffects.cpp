#include "SoundEffects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

using namespace bibo::audio;

namespace {
const float TAU = 6.28318531f;
const int BEAVER_VARIANTS = 8;
const int BEAR_VARIANTS = 6;
const int RACCOON_VARIANTS = 6;

float smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

/**
 * Random numbers, the same every time
 */
class Random {
 public:
  explicit Random(std::uint32_t seed)
      : state(seed * 2654435761u + 0x9E3779B9u) {}
  // 0 to 1
  float next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
  }
  //-1 to 1
  float noise() { return 2.0f * next() - 1.0f; }

 private:
  std::uint32_t state;
};

/**
 * Scales the sound so that its loudest sample has this value
 */
void normalize(std::vector<float>& samples, float peak) {
  float loudest = 0.0f;
  for (float sample : samples) {
    loudest = std::max(loudest, std::abs(sample));
  }
  if (loudest > 0.0f) {
    for (float& sample : samples) {
      sample *= peak / loudest;
    }
  }
}

/**
 * A voice made of harmonics: the strength of every harmonic comes from the
 * filter (a function of its frequency)
 * @param frequency Fundamental at a time (Hz)
 * @param filter Strength of a harmonic at a frequency and a time
 */
template <typename Frequency, typename Filter>
void addHarmonics(std::vector<float>& samples, int sampleRate,
                  std::size_t first, std::size_t count, int harmonics,
                  Frequency frequency, Filter filter) {
  float phase = 0.0f;
  float nyquist = 0.45f * static_cast<float>(sampleRate);
  for (std::size_t i = 0; i < count && first + i < samples.size(); ++i) {
    float t = static_cast<float>(i) / static_cast<float>(sampleRate);
    float f = frequency(t);
    phase += TAU * f / static_cast<float>(sampleRate);
    if (phase > TAU * 1000.0f) {
      phase -=
          TAU * 1000.0f;  // keeps the precision (whole cycles of all harmonics)
    }
    float value = 0.0f;
    for (int k = 1; k <= harmonics && k * f < nyquist; ++k) {
      value += filter(k * f, t, k) * std::sin(phase * static_cast<float>(k));
    }
    samples[first + i] += value;
  }
}

float peak(float frequency, float center, float width) {
  float x = (frequency - center) / width;
  return std::exp(-x * x);
}

/**
 * Beavers whine and chatter through their nose: a high voice with a pitch that
 * goes up or down, nasal formants, a fast flutter (the chatter of the teeth)
 * and a bit of breath
 */
std::vector<float> beaver(int sampleRate, int variant) {
  Random random(static_cast<std::uint32_t>(variant) * 7919u + 17u);
  float duration = 0.11f + 0.08f * random.next();
  float base = 430.0f + 230.0f * random.next();
  float vowel =
      1150.0f +
      900.0f *
          random.next();  // the second formant: "ee" when high, "oo" when low
  float chatterRate = 24.0f + 12.0f * random.next();
  float chatterDepth = 0.25f + 0.35f * random.next();
  int contour = variant % 3;
  std::size_t count =
      static_cast<std::size_t>(duration * static_cast<float>(sampleRate));
  std::vector<float> samples(count, 0.0f);

  auto pitch = [&](float t) {
    float u = t / duration;
    float shape;
    switch (contour) {
      case 0:
        shape = 1.0f + 0.25f * std::sin(std::numbers::pi_v<float> * u);
        break;  // up and down
      case 1:
        shape = 1.25f - 0.35f * u;
        break;  // down
      default:
        shape = 0.9f + 0.4f * u * u;
        break;  // up, like a question
    }
    return base * shape * (1.0f + 0.02f * std::sin(TAU * 9.0f * t));
  };
  addHarmonics(
      samples, sampleRate, 0, count, 16, pitch,
      [&](float frequency, float, int k) {
        float formants = 0.2f + peak(frequency, 380.0f, 200.0f) +
                         0.9f * peak(frequency, vowel, 280.0f) +
                         0.45f * peak(frequency, 2800.0f, 450.0f);
        float nasal =
            1.0f -
            0.65f * peak(frequency, 950.0f,
                         160.0f);  // the nose takes away these frequencies
        return formants * nasal / std::pow(static_cast<float>(k), 0.7f);
      });
  Random breath(static_cast<std::uint32_t>(variant) + 101u);
  float low = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(sampleRate);
    float envelope =
        smooth(t / 0.012f) *
        (1.0f - smooth((t - 0.55f * duration) / (0.45f * duration)));
    float chatter =
        1.0f - chatterDepth * (0.5f + 0.5f * std::sin(TAU * chatterRate * t));
    float noise = breath.noise();
    low += (noise - low) * 0.3f;
    samples[i] = std::tanh(1.4f * samples[i] * 0.25f) * envelope * chatter +
                 0.05f * (noise - low) * envelope;
  }
  normalize(samples, 0.55f);
  return samples;
}

/**
 * Raccoons chitter and trill: a high, bright voice that quickly rises or falls,
 * broken up by a fast trill, with a hiss of breath
 */
std::vector<float> raccoon(int sampleRate, int variant) {
  Random random(static_cast<std::uint32_t>(variant) * 6151u + 41u);
  float duration = 0.09f + 0.08f * random.next();
  float base = 820.0f + 420.0f * random.next();
  float trillRate = 34.0f + 16.0f * random.next();
  float sweep = random.next() < 0.5f ? 0.35f : -0.25f;
  std::size_t count =
      static_cast<std::size_t>(duration * static_cast<float>(sampleRate));
  std::vector<float> samples(count, 0.0f);
  addHarmonics(
      samples, sampleRate, 0, count, 8,
      [&](float t) {
        return base * (1.0f + sweep * t / duration) *
               (1.0f + 0.04f * std::sin(TAU * trillRate * t));
      },
      [&](float frequency, float, int k) {
        float formants = 0.3f + peak(frequency, 1700.0f, 400.0f) +
                         0.5f * peak(frequency, 3400.0f, 600.0f);
        return formants / static_cast<float>(k);
      });
  Random breath(static_cast<std::uint32_t>(variant) + 307u);
  float low = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(sampleRate);
    float envelope = smooth(t / 0.008f) *
                     (1.0f - smooth((t - 0.6f * duration) / (0.4f * duration)));
    float trill = 0.55f + 0.45f * std::sin(TAU * trillRate * t);
    float noise = breath.noise();
    low += (noise - low) * 0.5f;
    samples[i] = std::tanh(0.4f * samples[i]) * envelope * trill +
                 0.06f * (noise - low) * envelope;
  }
  normalize(samples, 0.5f);
  return samples;
}

/**
 * A bear grumbles with its mouth almost closed: a low voice ("mhm", "hrrm")
 * with dark formants, a slow rasp in the throat and a breathy start
 */
std::vector<float> bear(int sampleRate, int variant) {
  Random random(static_cast<std::uint32_t>(variant) * 4099u + 29u);
  float duration = 0.16f + 0.1f * random.next();
  float base = 105.0f + 50.0f * random.next();
  float vowel = 520.0f + 260.0f * random.next();  //"oh" to "ah"
  float raspRate = 26.0f + 10.0f * random.next();
  int contour = variant % 3;
  std::size_t count =
      static_cast<std::size_t>(duration * static_cast<float>(sampleRate));
  std::vector<float> samples(count, 0.0f);

  auto pitch = [&](float t) {
    float u = t / duration;
    float shape;
    switch (contour) {
      case 0:
        shape = 1.0f + 0.12f * std::sin(std::numbers::pi_v<float> * u);
        break;  //"mhm"
      case 1:
        shape = 1.1f - 0.2f * u;
        break;  // down, content
      default:
        shape = 0.92f + 0.22f * u * u;
        break;  // up, asking
    }
    return base * shape;
  };
  addHarmonics(samples, sampleRate, 0, count, 30, pitch,
               [&](float frequency, float, int k) {
                 float formants = 0.15f + peak(frequency, 280.0f, 140.0f) +
                                  0.7f * peak(frequency, vowel, 220.0f) +
                                  0.2f * peak(frequency, 2300.0f, 400.0f);
                 return formants / std::pow(static_cast<float>(k), 0.9f);
               });
  Random breath(static_cast<std::uint32_t>(variant) + 211u);
  float low = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(sampleRate);
    float envelope = smooth(t / 0.025f) *
                     (1.0f - smooth((t - 0.5f * duration) / (0.5f * duration)));
    float rasp = 1.0f - 0.3f * (0.5f + 0.5f * std::sin(TAU * raspRate * t));
    float noise = breath.noise();
    low += (noise - low) * 0.1f;  // only the low rumble of the breath
    samples[i] =
        std::tanh(samples[i] * 0.3f) * envelope * rasp + 0.08f * low * envelope;
  }
  normalize(samples, 0.6f);
  return samples;
}

/**
 * A short tone with a few harmonics that fades out
 */
void addBeep(std::vector<float>& samples, int sampleRate, float start,
             float frequency, float decay, float gain) {
  std::size_t first =
      static_cast<std::size_t>(start * static_cast<float>(sampleRate));
  for (std::size_t i = first; i < samples.size(); ++i) {
    float t = static_cast<float>(i - first) / static_cast<float>(sampleRate);
    float envelope = smooth(t / 0.004f) * std::exp(-t / decay);
    float phase = TAU * frequency * t;
    samples[i] += gain * envelope *
                  (std::sin(phase) + 0.3f * std::sin(2.0f * phase) +
                   0.1f * std::sin(3.0f * phase));
  }
}

/**
 * A marimba-like note: the fundamental and the bright fourth partial, which
 * fades faster
 */
void addMallet(std::vector<float>& samples, int sampleRate, float start,
               float frequency, float decay, float gain) {
  std::size_t first =
      static_cast<std::size_t>(start * static_cast<float>(sampleRate));
  for (std::size_t i = first; i < samples.size(); ++i) {
    float t = static_cast<float>(i - first) / static_cast<float>(sampleRate);
    float phase = TAU * frequency * t;
    samples[i] +=
        gain * smooth(t / 0.003f) *
        (std::exp(-t / decay) * std::sin(phase) +
         0.35f * std::exp(-t / (0.25f * decay)) * std::sin(4.0f * phase));
  }
}

std::vector<float> success(int sampleRate) {
  std::vector<float> samples(
      static_cast<std::size_t>(1.4f * static_cast<float>(sampleRate)), 0.0f);
  // C major arpeggio up, then the chord
  const float notes[] = {523.25f, 659.25f, 783.99f, 1046.5f};
  for (int i = 0; i < 4; ++i) {
    addMallet(samples, sampleRate, 0.11f * static_cast<float>(i), notes[i],
              0.25f, 0.5f);
  }
  for (float note : {523.25f, 659.25f, 783.99f, 1046.5f}) {
    addMallet(samples, sampleRate, 0.5f, note, 0.5f, 0.3f);
  }
  normalize(samples, 0.6f);
  return samples;
}

/**
 * A muted trombone going down: "wah wah wah waaah"
 */
std::vector<float> failure(int sampleRate) {
  const float notes[] = {311.13f, 293.66f, 277.18f, 261.63f};
  const float NOTE = 0.34f, LAST = 1.0f;
  std::vector<float> samples(
      static_cast<std::size_t>((3.0f * NOTE + LAST + 0.1f) *
                               static_cast<float>(sampleRate)),
      0.0f);
  for (int n = 0; n < 4; ++n) {
    float length = n < 3 ? NOTE : LAST;
    std::size_t first = static_cast<std::size_t>(
        NOTE * static_cast<float>(n) * static_cast<float>(sampleRate));
    std::size_t count =
        static_cast<std::size_t>(length * static_cast<float>(sampleRate));
    std::vector<float> note(count, 0.0f);
    float frequency = notes[n];
    bool last = n == 3;
    addHarmonics(
        note, sampleRate, 0, count, 12,
        [&](float t) {
          // The last note wobbles
          return frequency * (1.0f + (last ? 0.03f * std::sin(TAU * 5.0f * t) *
                                                 smooth((t - 0.2f) / 0.2f)
                                           : 0.0f));
        },
        [&](float, float t, int k) {
          // The mute opens: the sound gets brighter after the start of the note
          // ("wah")
          float brightness = 1.5f + 3.5f * smooth(t / 0.12f);
          return std::exp(-static_cast<float>(k) / brightness) /
                 static_cast<float>(k);
        });
    for (std::size_t i = 0; i < count && first + i < samples.size(); ++i) {
      float t = static_cast<float>(i) / static_cast<float>(sampleRate);
      float envelope =
          smooth(t / 0.03f) * (1.0f - smooth((t - (length - 0.08f)) / 0.08f));
      samples[first + i] += note[i] * envelope;
    }
  }
  normalize(samples, 0.55f);
  return samples;
}

/**
 * Water splashing: noise that gets darker as it fades, and a few bubbles
 */
std::vector<float> splash(int sampleRate) {
  const float duration = 0.8f;
  std::size_t count =
      static_cast<std::size_t>(duration * static_cast<float>(sampleRate));
  std::vector<float> samples(count, 0.0f);
  Random random(4711u);
  float low = 0.0f, lower = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(sampleRate);
    float cutoff = 0.5f * std::exp(-t / 0.18f) + 0.03f;
    float noise = random.noise();
    low += (noise - low) * cutoff;
    lower += (low - lower) * cutoff;
    samples[i] = lower * smooth(t / 0.01f) * std::exp(-t / 0.22f);
  }
  // Bubbles: short chirps going up
  for (int bubble = 0; bubble < 6; ++bubble) {
    float start = 0.08f + 0.5f * random.next();
    float frequency = 500.0f + 700.0f * random.next();
    std::size_t first =
        static_cast<std::size_t>(start * static_cast<float>(sampleRate));
    float phase = 0.0f;
    for (std::size_t i = first; i < count; ++i) {
      float t = static_cast<float>(i - first) / static_cast<float>(sampleRate);
      if (t > 0.06f) {
        break;
      }
      phase +=
          TAU * frequency * (1.0f + 6.0f * t) / static_cast<float>(sampleRate);
      samples[i] += 0.15f * std::sin(phase) *
                    std::sin(std::numbers::pi_v<float> * t / 0.06f);
    }
  }
  normalize(samples, 0.6f);
  return samples;
}
}  // namespace

int bibo::audio::soundEffectVariants(SoundEffect effect) {
  switch (effect) {
    case SoundEffect::BEAVER:
      return BEAVER_VARIANTS;
    case SoundEffect::BEAR:
      return BEAR_VARIANTS;
    case SoundEffect::RACCOON:
      return RACCOON_VARIANTS;
    default:
      return 1;
  }
}

std::vector<float> bibo::audio::synthesizeSoundEffect(SoundEffect effect,
                                                      int variant,
                                                      int sampleRate) {
  switch (effect) {
    case SoundEffect::BEAVER:
      return beaver(sampleRate, std::clamp(variant, 0, BEAVER_VARIANTS - 1));
    case SoundEffect::BEAR:
      return bear(sampleRate, std::clamp(variant, 0, BEAR_VARIANTS - 1));
    case SoundEffect::RACCOON:
      return raccoon(sampleRate, std::clamp(variant, 0, RACCOON_VARIANTS - 1));
    case SoundEffect::COUNTDOWN: {
      std::vector<float> samples(
          static_cast<std::size_t>(0.35f * static_cast<float>(sampleRate)),
          0.0f);
      addBeep(samples, sampleRate, 0.0f, 740.0f, 0.08f, 1.0f);
      normalize(samples, 0.5f);
      return samples;
    }
    case SoundEffect::GO: {
      std::vector<float> samples(
          static_cast<std::size_t>(1.0f * static_cast<float>(sampleRate)),
          0.0f);
      addBeep(samples, sampleRate, 0.0f, 1108.73f, 0.25f, 1.0f);
      addBeep(samples, sampleRate, 0.0f, 1479.98f, 0.2f, 0.4f);
      normalize(samples, 0.55f);
      return samples;
    }
    case SoundEffect::CHECKPOINT: {
      std::vector<float> samples(
          static_cast<std::size_t>(0.6f * static_cast<float>(sampleRate)),
          0.0f);
      addMallet(samples, sampleRate, 0.0f, 1318.51f, 0.12f, 0.6f);
      addMallet(samples, sampleRate, 0.08f, 1760.0f, 0.2f, 0.8f);
      normalize(samples, 0.4f);
      return samples;
    }
    case SoundEffect::SUCCESS:
      return success(sampleRate);
    case SoundEffect::FAILURE:
      return failure(sampleRate);
    case SoundEffect::SPLASH:
      return splash(sampleRate);
    case SoundEffect::COUNT:
      break;
  }
  return {};
}
