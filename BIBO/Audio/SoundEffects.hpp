#ifndef BIBO_AUDIO_SOUNDEFFECTS_HPP_
#define BIBO_AUDIO_SOUNDEFFECTS_HPP_
#include <vector>

namespace bibo {
namespace audio {
/**
 * The sounds of the game besides the music
 */
enum class SoundEffect {
  // a short squeaky, nasal chatter of the beaver while it talks (several
  // variants)
  kBeaver,
  // a low, warm grumble of the bear at the pool while it talks (several
  // variants)
  kBear,
  // a high chitter and trill of a raccoon while it talks or cheers (several
  // variants)
  kRaccoon,
  kCountdown,   // beep of 3, 2, 1
  kGo,          // the higher, longer beep at the start
  kCheckpoint,  // a bright ding
  kSuccess,     // a cheerful jingle when a quest is completed
  kFailure,     // a sad "wah wah" when it failed
  kSplash,      // the bear falls into the water
  kCount
};
constexpr int kSoundEffectCount = static_cast<int>(SoundEffect::kCount);

/**
 * @return How many different versions of the sound there are (e.g. the beaver
 * does not always make the same sound)
 */
int SoundEffectVariants(SoundEffect effect);
/**
 * Synthesizes a sound (no sound files), the same every time
 * @param variant 0 to SoundEffectVariants - 1
 * @return Mono samples between -1 and 1
 */
std::vector<float> SynthesizeSoundEffect(SoundEffect effect, int variant,
                                         int sample_rate);
}  // namespace audio
}  // namespace bibo

#endif  // BIBO_AUDIO_SOUNDEFFECTS_HPP_
