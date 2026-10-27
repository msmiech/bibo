#include "Dialog.hpp"

#include <algorithm>
#include <cctype>

using namespace bibo;
using namespace bibo::scene;

namespace {
const std::string NO_PAGE;

bool isLetter(char character) {
  return std::isalnum(static_cast<unsigned char>(character)) != 0;
}
}  // namespace

void Dialog::open(const std::string& speaker,
                  const std::vector<std::string>& pages,
                  const std::vector<std::string>& choices, int cancelChoice) {
  this->speaker = speaker;
  this->pages = pages.empty() ? std::vector<std::string>{""} : pages;
  this->choices = choices;
  this->cancelChoice =
      cancelChoice >= 0 && cancelChoice < static_cast<int>(choices.size())
          ? cancelChoice
          : static_cast<int>(choices.size()) - 1;
  active = true;
  passing = false;
  selected = 0;
  ended = false;
  endChoice = -1;
  startPage(0);
}

void Dialog::say(const std::string& speaker, const std::string& line) {
  open(speaker, {line});
  passing = true;
}

void Dialog::close() {
  if (active) {
    end(-1);
  }
}

void Dialog::startPage(int index) {
  page = index;
  shown = 0.0f;
  pause = 0.0f;
  soundTimer = 0.0f;  // the first sound right away
  holdTime = 0.0f;
}

void Dialog::end(int choice) {
  active = false;
  passing = false;
  ended = true;
  endChoice = choice;
}

const std::string& Dialog::getPage() const {
  return active ? pages[page] : NO_PAGE;
}

std::string Dialog::getText() const {
  const std::string& text = getPage();
  return text.substr(0, std::min(text.size(), static_cast<std::size_t>(shown)));
}

bool Dialog::isTyping() const {
  return active && static_cast<std::size_t>(shown) < getPage().size();
}

bool Dialog::isChoosing() const {
  return active && !passing && !choices.empty() && page + 1 == getPageCount() &&
         !isTyping();
}

void Dialog::update(float timeDelta, const DialogInput& input) {
  if (!active) {
    return;
  }
  const std::string& text = getPage();
  if (isTyping()) {
    if (input.confirm && !passing) {
      shown = static_cast<float>(text.size());  // the whole page at once
      return;
    }
    // The text appears letter by letter, with a pause after the punctuation,
    // the speaker makes a sound now and then
    float time = timeDelta;
    while (time > 0.0f && isTyping()) {
      if (pause > 0.0f) {
        float used = std::min(pause, time);
        pause -= used;
        time -= used;
        continue;
      }
      std::size_t before = static_cast<std::size_t>(shown);
      float next = static_cast<float>(before + 1);
      float needed = (next - shown) / CHARACTERS_PER_SECOND;
      if (needed > time) {
        shown += time * CHARACTERS_PER_SECOND;
        soundTimer -= time;
        time = 0.0f;
      } else {
        shown = next;
        soundTimer -= needed;
        time -= needed;
        char character = text[before];
        bool lastOfSentence =
            before + 1 >= text.size() || text[before + 1] == ' ';
        if ((character == '.' || character == '!' || character == '?') &&
            lastOfSentence) {
          pause = SENTENCE_PAUSE;
        } else if (character == ',' || character == ';' || character == ':') {
          pause = COMMA_PAUSE;
        }
      }
      if (soundTimer <= 0.0f && pause <= 0.0f &&
          isLetter(
              text[static_cast<std::size_t>(std::max(shown - 1.0f, 0.0f))])) {
        ++sounds;
        soundTimer = SOUND_INTERVAL;
      }
    }
    return;
  }

  if (passing) {
    holdTime += timeDelta;
    if (holdTime >= LINE_HOLD_TIME) {
      end(-1);
    }
    return;
  }
  if (isChoosing()) {
    int count = static_cast<int>(choices.size());
    if (input.navigate != 0) {
      selected = (selected + input.navigate % count + count) % count;
    }
    if (input.cancel) {
      end(cancelChoice);
    } else if (input.confirm) {
      end(selected);
    }
    return;
  }
  if (input.cancel) {
    end(-1);
  } else if (input.confirm) {
    if (page + 1 < getPageCount()) {
      startPage(page + 1);
    } else {
      end(-1);
    }
  }
}

int Dialog::takeSounds() {
  int result = sounds;
  sounds = 0;
  return result;
}

bool Dialog::takeEnded(int& choice) {
  if (!ended) {
    return false;
  }
  ended = false;
  choice = endChoice;
  return true;
}
