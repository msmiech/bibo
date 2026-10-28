#include "FoliageInteraction.hpp"

#include <algorithm>
#include <cmath>

namespace bibo::world {

namespace {
// A body leaves an imprint every time it moved this part of its radius
const float kImprintSpacing = 0.5f;
// Faster than this it was moved (e.g. back to the start), it did not walk
const float kTeleportSpeed = 40.0f;
// At this speed (m/s) the plants lean fully in the direction of the movement
const float kRunSpeed = 7.0f;
const std::size_t kMaxTrail = 96;

float HorizontalDistance(glm::vec3 a, glm::vec3 b) {
  return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
}
}  // namespace

float FoliageInteraction::Push(float age) {
  if (age < kHoldTime) {
    return 1.0f;
  }
  float t = age - kHoldTime;
  if (t >= kSpringTime) {
    return 0.0f;
  }
  // Damped swing: back past upright after about half a second, then it settles
  // (and fades out to exactly 0 at the end)
  return std::exp(-3.0f * t) * std::cos(6.0f * t) * (1.0f - t / kSpringTime);
}

void FoliageInteraction::Update(float time_delta,
                                const std::vector<Body>& bodies,
                                glm::vec3 camera) {
  for (Imprint& imprint : trail_) {
    imprint.age += time_delta;
  }
  std::erase_if(trail_, [](const Imprint& imprint) {
    return imprint.age >= kHoldTime + kSpringTime;
  });

  if (tracked_.size() != bodies.size()) {
    tracked_.assign(bodies.size(), Tracked());
  }
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    const Body& body = bodies[i];
    Tracked& track = tracked_[i];
    if (track.valid && time_delta > 0.0f) {
      glm::vec3 velocity = (body.center - track.position) / time_delta;
      if (glm::length(velocity) > kTeleportSpeed) {
        // The plants where it was spring back up
        Imprint imprint;
        imprint.center = track.position;
        imprint.radius = body.radius;
        trail_.push_back(imprint);
        track.valid = false;
      } else {
        glm::vec2 motion = glm::vec2(velocity.x, velocity.z) / kRunSpeed;
        float length = glm::length(motion);
        if (length > 1.0f) {
          motion /= length;
        }
        // Smoothed, the position of an animal jitters a bit from frame to frame
        track.motion =
            glm::mix(track.motion, motion, std::min(1.0f, time_delta * 8.0f));
      }
    }
    if (!track.valid) {
      track.last_imprint = body.center;
      track.motion = glm::vec2(0.0f);
      track.valid = true;
    }
    track.position = body.center;
    if (HorizontalDistance(body.center, track.last_imprint) >
            kImprintSpacing * body.radius ||
        std::abs(body.center.y - track.last_imprint.y) > body.radius) {
      Imprint imprint;
      imprint.center = track.last_imprint;
      imprint.radius = body.radius;
      imprint.motion = track.motion;
      trail_.push_back(imprint);
      track.last_imprint = body.center;
    }
  }
  if (trail_.size() > kMaxTrail) {
    trail_.erase(trail_.begin(),
                 trail_.begin() +
                     static_cast<std::ptrdiff_t>(trail_.size() - kMaxTrail));
  }

  // The bodies first, then the imprints that still push, the closest to the
  // camera first
  selected_.clear();
  std::vector<std::pair<float, Imprint>> candidates;
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    float distance =
        HorizontalDistance(bodies[i].center, camera) - bodies[i].radius;
    if (distance < kRange) {
      Imprint imprint;
      imprint.center = bodies[i].center;
      imprint.radius = bodies[i].radius;
      imprint.motion = tracked_[i].motion;
      imprint.age = -1.0f;
      candidates.push_back({distance - 1e6f, imprint});
    }
  }
  for (const Imprint& imprint : trail_) {
    float distance =
        HorizontalDistance(imprint.center, camera) - imprint.radius;
    if (distance < kRange && Push(imprint.age) != 0.0f) {
      candidates.push_back({distance + 4.0f * imprint.age, imprint});
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  for (std::size_t i = 0;
       i < candidates.size() &&
       i < static_cast<std::size_t>(render::kMaxFoliageInteractors);
       ++i) {
    selected_.push_back(candidates[i].second);
  }
}

void FoliageInteraction::FillFrameUniforms(
    render::FrameUniforms& uniforms) const {
  for (std::size_t i = 0; i < selected_.size(); ++i) {
    const Imprint& imprint = selected_[i];
    uniforms.foliage_interactors[i] = glm::vec4(imprint.center, imprint.radius);
    uniforms.foliage_pushes[i] =
        glm::vec4(Push(imprint.age), imprint.motion, 0.0f);
  }
  uniforms.wind.w = static_cast<float>(selected_.size());
}

}  // namespace bibo::world
