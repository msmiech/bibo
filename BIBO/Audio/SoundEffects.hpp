#pragma once
#include <vector>

namespace bibo
{
	namespace audio
	{
		/**
		* The sounds of the game besides the music
		*/
		enum class SoundEffect
		{
			BEAVER, //a short squeaky, nasal chatter of the beaver while it talks (several variants)
			COUNTDOWN, //beep of 3, 2, 1
			GO, //the higher, longer beep at the start
			CHECKPOINT, //a bright ding
			SUCCESS, //a cheerful jingle when a quest is completed
			FAILURE, //a sad "wah wah" when it failed
			SPLASH, //the bear falls into the water
			COUNT
		};
		constexpr int SOUND_EFFECT_COUNT = static_cast<int>(SoundEffect::COUNT);

		/**
		* @return How many different versions of the sound there are (e.g. the beaver does not always make the same sound)
		*/
		int soundEffectVariants(SoundEffect effect);
		/**
		* Synthesizes a sound (no sound files), the same every time
		* @param variant 0 to soundEffectVariants - 1
		* @return Mono samples between -1 and 1
		*/
		std::vector<float> synthesizeSoundEffect(SoundEffect effect, int variant, int sampleRate);
	}
}
