#include "Dialog.hpp"

#include <algorithm>
#include <cctype>

namespace bibo::scene {

namespace {
const std::string kNoPage;

bool IsLetter(char character) {
  return std::isalnum(static_cast<unsigned char>(character)) != 0;
}
}  // namespace

void Dialog::Open(const std::string& speaker,
                  const std::vector<std::string>& pages,
                  const std::vector<std::string>& choices, int cancel_choice) {
  this->speaker_ = speaker;
  this->pages_ = pages.empty() ? std::vector<std::string>{""} : pages;
  this->choices_ = choices;
  this->cancel_choice_ =
      cancel_choice >= 0 && cancel_choice < static_cast<int>(choices.size())
          ? cancel_choice
          : static_cast<int>(choices.size()) - 1;
  active_ = true;
  passing_ = false;
  selected_ = 0;
  ended_ = false;
  end_choice_ = -1;
  StartPage(0);
}

void Dialog::Say(const std::string& speaker, const std::string& line) {
  Open(speaker, {line});
  passing_ = true;
}

void Dialog::Close() {
  if (active_) {
    End(-1);
  }
}

void Dialog::StartPage(int index) {
  page_ = index;
  shown_ = 0.0f;
  pause_ = 0.0f;
  sound_timer_ = 0.0f;  // the first sound right away
  hold_time_ = 0.0f;
}

void Dialog::End(int choice) {
  active_ = false;
  passing_ = false;
  ended_ = true;
  end_choice_ = choice;
}

const std::string& Dialog::GetPage() const {
  return active_ ? pages_[page_] : kNoPage;
}

std::string Dialog::GetText() const {
  const std::string& text = GetPage();
  return text.substr(0,
                     std::min(text.size(), static_cast<std::size_t>(shown_)));
}

bool Dialog::IsTyping() const {
  return active_ && static_cast<std::size_t>(shown_) < GetPage().size();
}

bool Dialog::IsChoosing() const {
  return active_ && !passing_ && !choices_.empty() &&
         page_ + 1 == GetPageCount() && !IsTyping();
}

void Dialog::Update(float time_delta, const DialogInput& input) {
  if (!active_) {
    return;
  }
  const std::string& text = GetPage();
  if (IsTyping()) {
    if (input.confirm && !passing_) {
      shown_ = static_cast<float>(text.size());  // the whole page at once
      return;
    }
    // The text appears letter by letter, with a pause after the punctuation,
    // the speaker makes a sound now and then
    float time = time_delta;
    while (time > 0.0f && IsTyping()) {
      if (pause_ > 0.0f) {
        float used = std::min(pause_, time);
        pause_ -= used;
        time -= used;
        continue;
      }
      std::size_t before = static_cast<std::size_t>(shown_);
      float next = static_cast<float>(before + 1);
      float needed = (next - shown_) / kCharactersPerSecond;
      if (needed > time) {
        shown_ += time * kCharactersPerSecond;
        sound_timer_ -= time;
        time = 0.0f;
      } else {
        shown_ = next;
        sound_timer_ -= needed;
        time -= needed;
        char character = text[before];
        bool last_of_sentence =
            before + 1 >= text.size() || text[before + 1] == ' ';
        if ((character == '.' || character == '!' || character == '?') &&
            last_of_sentence) {
          pause_ = kSentencePause;
        } else if (character == ',' || character == ';' || character == ':') {
          pause_ = kCommaPause;
        }
      }
      if (sound_timer_ <= 0.0f && pause_ <= 0.0f &&
          IsLetter(
              text[static_cast<std::size_t>(std::max(shown_ - 1.0f, 0.0f))])) {
        ++sounds_;
        sound_timer_ = kSoundInterval;
      }
    }
    return;
  }

  if (passing_) {
    hold_time_ += time_delta;
    if (hold_time_ >= kLineHoldTime) {
      End(-1);
    }
    return;
  }
  if (IsChoosing()) {
    int count = static_cast<int>(choices_.size());
    if (input.navigate != 0) {
      selected_ = (selected_ + input.navigate % count + count) % count;
    }
    if (input.cancel) {
      End(cancel_choice_);
    } else if (input.confirm) {
      End(selected_);
    }
    return;
  }
  if (input.cancel) {
    End(-1);
  } else if (input.confirm) {
    if (page_ + 1 < GetPageCount()) {
      StartPage(page_ + 1);
    } else {
      End(-1);
    }
  }
}

int Dialog::TakeSounds() {
  int result = sounds_;
  sounds_ = 0;
  return result;
}

bool Dialog::TakeEnded(int& choice) {
  if (!ended_) {
    return false;
  }
  ended_ = false;
  choice = end_choice_;
  return true;
}

}  // namespace bibo::scene
