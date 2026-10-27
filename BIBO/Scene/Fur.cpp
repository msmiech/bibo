#include "Fur.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace bibo::fur {

namespace {
// Spring of the guide hairs (per fur length): a bit bouncy, so the fur keeps
// moving for a moment after the body stops
const float kStiffness = 260.0f;
const float kDamping = 11.0f;
// How strongly the fur lags behind the accelerating skin (1 would be a free
// hair tip)
const float kInertia = 0.35f;
// How far the fur hangs down along the surface
const float kDroop = 0.3f;
const float kMaxBend = 1.4f;
// The tip always stays this far above the skin (in fur lengths)
const float kMinLift = 0.15f;
// Accelerations are clamped (e.g. teleports when the game is restarted)
const float kMaxAcceleration = 200.0f;
const float kMaxStep = 1.0f / 120.0f;
// Hairs thinner than this length (world units) are not simulated
const float kMinLength = 0.005f;

float Random01(std::mt19937& generator) {
  return static_cast<float>(generator() >> 8) * (1.0f / 16777216.0f);
}
}  // namespace

void GuideHairs::Reset(const std::vector<glm::vec3>& positions) {
  previous_positions_ = positions;
  vertex_velocities_.assign(positions.size(), glm::vec3(0.0f));
  offsets_.assign(positions.size(), glm::vec3(0.0f));
  offset_velocities_.assign(positions.size(), glm::vec3(0.0f));
  impulses_.clear();
}

void GuideHairs::AddImpulse(glm::vec3 position, glm::vec3 velocity,
                            float radius) {
  impulses_.push_back({position, velocity, radius});
}

void GuideHairs::Update(float time_delta,
                        const std::vector<glm::vec3>& positions,
                        const std::vector<glm::vec3>& normals,
                        const std::vector<float>& lengths,
                        const Environment& environment) {
  if (previous_positions_.size() != positions.size()) {
    Reset(positions);
  }
  if (time_delta <= 0.0f) {
    return;
  }
  const int steps =
      std::max(1, static_cast<int>(std::ceil(time_delta / kMaxStep)));
  const float step = time_delta / static_cast<float>(steps);
  const glm::vec3 down = glm::length(environment.gravity) > 0.0f
                             ? glm::normalize(environment.gravity)
                             : glm::vec3(0.0f);

  for (std::size_t i = 0; i < positions.size(); ++i) {
    const float length = lengths[i];
    const glm::vec3 p = positions[i];
    glm::vec3 velocity = (p - previous_positions_[i]) / time_delta;
    glm::vec3 acceleration = (velocity - vertex_velocities_[i]) / time_delta;
    vertex_velocities_[i] = velocity;
    previous_positions_[i] = p;
    if (length < kMinLength) {
      offsets_[i] = glm::vec3(0.0f);
      continue;
    }
    float acceleration_length = glm::length(acceleration);
    if (acceleration_length > kMaxAcceleration) {
      acceleration *= kMaxAcceleration / acceleration_length;
    }

    const glm::vec3 n = normals[i];
    glm::vec3& offset = offsets_[i];
    glm::vec3& offset_velocity = offset_velocities_[i];

    for (const Impulse& impulse : impulses_) {
      float distance = glm::distance(p, impulse.position);
      if (distance < impulse.radius) {
        float falloff = 1.0f - distance / impulse.radius;
        offset_velocity +=
            impulse.velocity * (0.1f * falloff * falloff / length);
      }
    }

    // At rest the fur hangs down along the surface
    const glm::vec3 rest = kDroop * (down - n * glm::dot(down, n));
    // The skin accelerates, the tip lags behind (a pseudo force in the frame of
    // the skin)
    const glm::vec3 inertia = -acceleration * (kInertia / length);
    // Gusts: slow waves that travel over the body
    float gust = 0.6f + 0.4f *
                            std::sin(environment.time * 2.3f +
                                     glm::dot(p, glm::vec3(2.1f, 1.3f, 1.7f))) *
                            std::sin(environment.time * 0.9f + p.x * 0.7f);
    const glm::vec3 force = inertia + environment.wind * gust;

    for (int s = 0; s < steps; ++s) {
      offset_velocity +=
          (kStiffness * (rest - offset) - kDamping * offset_velocity + force) *
          step;
      offset += offset_velocity * step;
    }

    // The tip stays above the skin
    float lift = 1.0f + glm::dot(offset, n);
    if (lift < kMinLift) {
      offset += n * (kMinLift - lift);
      offset_velocity -= n * std::min(0.0f, glm::dot(offset_velocity, n));
    }
    float bend = glm::length(offset);
    if (bend > kMaxBend) {
      offset *= kMaxBend / bend;
    }

    // Collisions of the tip: the fur is squashed against the ground and
    // obstacles instead of going through them
    glm::vec3 tip = p + (n + offset) * length;
    bool collided = false;
    if (environment.has_ground) {
      float height =
          glm::dot(tip - environment.ground_point, environment.ground_normal);
      float root_height =
          glm::dot(p - environment.ground_point, environment.ground_normal);
      if (height < 0.0f && root_height > -length) {
        tip -= environment.ground_normal * height;
        offset_velocity -= environment.ground_normal *
                           std::min(0.0f, glm::dot(offset_velocity,
                                                   environment.ground_normal));
        collided = true;
      }
    }
    for (const SphereCollider& sphere : environment.spheres) {
      glm::vec3 from_center = tip - sphere.center;
      float distance = glm::length(from_center);
      if (distance < sphere.radius && distance > 1e-5f &&
          glm::distance(p, sphere.center) > sphere.radius * 0.8f) {
        glm::vec3 outward = from_center / distance;
        tip = sphere.center + outward * sphere.radius;
        offset_velocity -=
            outward * std::min(0.0f, glm::dot(offset_velocity, outward));
        collided = true;
      }
    }
    if (collided) {
      offset = (tip - p) / length - n;
      offset_velocity *= 0.8f;  // friction
    }
  }
  impulses_.clear();
}

std::vector<Strand> GenerateStrands(const std::vector<glm::vec3>& positions,
                                    const std::vector<glm::vec4>& colors,
                                    const std::vector<unsigned int>& triangles,
                                    std::size_t count, std::uint32_t seed) {
  std::vector<Strand> strands;
  // Probability of a triangle: its area, more where the fur is longer, none
  // where there is (almost) no fur
  std::vector<double> cumulative;
  std::vector<std::size_t> fur_triangles;
  double total = 0.0;
  for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
    const unsigned int a = triangles[t], b = triangles[t + 1],
                       c = triangles[t + 2];
    float fur = (colors[a].a + colors[b].a + colors[c].a) / 3.0f;
    if (fur < 0.05f) {
      continue;
    }
    float area = 0.5f * glm::length(glm::cross(positions[b] - positions[a],
                                               positions[c] - positions[a]));
    total += area * (0.4 + fur);
    cumulative.push_back(total);
    fur_triangles.push_back(t);
  }
  if (fur_triangles.empty() || count == 0) {
    return strands;
  }

  std::mt19937 generator(seed);
  strands.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    double pick = Random01(generator) * total;
    std::size_t index = std::min(
        static_cast<std::size_t>(
            std::lower_bound(cumulative.begin(), cumulative.end(), pick) -
            cumulative.begin()),
        fur_triangles.size() - 1);
    std::size_t t = fur_triangles[index];
    // Uniformly distributed point on the triangle
    float r1 = std::sqrt(Random01(generator)), r2 = Random01(generator);
    Strand strand;
    strand.vertices[0] = triangles[t];
    strand.vertices[1] = triangles[t + 1];
    strand.vertices[2] = triangles[t + 2];
    strand.seed = generator();
    strand.barycentric[0] = 1.0f - r1;
    strand.barycentric[1] = r1 * (1.0f - r2);
    strand.length_scale = 0.7f + 0.45f * Random01(generator);
    strand.shade = Random01(generator);
    strands.push_back(strand);
  }
  return strands;
}

std::vector<glm::vec3> CombDirections(const std::vector<glm::vec3>& normals,
                                      glm::vec3 forward, glm::vec3 up) {
  const glm::vec3 flow = glm::normalize(-forward - 0.55f * up);
  std::vector<glm::vec3> directions;
  directions.reserve(normals.size());
  for (const glm::vec3& n : normals) {
    glm::vec3 tangent = flow - n * glm::dot(flow, n);
    if (glm::dot(tangent, tangent) < 0.01f) {
      // The fur points straight away from the body where the surface faces the
      // flow (e.g. at the tail)
      tangent = -up - n * glm::dot(-up, n);
    }
    if (glm::dot(tangent, tangent) < 1e-6f) {
      tangent = glm::cross(n, forward);
    }
    directions.push_back(glm::normalize(tangent));
  }
  return directions;
}

}  // namespace bibo::fur
