#pragma once
#include <memory>

namespace bibo
{
	namespace audio
	{
		/**
		* Plays the music of the race (composeIslandTune) in a loop on the default audio device (based on miniaudio).
		* The music is synthesized by a background thread, it starts when it is ready. Without an audio device
		* (e.g. on a server) the game runs silently.
		*/
		class MusicPlayer
		{
		public:
			MusicPlayer();
			~MusicPlayer();
			MusicPlayer(const MusicPlayer&) = delete;
			MusicPlayer& operator=(const MusicPlayer&) = delete;

			/**
			* Fades the music in or out (it keeps playing silently, so it goes on where it was)
			*/
			void setEnabled(bool enabled);
			/**
			* @param volume 0 to 1
			*/
			void setVolume(float volume);
			/**
			* @return false if there is no audio device
			*/
			bool hasDevice() const;
			/**
			* @return true when the music was synthesized and is playing (also while it is faded out)
			*/
			bool isPlaying() const;

		private:
			struct Impl;
			std::unique_ptr<Impl> impl;
		};
	}
}
