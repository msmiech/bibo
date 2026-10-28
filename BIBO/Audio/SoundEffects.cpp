#include "SoundEffects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace bibo::audio {

namespace {
const float kTau = 6.28318531f;
const int kBeaverVariants = 8;
const int kBearVariants = 6;
const int kRaccoonVariants = 6;

float Smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

/**
 * Random numbers, the same every time
 */
class Random {
 public:
  explicit Random(std::uint32_t seed)
      : state_(seed * 2654435761u + 0x9E3779B9u) {}
  // 0 to 1
  float Next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return static_cast<float>(state_ >> 8) * (1.0f / 16777216.0f);
  }
  //-1 to 1
  float Noise() { return 2.0f * Next() - 1.0f; }

 private:
  std::uint32_t state_;
};

/**
 * Scales the sound so that its loudest sample has this value
 */
void Normalize(std::vector<float>& samples, float peak) {
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
void AddHarmonics(std::vector<float>& samples, int sample_rate,
                  std::size_t first, std::size_t count, int harmonics,
                  Frequency frequency, Filter filter) {
  float phase = 0.0f;
  float nyquist = 0.45f * static_cast<float>(sample_rate);
  for (std::size_t i = 0; i < count && first + i < samples.size(); ++i) {
    float t = static_cast<float>(i) / static_cast<float>(sample_rate);
    float f = frequency(t);
    phase += kTau * f / static_cast<float>(sample_rate);
    if (phase > kTau * 1000.0f) {
      // keeps the precision (whole cycles of all harmonics)
      phase -= kTau * 1000.0f;
    }
    float value = 0.0f;
    for (int k = 1; k <= harmonics && k * f < nyquist; ++k) {
      value += filter(k * f, t, k) * std::sin(phase * static_cast<float>(k));
    }
    samples[first + i] += value;
  }
}

float Peak(float frequency, float center, float width) {
  float x = (frequency - center) / width;
  return std::exp(-x * x);
}

/**
 * Beavers whine and chatter through their nose: a high voice with a pitch that
 * goes up or down, nasal formants, a fast flutter (the chatter of the teeth)
 * and a bit of breath
 */
std::vector<float> Beaver(int sample_rate, int variant) {
  Random random(static_cast<std::uint32_t>(variant) * 7919u + 17u);
  float duration = 0.11f + 0.08f * random.Next();
  float base = 430.0f + 230.0f * random.Next();
  float vowel = 1150.0f +
                // the second formant: "ee" when high, "oo" when low
                900.0f * random.Next();
  float chatter_rate = 24.0f + 12.0f * random.Next();
  float chatter_depth = 0.25f + 0.35f * random.Next();
  int contour = variant % 3;
  std::size_t count =
      static_cast<std::size_t>(duration * static_cast<float>(sample_rate));
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
    return base * shape * (1.0f + 0.02f * std::sin(kTau * 9.0f * t));
  };
  AddHarmonics(
      samples, sample_rate, 0, count, 16, pitch,
      [&](float frequency, float, int k) {
        float formants = 0.2f + Peak(frequency, 380.0f, 200.0f) +
                         0.9f * Peak(frequency, vowel, 280.0f) +
                         0.45f * Peak(frequency, 2800.0f, 450.0f);
        float nasal =
            1.0f -
            0.65f * Peak(frequency, 950.0f,
                         160.0f);  // the nose takes away these frequencies
        return formants * nasal / std::pow(static_cast<float>(k), 0.7f);
      });
  Random breath(static_cast<std::uint32_t>(variant) + 101u);
  float low = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(sample_rate);
    float envelope =
        Smooth(t / 0.012f) *
        (1.0f - Smooth((t - 0.55f * duration) / (0.45f * duration)));
    float chatter =
        1.0f -
        chatter_depth * (0.5f + 0.5f * std::sin(kTau * chatter_rate * t));
    float noise = breath.Noise();
    low += (noise - low) * 0.3f;
    samples[i] = std::tanh(1.4f * samples[i] * 0.25f) * envelope * chatter +
                 0.05f * (noise - low) * envelope;
  }
  Normalize(samples, 0.55f);
  return samples;
}

/**
 * Raccoons chitter and trill: a high, bright voice that quickly rises or falls,
 * broken up by a fast trill, with a hiss of breath
 */
std::vector<float> Raccoon(int sample_rate, int variant) {
  Random random(static_cast<std::uint32_t>(variant) * 6151u + 41u);
  float duration = 0.09f + 0.08f * random.Next();
  float base = 820.0f + 420.0f * random.Next();
  float trill_rate = 34.0f + 16.0f * random.Next();
  float sweep = random.Next() < 0.5f ? 0.35f : -0.25f;
  std::size_t count =
      static_cast<std::size_t>(duration * static_cast<float>(sample_rate));
  std::vector<float> samples(count, 0.0f);
  AddHarmonics(
      samples, sample_rate, 0, count, 8,
      [&](float t) {
        return base * (1.0f + sweep * t / duration) *
               (1.0f + 0.04f * std::sin(kTau * trill_rate * t));
      },
      [&](float frequency, float, int k) {
        float formants = 0.3f + Peak(frequency, 1700.0f, 400.0f) +
                         0.5f * Peak(frequency, 3400.0f, 600.0f);
        return formants / static_cast<float>(k);
      });
  Random breath(static_cast<std::uint32_t>(variant) + 307u);
  float low = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(sample_rate);
    float envelope = Smooth(t / 0.008f) *
                     (1.0f - Smooth((t - 0.6f * duration) / (0.4f * duration)));
    float trill = 0.55f + 0.45f * std::sin(kTau * trill_rate * t);
    float noise = breath.Noise();
    low += (noise - low) * 0.5f;
    samples[i] = std::tanh(0.4f * samples[i]) * envelope * trill +
                 0.06f * (noise - low) * envelope;
  }
  Normalize(samples, 0.5f);
  return samples;
}

/**
 * A bear grumbles with its mouth almost closed: a low voice ("mhm", "hrrm")
 * with dark formants, a slow rasp in the throat and a breathy start
 */
std::vector<float> Bear(int sample_rate, int variant) {
  Random random(static_cast<std::uint32_t>(variant) * 4099u + 29u);
  float duration = 0.16f + 0.1f * random.Next();
  float base = 105.0f + 50.0f * random.Next();
  float vowel = 520.0f + 260.0f * random.Next();  //"oh" to "ah"
  float rasp_rate = 26.0f + 10.0f * random.Next();
  int contour = variant % 3;
  std::size_t count =
      static_cast<std::size_t>(duration * static_cast<float>(sample_rate));
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
  AddHarmonics(samples, sample_rate, 0, count, 30, pitch,
               [&](float frequency, float, int k) {
                 float formants = 0.15f + Peak(frequency, 280.0f, 140.0f) +
                                  0.7f * Peak(frequency, vowel, 220.0f) +
                                  0.2f * Peak(frequency, 2300.0f, 400.0f);
                 return formants / std::pow(static_cast<float>(k), 0.9f);
               });
  Random breath(static_cast<std::uint32_t>(variant) + 211u);
  float low = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(sample_rate);
    float envelope = Smooth(t / 0.025f) *
                     (1.0f - Smooth((t - 0.5f * duration) / (0.5f * duration)));
    float rasp = 1.0f - 0.3f * (0.5f + 0.5f * std::sin(kTau * rasp_rate * t));
    float noise = breath.Noise();
    low += (noise - low) * 0.1f;  // only the low rumble of the breath
    samples[i] =
        std::tanh(samples[i] * 0.3f) * envelope * rasp + 0.08f * low * envelope;
  }
  Normalize(samples, 0.6f);
  return samples;
}

/**
 * A short tone with a few harmonics that fades out
 */
void AddBeep(std::vector<float>& samples, int sample_rate, float start,
             float frequency, float decay, float gain) {
  std::size_t first =
      static_cast<std::size_t>(start * static_cast<float>(sample_rate));
  for (std::size_t i = first; i < samples.size(); ++i) {
    float t = static_cast<float>(i - first) / static_cast<float>(sample_rate);
    float envelope = Smooth(t / 0.004f) * std::exp(-t / decay);
    float phase = kTau * frequency * t;
    samples[i] += gain * envelope *
                  (std::sin(phase) + 0.3f * std::sin(2.0f * phase) +
                   0.1f * std::sin(3.0f * phase));
  }
}

/**
 * A marimba-like note: the fundamental and the bright fourth partial, which
 * fades faster
 */
void AddMallet(std::vector<float>& samples, int sample_rate, float start,
               float frequency, float decay, float gain) {
  std::size_t first =
      static_cast<std::size_t>(start * static_cast<float>(sample_rate));
  for (std::size_t i = first; i < samples.size(); ++i) {
    float t = static_cast<float>(i - first) / static_cast<float>(sample_rate);
    float phase = kTau * frequency * t;
    samples[i] +=
        gain * Smooth(t / 0.003f) *
        (std::exp(-t / decay) * std::sin(phase) +
         0.35f * std::exp(-t / (0.25f * decay)) * std::sin(4.0f * phase));
  }
}

std::vector<float> Success(int sample_rate) {
  std::vector<float> samples(
      static_cast<std::size_t>(1.4f * static_cast<float>(sample_rate)), 0.0f);
  // C major arpeggio up, then the chord
  const float notes[] = {523.25f, 659.25f, 783.99f, 1046.5f};
  for (int i = 0; i < 4; ++i) {
    AddMallet(samples, sample_rate, 0.11f * static_cast<float>(i), notes[i],
              0.25f, 0.5f);
  }
  for (float note : {523.25f, 659.25f, 783.99f, 1046.5f}) {
    AddMallet(samples, sample_rate, 0.5f, note, 0.5f, 0.3f);
  }
  Normalize(samples, 0.6f);
  return samples;
}

/**
 * A muted trombone going down: "wah wah wah waaah"
 */
std::vector<float> Failure(int sample_rate) {
  const float notes[] = {311.13f, 293.66f, 277.18f, 261.63f};
  const float note_length = 0.34f, last_length = 1.0f;
  std::vector<float> samples(
      static_cast<std::size_t>((3.0f * note_length + last_length + 0.1f) *
                               static_cast<float>(sample_rate)),
      0.0f);
  for (int n = 0; n < 4; ++n) {
    float length = n < 3 ? note_length : last_length;
    std::size_t first = static_cast<std::size_t>(
        note_length * static_cast<float>(n) * static_cast<float>(sample_rate));
    std::size_t count =
        static_cast<std::size_t>(length * static_cast<float>(sample_rate));
    std::vector<float> note(count, 0.0f);
    float frequency = notes[n];
    bool last = n == 3;
    AddHarmonics(
        note, sample_rate, 0, count, 12,
        [&](float t) {
          // The last note wobbles
          return frequency * (1.0f + (last ? 0.03f * std::sin(kTau * 5.0f * t) *
                                                 Smooth((t - 0.2f) / 0.2f)
                                           : 0.0f));
        },
        [&](float, float t, int k) {
          // The mute opens: the sound gets brighter after the start of the note
          // ("wah")
          float brightness = 1.5f + 3.5f * Smooth(t / 0.12f);
          return std::exp(-static_cast<float>(k) / brightness) /
                 static_cast<float>(k);
        });
    for (std::size_t i = 0; i < count && first + i < samples.size(); ++i) {
      float t = static_cast<float>(i) / static_cast<float>(sample_rate);
      float envelope =
          Smooth(t / 0.03f) * (1.0f - Smooth((t - (length - 0.08f)) / 0.08f));
      samples[first + i] += note[i] * envelope;
    }
  }
  Normalize(samples, 0.55f);
  return samples;
}

/**
 * Water splashing: noise that gets darker as it fades, and a few bubbles
 */
std::vector<float> Splash(int sample_rate) {
  const float duration = 0.8f;
  std::size_t count =
      static_cast<std::size_t>(duration * static_cast<float>(sample_rate));
  std::vector<float> samples(count, 0.0f);
  Random random(4711u);
  float low = 0.0f, lower = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(sample_rate);
    float cutoff = 0.5f * std::exp(-t / 0.18f) + 0.03f;
    float noise = random.Noise();
    low += (noise - low) * cutoff;
    lower += (low - lower) * cutoff;
    samples[i] = lower * Smooth(t / 0.01f) * std::exp(-t / 0.22f);
  }
  // Bubbles: short chirps going up
  for (int bubble = 0; bubble < 6; ++bubble) {
    float start = 0.08f + 0.5f * random.Next();
    float frequency = 500.0f + 700.0f * random.Next();
    std::size_t first =
        static_cast<std::size_t>(start * static_cast<float>(sample_rate));
    float phase = 0.0f;
    for (std::size_t i = first; i < count; ++i) {
      float t = static_cast<float>(i - first) / static_cast<float>(sample_rate);
      if (t > 0.06f) {
        break;
      }
      phase += kTau * frequency * (1.0f + 6.0f * t) /
               static_cast<float>(sample_rate);
      samples[i] += 0.15f * std::sin(phase) *
                    std::sin(std::numbers::pi_v<float> * t / 0.06f);
    }
  }
  Normalize(samples, 0.6f);
  return samples;
}
}  // namespace

int SoundEffectVariants(SoundEffect effect) {
  switch (effect) {
    case SoundEffect::kBeaver:
      return kBeaverVariants;
    case SoundEffect::kBear:
      return kBearVariants;
    case SoundEffect::kRaccoon:
      return kRaccoonVariants;
    default:
      return 1;
  }
}

std::vector<float> SynthesizeSoundEffect(SoundEffect effect, int variant,
                                         int sample_rate) {
  switch (effect) {
    case SoundEffect::kBeaver:
      return Beaver(sample_rate, std::clamp(variant, 0, kBeaverVariants - 1));
    case SoundEffect::kBear:
      return Bear(sample_rate, std::clamp(variant, 0, kBearVariants - 1));
    case SoundEffect::kRaccoon:
      return Raccoon(sample_rate, std::clamp(variant, 0, kRaccoonVariants - 1));
    case SoundEffect::kCountdown: {
      std::vector<float> samples(
          static_cast<std::size_t>(0.35f * static_cast<float>(sample_rate)),
          0.0f);
      AddBeep(samples, sample_rate, 0.0f, 740.0f, 0.08f, 1.0f);
      Normalize(samples, 0.5f);
      return samples;
    }
    case SoundEffect::kGo: {
      std::vector<float> samples(
          static_cast<std::size_t>(1.0f * static_cast<float>(sample_rate)),
          0.0f);
      AddBeep(samples, sample_rate, 0.0f, 1108.73f, 0.25f, 1.0f);
      AddBeep(samples, sample_rate, 0.0f, 1479.98f, 0.2f, 0.4f);
      Normalize(samples, 0.55f);
      return samples;
    }
    case SoundEffect::kCheckpoint: {
      std::vector<float> samples(
          static_cast<std::size_t>(0.6f * static_cast<float>(sample_rate)),
          0.0f);
      AddMallet(samples, sample_rate, 0.0f, 1318.51f, 0.12f, 0.6f);
      AddMallet(samples, sample_rate, 0.08f, 1760.0f, 0.2f, 0.8f);
      Normalize(samples, 0.4f);
      return samples;
    }
    case SoundEffect::kSuccess:
      return Success(sample_rate);
    case SoundEffect::kFailure:
      return Failure(sample_rate);
    case SoundEffect::kSplash:
      return Splash(sample_rate);
    case SoundEffect::kCount:
      break;
  }
  return {};
}

}  // namespace bibo::audio
