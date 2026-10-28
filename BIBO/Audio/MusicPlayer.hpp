#ifndef BIBO_AUDIO_MUSICPLAYER_HPP_
#define BIBO_AUDIO_MUSICPLAYER_HPP_
#include <memory>

#include "SoundEffects.hpp"

namespace bibo {
namespace audio {
/**
 * Plays the music of the island (ComposeIslandTune) in a loop and the sound
 * effects on the default audio device (based on miniaudio). The music and the
 * sounds are synthesized by a background thread, they start when they are
 * ready. Without an audio device (e.g. on a server) the game runs silently.
 */
class MusicPlayer {
 public:
  MusicPlayer();
  ~MusicPlayer();
  MusicPlayer(const MusicPlayer&) = delete;
  MusicPlayer& operator=(const MusicPlayer&) = delete;

  /**
   * Fades the music in or out (it keeps playing silently, so it goes on where
   * it was)
   */
  void SetEnabled(bool enabled);
  /**
   * @param volume 0 to 1
   */
  void SetVolume(float volume);
  /**
   * Plays a sound once, on top of the music (ignored while the sounds are not
   * synthesized yet)
   * @param variant See SoundEffectVariants (wraps around)
   * @param volume 0 to 1
   */
  void Play(SoundEffect effect, int variant = 0, float volume = 1.0f);
  /**
   * Switches the sound effects on or off (the music is switched with
   * SetEnabled)
   */
  void SetEffectsEnabled(bool enabled);
  /**
   * @return false if there is no audio device
   */
  bool HasDevice() const;
  /**
   * @return true when the music was synthesized and is playing (also while it
   * is faded out)
   */
  bool IsPlaying() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace audio
}  // namespace bibo

#endif  // BIBO_AUDIO_MUSICPLAYER_HPP_
