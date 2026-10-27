#include "MusicPlayer.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <miniaudio.h>
#include "Music.hpp"

using namespace bibo::audio;

namespace
{
	const ma_uint32 SAMPLE_RATE = 44100;
	//Fading in and out (seconds from silence to full volume)
	const float FADE_TIME = 0.6f;
	//Volume of the sound effects
	const float EFFECTS_VOLUME = 0.8f;
	//At most this many sounds play at the same time (the oldest one stops)
	const std::size_t MAX_VOICES = 16;
}

struct MusicPlayer::Impl
{
	ma_device device;
	bool hasDevice = false;
	std::thread composer;
	//Written by the composer before ready is set, then only read by the audio thread
	std::vector<float> samples;
	std::size_t frames = 0;
	std::atomic<bool> ready{ false };
	std::atomic<bool> enabled{ true };
	std::atomic<float> volume{ 0.55f };
	//Audio thread only
	std::size_t position = 0;
	float gain = 0.0f;

	//Sound effects: synthesized by the composer before effectsReady is set, per effect and variant (mono)
	std::array<std::vector<std::vector<float>>, SOUND_EFFECT_COUNT> effects;
	std::atomic<bool> effectsReady{ false };
	std::atomic<bool> effectsEnabled{ true };
	struct Voice
	{
		const std::vector<float>* samples = nullptr;
		std::size_t position = 0;
		float gain = 1.0f;
	};
	//Sounds started by the game, taken over by the audio thread
	std::mutex pendingMutex;
	std::vector<Voice> pending;
	//Audio thread only
	std::vector<Voice> voices;

	//Called by miniaudio on its audio thread: the next frames of the loop and the sounds (the output is silent before)
	static void fill(ma_device* device, void* output, const void*, ma_uint32 frameCount)
	{
		Impl& self = *static_cast<Impl*>(device->pUserData);
		float* out = static_cast<float*>(output);
		if (self.ready.load(std::memory_order_acquire) && self.frames > 0)
		{
			float target = self.enabled.load(std::memory_order_relaxed) ? self.volume.load(std::memory_order_relaxed) : 0.0f;
			float step = 1.0f / (FADE_TIME * static_cast<float>(device->sampleRate));
			for (ma_uint32 i = 0; i < frameCount; ++i)
			{
				self.gain += std::clamp(target - self.gain, -step, step);
				out[2 * i] = self.samples[2 * self.position] * self.gain;
				out[2 * i + 1] = self.samples[2 * self.position + 1] * self.gain;
				self.position = (self.position + 1) % self.frames;
			}
		}
		if (!self.effectsReady.load(std::memory_order_acquire))
		{
			return;
		}
		{
			//Never waits for the game (it only holds the lock for a moment)
			std::unique_lock<std::mutex> lock(self.pendingMutex, std::try_to_lock);
			if (lock.owns_lock())
			{
				for (const Voice& voice : self.pending)
				{
					if (self.voices.size() >= MAX_VOICES)
					{
						self.voices.erase(self.voices.begin());
					}
					self.voices.push_back(voice);
				}
				self.pending.clear();
			}
		}
		bool effectsOn = self.effectsEnabled.load(std::memory_order_relaxed);
		for (Voice& voice : self.voices)
		{
			const std::vector<float>& sound = *voice.samples;
			for (ma_uint32 i = 0; i < frameCount && voice.position < sound.size(); ++i, ++voice.position)
			{
				if (effectsOn)
				{
					float value = sound[voice.position] * voice.gain;
					out[2 * i] = std::clamp(out[2 * i] + value, -1.0f, 1.0f);
					out[2 * i + 1] = std::clamp(out[2 * i + 1] + value, -1.0f, 1.0f);
				}
			}
		}
		std::erase_if(self.voices, [](const Voice& voice) { return voice.position >= voice.samples->size(); });
	}
};

MusicPlayer::MusicPlayer() : impl(std::make_unique<Impl>())
{
	impl->pending.reserve(MAX_VOICES);
	impl->voices.reserve(MAX_VOICES);
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_f32;
	config.playback.channels = 2;
	config.sampleRate = SAMPLE_RATE; //miniaudio converts it to the rate of the device
	config.dataCallback = Impl::fill;
	config.pUserData = impl.get();
	if (ma_device_init(nullptr, &config, &impl->device) != MA_SUCCESS)
	{
		std::cerr << "WARNING: No audio device, the game runs without music" << std::endl;
		return;
	}
	impl->hasDevice = true;
	if (ma_device_start(&impl->device) != MA_SUCCESS)
	{
		std::cerr << "WARNING: Could not start the audio device, the game runs without music" << std::endl;
		return;
	}
	//Synthesizing takes a moment, the game starts meanwhile
	Impl* player = impl.get();
	impl->composer = std::thread([player] {
		//The sounds first, they are quick
		for (int effect = 0; effect < SOUND_EFFECT_COUNT; ++effect)
		{
			for (int variant = 0; variant < soundEffectVariants(static_cast<SoundEffect>(effect)); ++variant)
			{
				player->effects[effect].push_back(synthesizeSoundEffect(static_cast<SoundEffect>(effect), variant, static_cast<int>(SAMPLE_RATE)));
			}
		}
		player->effectsReady.store(true, std::memory_order_release);
		Song song = composeIslandTune(static_cast<int>(SAMPLE_RATE));
		player->frames = song.frames();
		player->samples = std::move(song.samples);
		player->ready.store(true, std::memory_order_release);
	});
}

MusicPlayer::~MusicPlayer()
{
	if (impl->composer.joinable())
	{
		impl->composer.join();
	}
	if (impl->hasDevice)
	{
		ma_device_uninit(&impl->device); //stops the audio thread
	}
}

void MusicPlayer::setEnabled(bool enabled)
{
	impl->enabled.store(enabled, std::memory_order_relaxed);
}

void MusicPlayer::setVolume(float volume)
{
	impl->volume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

void MusicPlayer::play(SoundEffect effect, int variant, float volume)
{
	int index = static_cast<int>(effect);
	if (!impl->hasDevice || !impl->effectsReady.load(std::memory_order_acquire) || index < 0 || index >= SOUND_EFFECT_COUNT
		|| !impl->effectsEnabled.load(std::memory_order_relaxed))
	{
		return;
	}
	const std::vector<std::vector<float>>& variants = impl->effects[index];
	if (variants.empty())
	{
		return;
	}
	Impl::Voice voice;
	voice.samples = &variants[static_cast<std::size_t>(std::max(variant, 0)) % variants.size()];
	voice.gain = EFFECTS_VOLUME * std::clamp(volume, 0.0f, 1.0f);
	std::lock_guard<std::mutex> lock(impl->pendingMutex);
	if (impl->pending.size() < MAX_VOICES)
	{
		impl->pending.push_back(voice);
	}
}

void MusicPlayer::setEffectsEnabled(bool enabled)
{
	impl->effectsEnabled.store(enabled, std::memory_order_relaxed);
}

bool MusicPlayer::hasDevice() const
{
	return impl->hasDevice;
}

bool MusicPlayer::isPlaying() const
{
	return impl->hasDevice && impl->ready.load(std::memory_order_acquire);
}
