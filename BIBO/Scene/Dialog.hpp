#ifndef BIBO_SCENE_DIALOG_HPP_
#define BIBO_SCENE_DIALOG_HPP_
#include <string>
#include <vector>

namespace bibo {
namespace scene {
/**
 * What the player does in a conversation, pressed in this frame
 */
struct DialogInput {
  // next page, or the selected answer (E, Enter, Space, A on a gamepad)
  bool confirm = false;
  int navigate = 0;     //-1: previous answer, 1: next answer
  bool cancel = false;  // ends the conversation (the cancel answer, e.g. "No")
};

/**
 * A conversation with an animal: what it says appears letter by letter on the
 * screen, page by page, the player goes on with confirm. At the end the player
 * may have to choose an answer. Animals do not talk like people: while the text
 * appears the speaker makes its sounds (TakeSounds, e.g. the chatter of a
 * beaver), the text is only on the screen. A single line can also be said in
 * passing (say): it goes away on its own.
 */
class Dialog {
 public:
  // How fast the text appears
  static constexpr float kCharactersPerSecond = 40.0f;
  // The speaker makes a sound this often while the text appears (not in the
  // pauses after punctuation)
  static constexpr float kSoundInterval = 0.17f;
  // Pause after the end of a sentence and after a comma
  static constexpr float kSentencePause = 0.3f, kCommaPause = 0.12f;
  // A line said in passing stays this long after it appeared
  static constexpr float kLineHoldTime = 1.8f;

  /**
   * Starts a conversation
   * @param choices Answers the player chooses from after the last page (none:
   * confirm closes it)
   * @param cancelChoice The answer that cancel chooses (-1: the last one)
   */
  void Open(const std::string& speaker, const std::vector<std::string>& pages,
            const std::vector<std::string>& choices = {},
            int cancel_choice = -1);
  /**
   * A line said in passing: it appears like a page and goes away on its own,
   * the player cannot do anything
   */
  void Say(const std::string& speaker, const std::string& line);
  void Close();
  void Update(float time_delta, const DialogInput& input);

  bool IsOpen() const { return active_; }
  /**
   * @return true for a line said in passing (say)
   */
  bool IsPassing() const { return passing_; }
  const std::string& GetSpeaker() const { return speaker_; }
  /**
   * @return The part of the page that has appeared so far
   */
  std::string GetText() const;
  /**
   * @return The whole page (e.g. to reserve its space on the screen)
   */
  const std::string& GetPage() const;
  int GetPageIndex() const { return page_; }
  int GetPageCount() const { return static_cast<int>(pages_.size()); }
  /**
   * @return true while the text of the page appears
   */
  bool IsTyping() const;
  /**
   * @return true when the page is complete and the player can go on (a next
   * page or closing, not an answer)
   */
  bool CanContinue() const {
    return active_ && !passing_ && !IsTyping() && !IsChoosing();
  }
  /**
   * @return true when the last page is complete and the player chooses an
   * answer
   */
  bool IsChoosing() const;
  const std::vector<std::string>& GetChoices() const { return choices_; }
  int GetSelectedChoice() const { return selected_; }

  /**
   * @return Number of sounds the speaker made since the last call
   */
  int TakeSounds();
  /**
   * @param choice The answer the player chose, -1 if none (no choices, or
   * closed with close)
   * @return true once after the conversation ended
   */
  bool TakeEnded(int& choice);

 private:
  void StartPage(int index);
  void End(int choice);

  std::string speaker_;
  std::vector<std::string> pages_;
  std::vector<std::string> choices_;
  bool active_ = false;
  bool passing_ = false;
  int page_ = 0;
  float shown_ = 0.0f;  // characters of the page that appeared
  float pause_ = 0.0f;  // after punctuation
  float sound_timer_ = 0.0f;
  float hold_time_ = 0.0f;  // of a line in passing after it appeared
  int selected_ = 0;
  int cancel_choice_ = -1;
  int sounds_ = 0;
  bool ended_ = false;
  int end_choice_ = -1;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_DIALOG_HPP_
