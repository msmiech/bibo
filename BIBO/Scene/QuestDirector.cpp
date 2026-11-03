#include "QuestDirector.hpp"

#include <limits>

namespace bibo::scene {

using glm::vec2;
using glm::vec3;
using std::size_t;
using std::vector;

bool QuestDirector::IsBusy(size_t entry) const {
  for (size_t other = 0; other < entries_.size(); ++other) {
    if (other != entry && entries_[other].quest->IsActive()) {
      return true;
    }
  }
  return false;
}

void QuestDirector::ChooseTalkPartner(vec3 bear) {
  // In a conversation: the one it talks to
  for (size_t i = 0; i < entries_.size(); ++i) {
    int partner = entries_[i].quest->GetConversationPartner();
    if (partner >= 0) {
      talk_entry_ = static_cast<int>(i);
      talk_partner_ = partner;
      return;
    }
  }
  // Otherwise the closest one it could talk to
  talk_entry_ = talk_partner_ = -1;
  float closest = std::numeric_limits<float>::max();
  for (size_t i = 0; i < entries_.size(); ++i) {
    const Entry& entry = entries_[i];
    for (size_t p = 0; p < entry.partners.size(); ++p) {
      vec3 position = entry.partners[p].who->GetPosition();
      float away = length(vec2(position.x - bear.x, position.z - bear.z));
      if (entry.quest->CanTalkTo(static_cast<int>(p)) && away < closest) {
        closest = away;
        talk_entry_ = static_cast<int>(i);
        talk_partner_ = static_cast<int>(p);
      }
    }
  }
}

QuestDirector::News QuestDirector::Update(float time_delta, vec3 bear,
                                          const input::PlayerInput& player,
                                          EventQueue& events) {
  // The button starts a conversation with the closest partner only
  ChooseTalkPartner(bear);
  News news;
  for (size_t i = 0; i < entries_.size(); ++i) {
    Entry& entry = entries_[i];
    QuestSituation common;
    common.busy = IsBusy(i);
    common.interact = player.interact && talk_entry_ == static_cast<int>(i);
    common.dialog.confirm = player.confirm;
    common.dialog.navigate = player.navigate;
    common.dialog.cancel = player.cancel;
    vector<bool> near;
    for (const Partner& partner : entry.partners) {
      near.push_back(
          can_talk_at_(partner.who->GetPosition(), partner.talk_distance));
    }
    TalkPartner& giver = *entry.partners.front().who;
    for (const Quest::Reaction& reaction :
         entry.run(time_delta, common, near, events)) {
      if (reaction.speak) {
        giver.Speak();
      }
      if (reaction.cheer) {
        giver.Cheer();
      }
      news.celebrate = news.celebrate || reaction.celebrate;
      news.completed = news.completed || reaction.completed;
    }
    const Dialog& dialog = entry.quest->GetDialog();
    giver.SetTalking(dialog.IsOpen() &&
                     dialog.GetSpeaker() == entry.quest->GetSpeaker());
  }
  return news;
}

void QuestDirector::Abort() {
  for (Entry& entry : entries_) {
    entry.quest->Abort();
  }
  talk_entry_ = talk_partner_ = -1;
}

bool QuestDirector::IsPlayerInControl() const {
  for (const Entry& entry : entries_) {
    if (!entry.quest->IsPlayerInControl()) {
      return false;
    }
  }
  return true;
}

bool QuestDirector::IsInConversation() const {
  for (const Entry& entry : entries_) {
    if (entry.quest->GetConversationPartner() >= 0) {
      return true;
    }
  }
  return false;
}

bool QuestDirector::IsCheeredOn() const {
  for (const Entry& entry : entries_) {
    if (entry.quest->IsCheeredOn()) {
      return true;
    }
  }
  return false;
}

const QuestDirector::Partner* QuestDirector::GetTalkPartner() const {
  if (talk_entry_ < 0) {
    return nullptr;
  }
  return &entries_[talk_entry_].partners[talk_partner_];
}

const QuestDirector::Partner* QuestDirector::GetConversationPartner() const {
  for (const Entry& entry : entries_) {
    int partner = entry.quest->GetConversationPartner();
    if (partner >= 0) {
      return &entry.partners[partner];
    }
  }
  return nullptr;
}

std::string QuestDirector::GetTalkPrompt() const {
  if (talk_entry_ < 0) {
    return "";
  }
  return entries_[talk_entry_].quest->GetPrompt(talk_partner_);
}

const Dialog& QuestDirector::GetShownDialog() const {
  for (const Entry& entry : entries_) {
    const Dialog& dialog = entry.quest->GetDialog();
    if (dialog.IsOpen() && !dialog.IsPassing()) {
      return dialog;
    }
  }
  for (const Entry& entry : entries_) {
    const Dialog& dialog = entry.quest->GetDialog();
    if (dialog.IsOpen()) {
      return dialog;
    }
  }
  return entries_.front().quest->GetDialog();
}

vector<QuestDirector::MarkerSpot> QuestDirector::GetMarkers() const {
  vector<MarkerSpot> markers;
  for (const Entry& entry : entries_) {
    for (size_t p = 0; p < entry.partners.size(); ++p) {
      Quest::Marker marker = entry.quest->GetMarker(static_cast<int>(p));
      if (marker != Quest::Marker::kNone) {
        markers.push_back({entry.partners[p].who, marker});
      }
    }
  }
  return markers;
}

}  // namespace bibo::scene
