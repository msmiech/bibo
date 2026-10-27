#pragma once
#include <string>
#include <vector>

namespace bibo {
namespace scene {
/**
 * What the player does in a conversation, pressed in this frame
 */
struct DialogInput {
  bool confirm = false;  // next page, or the selected answer (E, Enter, Space,
                         // A on a gamepad)
  int navigate = 0;      //-1: previous answer, 1: next answer
  bool cancel = false;   // ends the conversation (the cancel answer, e.g. "No")
};

/**
 * A conversation with an animal: what it says appears letter by letter on the
 * screen, page by page, the player goes on with confirm. At the end the player
 * may have to choose an answer. Animals do not talk like people: while the text
 * appears the speaker makes its sounds (takeSounds, e.g. the chatter of a
 * beaver), the text is only on the screen. A single line can also be said in
 * passing (say): it goes away on its own.
 */
class Dialog {
 public:
  // How fast the text appears
  static constexpr float CHARACTERS_PER_SECOND = 40.0f;
  // The speaker makes a sound this often while the text appears (not in the
  // pauses after punctuation)
  static constexpr float SOUND_INTERVAL = 0.17f;
  // Pause after the end of a sentence and after a comma
  static constexpr float SENTENCE_PAUSE = 0.3f, COMMA_PAUSE = 0.12f;
  // A line said in passing stays this long after it appeared
  static constexpr float LINE_HOLD_TIME = 1.8f;

  /**
   * Starts a conversation
   * @param choices Answers the player chooses from after the last page (none:
   * confirm closes it)
   * @param cancelChoice The answer that cancel chooses (-1: the last one)
   */
  void open(const std::string& speaker, const std::vector<std::string>& pages,
            const std::vector<std::string>& choices = {},
            int cancelChoice = -1);
  /**
   * A line said in passing: it appears like a page and goes away on its own,
   * the player cannot do anything
   */
  void say(const std::string& speaker, const std::string& line);
  void close();
  void update(float timeDelta, const DialogInput& input);

  bool isOpen() const { return active; }
  /**
   * @return true for a line said in passing (say)
   */
  bool isPassing() const { return passing; }
  const std::string& getSpeaker() const { return speaker; }
  /**
   * @return The part of the page that has appeared so far
   */
  std::string getText() const;
  /**
   * @return The whole page (e.g. to reserve its space on the screen)
   */
  const std::string& getPage() const;
  int getPageIndex() const { return page; }
  int getPageCount() const { return static_cast<int>(pages.size()); }
  /**
   * @return true while the text of the page appears
   */
  bool isTyping() const;
  /**
   * @return true when the page is complete and the player can go on (a next
   * page or closing, not an answer)
   */
  bool canContinue() const {
    return active && !passing && !isTyping() && !isChoosing();
  }
  /**
   * @return true when the last page is complete and the player chooses an
   * answer
   */
  bool isChoosing() const;
  const std::vector<std::string>& getChoices() const { return choices; }
  int getSelectedChoice() const { return selected; }

  /**
   * @return Number of sounds the speaker made since the last call
   */
  int takeSounds();
  /**
   * @param choice The answer the player chose, -1 if none (no choices, or
   * closed with close)
   * @return true once after the conversation ended
   */
  bool takeEnded(int& choice);

 private:
  void startPage(int index);
  void end(int choice);

  std::string speaker;
  std::vector<std::string> pages;
  std::vector<std::string> choices;
  bool active = false;
  bool passing = false;
  int page = 0;
  float shown = 0.0f;  // characters of the page that appeared
  float pause = 0.0f;  // after punctuation
  float soundTimer = 0.0f;
  float holdTime = 0.0f;  // of a line in passing after it appeared
  int selected = 0;
  int cancelChoice = -1;
  int sounds = 0;
  bool ended = false;
  int endChoice = -1;
};
}  // namespace scene
}  // namespace bibo
