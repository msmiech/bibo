#include "MusicPlayer.hpp"
#include <algorithm>
#include <atomic>
#include <iostream>
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

	//Called by miniaudio on its audio thread: the next frames of the loop (the output is silent before)
	static void fill(ma_device* device, void* output, const void*, ma_uint32 frameCount)
	{
		Impl& self = *static_cast<Impl*>(device->pUserData);
		if (!self.ready.load(std::memory_order_acquire) || self.frames == 0)
		{
			return;
		}
		float* out = static_cast<float*>(output);
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
};

MusicPlayer::MusicPlayer() : impl(std::make_unique<Impl>())
{
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

bool MusicPlayer::hasDevice() const
{
	return impl->hasDevice;
}

bool MusicPlayer::isPlaying() const
{
	return impl->hasDevice && impl->ready.load(std::memory_order_acquire);
}
