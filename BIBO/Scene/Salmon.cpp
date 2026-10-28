#include "Salmon.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <limits>

#include "../World/WorldGenerator.hpp"

namespace bibo::scene {

namespace {
const float kPi = glm::pi<float>();
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);
// Water that is shallower is not a pool (e.g. a wet edge), a salmon needs this
// much
const float kPoolDepth = 0.05f;
const float kSalmonDepth = 0.6f;
// The salmon come out of the water from a bit below the surface
const float kEmergeDepth = 0.3f;
// At most this many salmon of a pool are in the air at the same time
const int kMaxJumpsPerPool = 2;
// The points of the water along a sample of the track are this far apart
// (meters across the track)
const float kWaterStep = 0.75f;
// A place on the shore: this high above the water, this flat (height difference
// within kShoreRadius), this far from it
const float kShoreMinHeight = 0.15f, kShoreMaxHeight = 4.0f,
            kShoreFlatness = 0.35f, kShoreRadius = 1.6f;
const float kShoreMinWaterDistance = 1.6f, kShoreMaxWaterDistance = 8.0f;
// It may also lie a bit beyond the floor of the track (at the foot of the
// walls), and keeps this far from the route of runners
const float kShoreBeyondFloor = 4.0f, kShoreRouteClearance = 6.0f;
// Steps across the floor when looking for a way around the water
const float kRouteStep = 0.5f;
// The route is smoothed over this many points before and after (about meters),
// runners cut corners
const std::size_t kRouteSmoothing = 4;

bool IsClosed(const std::vector<bool>& closed, std::size_t pool) {
  return pool < closed.size() && closed[pool];
}
}  // namespace

std::vector<WaterPool> FindWaterPools(const world::WorldGenerator& generator) {
  std::vector<WaterPool> pools;
  const float sea = generator.GetSeaLevel();
  const std::vector<world::TrackPath::Sample>& samples =
      generator.GetPath().GetSamples();
  std::vector<glm::vec2> water, deep;
  float distance_sum = 0.0f;
  int wet_samples = 0, dry_run = 0;
  auto finish = [&]() {
    if (!water.empty()) {
      WaterPool pool;
      glm::vec2 sum(0.0f);
      for (glm::vec2 point : water) {
        sum += point;
      }
      glm::vec2 center = sum / static_cast<float>(water.size());
      pool.center = glm::vec3(center.x, sea, center.y);
      for (glm::vec2 point : water) {
        pool.radius =
            std::max(pool.radius, glm::distance(point, center) + 0.5f);
      }
      pool.track_distance = distance_sum / static_cast<float>(wet_samples);
      pool.area = static_cast<float>(water.size()) * kWaterStep *
                  world::TrackPath::kSampleSpacing;
      pool.spots = deep.empty() ? water : deep;
      pools.push_back(pool);
    }
    water.clear();
    deep.clear();
    distance_sum = 0.0f;
    wet_samples = 0;
  };
  for (const world::TrackPath::Sample& sample : samples) {
    glm::vec2 center(sample.position.x, sample.position.z);
    glm::vec2 right(-sample.tangent.y, sample.tangent.x);
    bool wet = false;
    for (float lateral = -sample.floor_left - 1.0f;
         lateral <= sample.floor_right + 1.0f; lateral += kWaterStep) {
      glm::vec2 point = center + right * lateral;
      float depth = sea - generator.Height(point);
      if (depth > kPoolDepth) {
        wet = true;
        water.push_back(point);
        if (depth > kSalmonDepth) {
          deep.push_back(point);
        }
      }
    }
    if (wet) {
      distance_sum += sample.distance;
      ++wet_samples;
      dry_run = 0;
    } else if (!water.empty() && ++dry_run > 2) {
      finish();  // a few meters of dry ground between two pools
    }
  }
  finish();
  return pools;
}

int LargestPool(const std::vector<WaterPool>& pools) {
  int largest = -1;
  for (std::size_t i = 0; i < pools.size(); ++i) {
    if (largest < 0 || pools[i].area > pools[largest].area) {
      largest = static_cast<int>(i);
    }
  }
  return largest;
}

std::vector<glm::vec2> RunningRoute(const world::WorldGenerator& generator,
                                    float from, float to) {
  const float sea = generator.GetSeaLevel();
  std::vector<glm::vec2> route;
  for (const world::TrackPath::Sample& sample :
       generator.GetPath().GetSamples()) {
    if (sample.distance < from || sample.distance > to) {
      continue;
    }
    glm::vec2 middle(sample.position.x, sample.position.z);
    glm::vec2 right(-sample.tangent.y, sample.tangent.x);
    glm::vec2 trail = middle + right * sample.trail_offset;
    if (generator.Height(trail) >= sea) {
      route.push_back(trail);
      continue;
    }
    // The trail is under water here: around it through the widest dry part of
    // the floor
    float best_width = 0.0f, best_middle = 0.0f, start = 0.0f;
    bool dry = false;
    for (float lateral = -sample.floor_left;
         lateral <= sample.floor_right + kRouteStep; lateral += kRouteStep) {
      bool here = lateral <= sample.floor_right &&
                  generator.Height(middle + right * lateral) >= sea;
      if (here && !dry) {
        start = lateral;
      } else if (!here && dry && lateral - start > best_width) {
        best_width = lateral - start;
        best_middle = 0.5f * (start + lateral - kRouteStep);
      }
      dry = here;
    }
    if (best_width > 0.0f) {
      route.push_back(middle + right * best_middle);
    }
  }
  // Runners cut the corners (e.g. from the trail towards the way around the
  // water): the average of the points around
  std::vector<glm::vec2> smooth(route.size());
  for (std::size_t i = 0; i < route.size(); ++i) {
    std::size_t first = i > kRouteSmoothing ? i - kRouteSmoothing : 0,
                last = std::min(i + kRouteSmoothing, route.size() - 1);
    glm::vec2 sum(0.0f);
    for (std::size_t k = first; k <= last; ++k) {
      sum += route[k];
    }
    smooth[i] = sum / static_cast<float>(last - first + 1);
  }
  return smooth;
}

bool FindShoreSpot(const world::WorldGenerator& generator,
                   const WaterPool& pool, glm::vec3& position,
                   glm::vec3& direction) {
  const float sea = generator.GetSeaLevel();
  const world::TrackPath& path = generator.GetPath();
  // The water around the pool (to measure how far a place is from it)
  std::vector<glm::vec2> water;
  const glm::vec2 center(pool.center.x, pool.center.z);
  const float reach = pool.radius + kShoreMaxWaterDistance + 1.0f;
  for (float x = -reach; x <= reach; x += 0.5f) {
    for (float z = -reach; z <= reach; z += 0.5f) {
      glm::vec2 point = center + glm::vec2(x, z);
      if (generator.Height(point) < sea) {
        water.push_back(point);
      }
    }
  }
  // Where runners go past the pool (e.g. in the race): the animal must not lie
  // in their way
  const float span = pool.radius + kShoreMaxWaterDistance + 10.0f;
  std::vector<glm::vec2> route = RunningRoute(
      generator, pool.track_distance - span, pool.track_distance + span);
  auto distance_to = [](const std::vector<glm::vec2>& points, glm::vec2 point) {
    float nearest = std::numeric_limits<float>::max();
    for (glm::vec2 other : points) {
      nearest = std::min(nearest, glm::distance(other, point));
    }
    return nearest;
  };
  // The route is a line (runners cut across from one point to the next, e.g.
  // back onto the trail behind the water)
  auto distance_to_route = [&route](glm::vec2 point) {
    float nearest = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i + 1 < route.size(); ++i) {
      glm::vec2 segment = route[i + 1] - route[i];
      float along = glm::dot(point - route[i], segment) /
                    std::max(glm::dot(segment, segment), 1e-6f);
      nearest = std::min(
          nearest,
          glm::distance(point,
                        route[i] + segment * std::clamp(along, 0.0f, 1.0f)));
    }
    return nearest;
  };

  // On the floor of the track (or at the foot of its walls) beside the water:
  // as far from the route as needed, then close to the middle of the pool
  float best_score = -std::numeric_limits<float>::max();
  for (const world::TrackPath::Sample& sample : path.GetSamples()) {
    if (std::abs(sample.distance - pool.track_distance) >
        pool.radius + kShoreMaxWaterDistance) {
      continue;
    }
    glm::vec2 middle(sample.position.x, sample.position.z);
    glm::vec2 right(-sample.tangent.y, sample.tangent.x);
    for (float lateral = -sample.floor_left - kShoreBeyondFloor;
         lateral <= sample.floor_right + kShoreBeyondFloor; lateral += 0.5f) {
      glm::vec2 point = middle + right * lateral;
      float height = generator.Height(point);
      if (height < sea + kShoreMinHeight || height > sea + kShoreMaxHeight) {
        continue;
      }
      bool flat = true;
      for (int i = 0; i < 8 && flat; ++i) {
        float angle = static_cast<float>(i) * 0.25f * kPi;
        flat = std::abs(generator.Height(
                            point + kShoreRadius * glm::vec2(std::cos(angle),
                                                             std::sin(angle))) -
                        height) < kShoreFlatness;
      }
      float water_distance = distance_to(water, point);
      if (!flat || water_distance < kShoreMinWaterDistance ||
          water_distance > kShoreMaxWaterDistance) {
        continue;
      }
      float clearance =
          std::min(distance_to_route(point), kShoreRouteClearance);
      float score = 10.0f * clearance - 0.3f * glm::distance(point, center);
      if (score > best_score) {
        best_score = score;
        position = glm::vec3(point.x, height, point.y);
        glm::vec2 look = center - point;
        direction = glm::normalize(glm::vec3(look.x, 0.0f, look.y));
      }
    }
  }
  return best_score > -std::numeric_limits<float>::max();
}

glm::vec3 SalmonPools::Jump::Position() const {
  return start + velocity * time - kUp * (0.5f * kGravity * time * time);
}

glm::quat SalmonPools::Jump::Rotation() const {
  glm::vec2 horizontal(velocity.x, velocity.z);
  float speed = glm::length(horizontal);
  float yaw = speed > 1e-4f ? std::atan2(horizontal.x, horizontal.y) : 0.0f;
  // The nose follows the arc: up when it comes out of the water, down when it
  // falls back
  float pitch = std::atan2(velocity.y - kGravity * time, std::max(speed, 0.6f));
  return glm::angleAxis(yaw, kUp) *
         glm::angleAxis(-pitch, glm::vec3(1.0f, 0.0f, 0.0f)) *
         glm::angleAxis(spin * time, glm::vec3(0.0f, 0.0f, 1.0f));
}

SalmonPools::SalmonPools(std::vector<WaterPool> pools, std::uint32_t seed)
    : pools_(std::move(pools)), seed_(seed) {
  Reset();
}

float SalmonPools::Uniform(float from, float to) {
  return from + (to - from) * static_cast<float>(random_() >> 8) *
                    (1.0f / 16777216.0f);
}

void SalmonPools::Reset() {
  random_.seed(seed_);
  next_jump_.assign(pools_.size(), 0.0f);
  jumps_.clear();
  droplets_.clear();
}

void SalmonPools::Splash(glm::vec3 position, float surface, int count) {
  for (int i = 0; i < count; ++i) {
    float angle = Uniform(0.0f, 2.0f * kPi);
    float outwards = Uniform(0.6f, 2.0f);
    Droplet droplet;
    droplet.position = glm::vec3(position.x, surface, position.z) +
                       glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) *
                           Uniform(0.05f, 0.3f);
    droplet.velocity =
        glm::vec3(std::cos(angle) * outwards, Uniform(2.0f, 4.5f),
                  std::sin(angle) * outwards);
    droplet.size = Uniform(0.05f, 0.11f);
    droplet.surface = surface - 0.05f;
    droplets_.push_back(droplet);
  }
}

void SalmonPools::StartJump(int pool, glm::vec3 bear_center) {
  const WaterPool& water = pools_[pool];
  if (water.spots.empty()) {
    return;
  }
  // Rather close to the bear (where it jumps over the water), so it has a
  // chance to catch one
  glm::vec2 bear(bear_center.x, bear_center.z);
  glm::vec2 spot = water.spots[random_() % water.spots.size()];
  for (int candidate = 0; candidate < 3; ++candidate) {
    glm::vec2 other = water.spots[random_() % water.spots.size()];
    if (glm::distance(other, bear) < glm::distance(spot, bear)) {
      spot = other;
    }
  }
  Jump jump;
  jump.pool = pool;
  jump.start = glm::vec3(spot.x, water.center.y - kEmergeDepth, spot.y);
  float height = Uniform(2.2f, 3.8f) + kEmergeDepth;
  float angle = Uniform(0.0f, 2.0f * kPi);
  float speed = Uniform(0.5f, 1.8f);
  jump.velocity =
      glm::vec3(std::cos(angle) * speed, std::sqrt(2.0f * kGravity * height),
                std::sin(angle) * speed);
  jump.duration = 2.0f * jump.velocity.y / kGravity;
  jump.spin = Uniform(-3.0f, 3.0f);
  jump.wiggle = Uniform(0.0f, 2.0f * kPi);
  jumps_.push_back(jump);
  Splash(jump.start, water.center.y, 8);
}

int SalmonPools::Update(float time_delta, glm::vec3 bear_center,
                        glm::vec3 bear_mouth, bool can_catch,
                        const std::vector<bool>& closed) {
  if (time_delta <= 0.0f) {
    return -1;
  }
  // Flying salmon splash back into the water
  for (std::size_t i = 0; i < jumps_.size();) {
    Jump& jump = jumps_[i];
    jump.time += time_delta;
    if (jump.time >= jump.duration) {
      Splash(jump.Position(), pools_[jump.pool].center.y, 10);
      jumps_.erase(jumps_.begin() + static_cast<std::ptrdiff_t>(i));
    } else {
      ++i;
    }
  }
  for (std::size_t i = 0; i < droplets_.size();) {
    Droplet& droplet = droplets_[i];
    droplet.velocity.y -= kGravity * time_delta;
    droplet.position += droplet.velocity * time_delta;
    if (droplet.position.y < droplet.surface && droplet.velocity.y < 0.0f) {
      droplets_.erase(droplets_.begin() + static_cast<std::ptrdiff_t>(i));
    } else {
      ++i;
    }
  }

  // New salmon jump while the bear is close to an open pool
  for (std::size_t p = 0; p < pools_.size(); ++p) {
    const WaterPool& pool = pools_[p];
    float distance = glm::distance(glm::vec2(bear_center.x, bear_center.z),
                                   glm::vec2(pool.center.x, pool.center.z));
    if (IsClosed(closed, p) || distance > kApproachDistance + pool.radius) {
      // the first one soon after the bear came close
      next_jump_[p] = std::min(next_jump_[p], 0.3f);
      continue;
    }
    next_jump_[p] -= time_delta;
    int in_air = static_cast<int>(
        std::ranges::count(jumps_, static_cast<int>(p), &Jump::pool));
    if (next_jump_[p] <= 0.0f && in_air < kMaxJumpsPerPool) {
      StartJump(static_cast<int>(p), bear_center);
      next_jump_[p] = Uniform(0.6f, 1.6f);
    }
  }

  if (!can_catch) {
    return -1;
  }
  for (std::size_t i = 0; i < jumps_.size(); ++i) {
    const Jump& jump = jumps_[i];
    glm::vec3 position = jump.Position();
    if (!IsClosed(closed, static_cast<std::size_t>(jump.pool)) &&
        (glm::distance(position, bear_mouth) < kMouthReach ||
         glm::distance(position, bear_center) < kBodyReach)) {
      int pool = jump.pool;
      jumps_.erase(jumps_.begin() + static_cast<std::ptrdiff_t>(i));
      return pool;
    }
  }
  return -1;
}

}  // namespace bibo::scene
