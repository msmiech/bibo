#include "FoliageInteraction.hpp"

#include <algorithm>
#include <cmath>

using namespace bibo;
using namespace bibo::world;

namespace {
// A body leaves an imprint every time it moved this part of its radius
const float IMPRINT_SPACING = 0.5f;
// Faster than this it was moved (e.g. back to the start), it did not walk
const float TELEPORT_SPEED = 40.0f;
// At this speed (m/s) the plants lean fully in the direction of the movement
const float RUN_SPEED = 7.0f;
const std::size_t MAX_TRAIL = 96;

float horizontalDistance(glm::vec3 a, glm::vec3 b) {
  return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
}
}  // namespace

float FoliageInteraction::push(float age) {
  if (age < HOLD_TIME) {
    return 1.0f;
  }
  float t = age - HOLD_TIME;
  if (t >= SPRING_TIME) {
    return 0.0f;
  }
  // Damped swing: back past upright after about half a second, then it settles
  // (and fades out to exactly 0 at the end)
  return std::exp(-3.0f * t) * std::cos(6.0f * t) * (1.0f - t / SPRING_TIME);
}

void FoliageInteraction::update(float timeDelta,
                                const std::vector<Body>& bodies,
                                glm::vec3 camera) {
  for (Imprint& imprint : trail) {
    imprint.age += timeDelta;
  }
  std::erase_if(trail, [](const Imprint& imprint) {
    return imprint.age >= HOLD_TIME + SPRING_TIME;
  });

  if (tracked.size() != bodies.size()) {
    tracked.assign(bodies.size(), Tracked());
  }
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    const Body& body = bodies[i];
    Tracked& track = tracked[i];
    if (track.valid && timeDelta > 0.0f) {
      glm::vec3 velocity = (body.center - track.position) / timeDelta;
      if (glm::length(velocity) > TELEPORT_SPEED) {
        // The plants where it was spring back up
        Imprint imprint;
        imprint.center = track.position;
        imprint.radius = body.radius;
        trail.push_back(imprint);
        track.valid = false;
      } else {
        glm::vec2 motion = glm::vec2(velocity.x, velocity.z) / RUN_SPEED;
        float length = glm::length(motion);
        if (length > 1.0f) {
          motion /= length;
        }
        // Smoothed, the position of an animal jitters a bit from frame to frame
        track.motion =
            glm::mix(track.motion, motion, std::min(1.0f, timeDelta * 8.0f));
      }
    }
    if (!track.valid) {
      track.lastImprint = body.center;
      track.motion = glm::vec2(0.0f);
      track.valid = true;
    }
    track.position = body.center;
    if (horizontalDistance(body.center, track.lastImprint) >
            IMPRINT_SPACING * body.radius ||
        std::abs(body.center.y - track.lastImprint.y) > body.radius) {
      Imprint imprint;
      imprint.center = track.lastImprint;
      imprint.radius = body.radius;
      imprint.motion = track.motion;
      trail.push_back(imprint);
      track.lastImprint = body.center;
    }
  }
  if (trail.size() > MAX_TRAIL) {
    trail.erase(trail.begin(), trail.begin() + static_cast<std::ptrdiff_t>(
                                                   trail.size() - MAX_TRAIL));
  }

  // The bodies first, then the imprints that still push, the closest to the
  // camera first
  selected.clear();
  std::vector<std::pair<float, Imprint>> candidates;
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    float distance =
        horizontalDistance(bodies[i].center, camera) - bodies[i].radius;
    if (distance < RANGE) {
      Imprint imprint;
      imprint.center = bodies[i].center;
      imprint.radius = bodies[i].radius;
      imprint.motion = tracked[i].motion;
      imprint.age = -1.0f;
      candidates.push_back({distance - 1e6f, imprint});
    }
  }
  for (const Imprint& imprint : trail) {
    float distance =
        horizontalDistance(imprint.center, camera) - imprint.radius;
    if (distance < RANGE && push(imprint.age) != 0.0f) {
      candidates.push_back({distance + 4.0f * imprint.age, imprint});
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  for (std::size_t i = 0;
       i < candidates.size() &&
       i < static_cast<std::size_t>(render::MAX_FOLIAGE_INTERACTORS);
       ++i) {
    selected.push_back(candidates[i].second);
  }
}

void FoliageInteraction::fillFrameUniforms(
    render::FrameUniforms& uniforms) const {
  for (std::size_t i = 0; i < selected.size(); ++i) {
    const Imprint& imprint = selected[i];
    uniforms.foliageInteractors[i] = glm::vec4(imprint.center, imprint.radius);
    uniforms.foliagePushes[i] =
        glm::vec4(push(imprint.age), imprint.motion, 0.0f);
  }
  uniforms.wind.w = static_cast<float>(selected.size());
}
