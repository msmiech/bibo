#include "MusicPlayer.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "Music.hpp"

namespace bibo::audio {

namespace {
const ma_uint32 kSampleRate = 44100;
// Fading in and out (seconds from silence to full volume)
const float kFadeTime = 0.6f;
// Volume of the sound effects
const float kEffectsVolume = 0.8f;
// At most this many sounds play at the same time (the oldest one stops)
const std::size_t kMaxVoices = 16;
}  // namespace

struct MusicPlayer::Impl {
  ma_device device;
  bool has_device = false;
  std::thread composer;
  // Written by the composer before ready is set, then only read by the audio
  // thread
  std::vector<float> samples;
  std::size_t frames = 0;
  std::atomic<bool> ready{false};
  std::atomic<bool> enabled{true};
  std::atomic<float> volume{0.55f};
  // Audio thread only
  std::size_t position = 0;
  float gain = 0.0f;

  // Sound effects: synthesized by the composer before effects_ready is set, per
  // effect and variant (mono)
  std::array<std::vector<std::vector<float>>, kSoundEffectCount> effects;
  std::atomic<bool> effects_ready{false};
  std::atomic<bool> effects_enabled{true};
  struct Voice {
    const std::vector<float>* samples = nullptr;
    std::size_t position = 0;
    float gain = 1.0f;
  };
  // Sounds started by the game, taken over by the audio thread
  std::mutex pending_mutex;
  std::vector<Voice> pending;
  // Audio thread only
  std::vector<Voice> voices;

  // Called by miniaudio on its audio thread: the next frames of the loop and
  // the sounds (the output is silent before)
  static void Fill(ma_device* device, void* output, const void*,
                   ma_uint32 frame_count) {
    Impl& self = *static_cast<Impl*>(device->pUserData);
    float* out = static_cast<float*>(output);
    if (self.ready.load(std::memory_order_acquire) && self.frames > 0) {
      float target = self.enabled.load(std::memory_order_relaxed)
                         ? self.volume.load(std::memory_order_relaxed)
                         : 0.0f;
      float step = 1.0f / (kFadeTime * static_cast<float>(device->sampleRate));
      for (ma_uint32 i = 0; i < frame_count; ++i) {
        self.gain += std::clamp(target - self.gain, -step, step);
        out[2 * i] = self.samples[2 * self.position] * self.gain;
        out[2 * i + 1] = self.samples[2 * self.position + 1] * self.gain;
        self.position = (self.position + 1) % self.frames;
      }
    }
    if (!self.effects_ready.load(std::memory_order_acquire)) {
      return;
    }
    {
      // Never waits for the game (it only holds the lock for a moment)
      std::unique_lock<std::mutex> lock(self.pending_mutex, std::try_to_lock);
      if (lock.owns_lock()) {
        for (const Voice& voice : self.pending) {
          if (self.voices.size() >= kMaxVoices) {
            self.voices.erase(self.voices.begin());
          }
          self.voices.push_back(voice);
        }
        self.pending.clear();
      }
    }
    bool effects_on = self.effects_enabled.load(std::memory_order_relaxed);
    for (Voice& voice : self.voices) {
      const std::vector<float>& sound = *voice.samples;
      for (ma_uint32 i = 0; i < frame_count && voice.position < sound.size();
           ++i, ++voice.position) {
        if (effects_on) {
          float value = sound[voice.position] * voice.gain;
          out[2 * i] = std::clamp(out[2 * i] + value, -1.0f, 1.0f);
          out[2 * i + 1] = std::clamp(out[2 * i + 1] + value, -1.0f, 1.0f);
        }
      }
    }
    std::erase_if(self.voices, [](const Voice& voice) {
      return voice.position >= voice.samples->size();
    });
  }
};

MusicPlayer::MusicPlayer() : impl_(std::make_unique<Impl>()) {
  impl_->pending.reserve(kMaxVoices);
  impl_->voices.reserve(kMaxVoices);
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = 2;
  // miniaudio converts it to the rate of the device
  config.sampleRate = kSampleRate;
  config.dataCallback = Impl::Fill;
  config.pUserData = impl_.get();
  if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
    std::cerr << "WARNING: No audio device, the game runs without music"
              << std::endl;
    return;
  }
  impl_->has_device = true;
  if (ma_device_start(&impl_->device) != MA_SUCCESS) {
    std::cerr << "WARNING: Could not start the audio device, the game runs "
                 "without music"
              << std::endl;
    return;
  }
  // Synthesizing takes a moment, the game starts meanwhile
  Impl* player = impl_.get();
  impl_->composer = std::thread([player] {
    // The sounds first, they are quick
    for (int effect = 0; effect < kSoundEffectCount; ++effect) {
      for (int variant = 0;
           variant < SoundEffectVariants(static_cast<SoundEffect>(effect));
           ++variant) {
        player->effects[effect].push_back(
            SynthesizeSoundEffect(static_cast<SoundEffect>(effect), variant,
                                  static_cast<int>(kSampleRate)));
      }
    }
    player->effects_ready.store(true, std::memory_order_release);
    Song song = ComposeIslandTune(static_cast<int>(kSampleRate));
    player->frames = song.Frames();
    player->samples = std::move(song.samples);
    player->ready.store(true, std::memory_order_release);
  });
}

MusicPlayer::~MusicPlayer() {
  if (impl_->composer.joinable()) {
    impl_->composer.join();
  }
  if (impl_->has_device) {
    ma_device_uninit(&impl_->device);  // stops the audio thread
  }
}

void MusicPlayer::SetEnabled(bool enabled) {
  impl_->enabled.store(enabled, std::memory_order_relaxed);
}

void MusicPlayer::SetVolume(float volume) {
  impl_->volume.store(std::clamp(volume, 0.0f, 1.0f),
                      std::memory_order_relaxed);
}

void MusicPlayer::Play(SoundEffect effect, int variant, float volume) {
  int index = static_cast<int>(effect);
  if (!impl_->has_device ||
      !impl_->effects_ready.load(std::memory_order_acquire) || index < 0 ||
      index >= kSoundEffectCount ||
      !impl_->effects_enabled.load(std::memory_order_relaxed)) {
    return;
  }
  const std::vector<std::vector<float>>& variants = impl_->effects[index];
  if (variants.empty()) {
    return;
  }
  Impl::Voice voice;
  voice.samples = &variants[static_cast<std::size_t>(std::max(variant, 0)) %
                            variants.size()];
  voice.gain = kEffectsVolume * std::clamp(volume, 0.0f, 1.0f);
  std::lock_guard<std::mutex> lock(impl_->pending_mutex);
  if (impl_->pending.size() < kMaxVoices) {
    impl_->pending.push_back(voice);
  }
}

void MusicPlayer::SetEffectsEnabled(bool enabled) {
  impl_->effects_enabled.store(enabled, std::memory_order_relaxed);
}

bool MusicPlayer::HasDevice() const { return impl_->has_device; }

bool MusicPlayer::IsPlaying() const {
  return impl_->has_device && impl_->ready.load(std::memory_order_acquire);
}

}  // namespace bibo::audio
