#include "Fur.hpp"

#include <algorithm>
#include <cmath>
#include <random>

using namespace bibo;
using namespace fur;

namespace {
// Spring of the guide hairs (per fur length): a bit bouncy, so the fur keeps
// moving for a moment after the body stops
const float STIFFNESS = 260.0f;
const float DAMPING = 11.0f;
// How strongly the fur lags behind the accelerating skin (1 would be a free
// hair tip)
const float INERTIA = 0.35f;
// How far the fur hangs down along the surface
const float DROOP = 0.3f;
const float MAX_BEND = 1.4f;
// The tip always stays this far above the skin (in fur lengths)
const float MIN_LIFT = 0.15f;
// Accelerations are clamped (e.g. teleports when the game is restarted)
const float MAX_ACCELERATION = 200.0f;
const float MAX_STEP = 1.0f / 120.0f;
// Hairs thinner than this length (world units) are not simulated
const float MIN_LENGTH = 0.005f;

float random01(std::mt19937& generator) {
  return static_cast<float>(generator() >> 8) * (1.0f / 16777216.0f);
}
}  // namespace

void GuideHairs::reset(const std::vector<glm::vec3>& positions) {
  previousPositions = positions;
  vertexVelocities.assign(positions.size(), glm::vec3(0.0f));
  offsets.assign(positions.size(), glm::vec3(0.0f));
  offsetVelocities.assign(positions.size(), glm::vec3(0.0f));
  impulses.clear();
}

void GuideHairs::addImpulse(glm::vec3 position, glm::vec3 velocity,
                            float radius) {
  impulses.push_back({position, velocity, radius});
}

void GuideHairs::update(float timeDelta,
                        const std::vector<glm::vec3>& positions,
                        const std::vector<glm::vec3>& normals,
                        const std::vector<float>& lengths,
                        const Environment& environment) {
  if (previousPositions.size() != positions.size()) {
    reset(positions);
  }
  if (timeDelta <= 0.0f) {
    return;
  }
  const int steps =
      std::max(1, static_cast<int>(std::ceil(timeDelta / MAX_STEP)));
  const float step = timeDelta / static_cast<float>(steps);
  const glm::vec3 down = glm::length(environment.gravity) > 0.0f
                             ? glm::normalize(environment.gravity)
                             : glm::vec3(0.0f);

  for (std::size_t i = 0; i < positions.size(); ++i) {
    const float length = lengths[i];
    const glm::vec3 p = positions[i];
    glm::vec3 velocity = (p - previousPositions[i]) / timeDelta;
    glm::vec3 acceleration = (velocity - vertexVelocities[i]) / timeDelta;
    vertexVelocities[i] = velocity;
    previousPositions[i] = p;
    if (length < MIN_LENGTH) {
      offsets[i] = glm::vec3(0.0f);
      continue;
    }
    float accelerationLength = glm::length(acceleration);
    if (accelerationLength > MAX_ACCELERATION) {
      acceleration *= MAX_ACCELERATION / accelerationLength;
    }

    const glm::vec3 n = normals[i];
    glm::vec3& offset = offsets[i];
    glm::vec3& offsetVelocity = offsetVelocities[i];

    for (const Impulse& impulse : impulses) {
      float distance = glm::distance(p, impulse.position);
      if (distance < impulse.radius) {
        float falloff = 1.0f - distance / impulse.radius;
        offsetVelocity +=
            impulse.velocity * (0.1f * falloff * falloff / length);
      }
    }

    // At rest the fur hangs down along the surface
    const glm::vec3 rest = DROOP * (down - n * glm::dot(down, n));
    // The skin accelerates, the tip lags behind (a pseudo force in the frame of
    // the skin)
    const glm::vec3 inertia = -acceleration * (INERTIA / length);
    // Gusts: slow waves that travel over the body
    float gust = 0.6f + 0.4f *
                            std::sin(environment.time * 2.3f +
                                     glm::dot(p, glm::vec3(2.1f, 1.3f, 1.7f))) *
                            std::sin(environment.time * 0.9f + p.x * 0.7f);
    const glm::vec3 force = inertia + environment.wind * gust;

    for (int s = 0; s < steps; ++s) {
      offsetVelocity +=
          (STIFFNESS * (rest - offset) - DAMPING * offsetVelocity + force) *
          step;
      offset += offsetVelocity * step;
    }

    // The tip stays above the skin
    float lift = 1.0f + glm::dot(offset, n);
    if (lift < MIN_LIFT) {
      offset += n * (MIN_LIFT - lift);
      offsetVelocity -= n * std::min(0.0f, glm::dot(offsetVelocity, n));
    }
    float bend = glm::length(offset);
    if (bend > MAX_BEND) {
      offset *= MAX_BEND / bend;
    }

    // Collisions of the tip: the fur is squashed against the ground and
    // obstacles instead of going through them
    glm::vec3 tip = p + (n + offset) * length;
    bool collided = false;
    if (environment.hasGround) {
      float height =
          glm::dot(tip - environment.groundPoint, environment.groundNormal);
      float rootHeight =
          glm::dot(p - environment.groundPoint, environment.groundNormal);
      if (height < 0.0f && rootHeight > -length) {
        tip -= environment.groundNormal * height;
        offsetVelocity -=
            environment.groundNormal *
            std::min(0.0f, glm::dot(offsetVelocity, environment.groundNormal));
        collided = true;
      }
    }
    for (const SphereCollider& sphere : environment.spheres) {
      glm::vec3 fromCenter = tip - sphere.center;
      float distance = glm::length(fromCenter);
      if (distance < sphere.radius && distance > 1e-5f &&
          glm::distance(p, sphere.center) > sphere.radius * 0.8f) {
        glm::vec3 outward = fromCenter / distance;
        tip = sphere.center + outward * sphere.radius;
        offsetVelocity -=
            outward * std::min(0.0f, glm::dot(offsetVelocity, outward));
        collided = true;
      }
    }
    if (collided) {
      offset = (tip - p) / length - n;
      offsetVelocity *= 0.8f;  // friction
    }
  }
  impulses.clear();
}

std::vector<Strand> bibo::fur::generateStrands(
    const std::vector<glm::vec3>& positions,
    const std::vector<glm::vec4>& colors,
    const std::vector<unsigned int>& triangles, std::size_t count,
    std::uint32_t seed) {
  std::vector<Strand> strands;
  // Probability of a triangle: its area, more where the fur is longer, none
  // where there is (almost) no fur
  std::vector<double> cumulative;
  std::vector<std::size_t> furTriangles;
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
    furTriangles.push_back(t);
  }
  if (furTriangles.empty() || count == 0) {
    return strands;
  }

  std::mt19937 generator(seed);
  strands.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    double pick = random01(generator) * total;
    std::size_t index = std::min(
        static_cast<std::size_t>(
            std::lower_bound(cumulative.begin(), cumulative.end(), pick) -
            cumulative.begin()),
        furTriangles.size() - 1);
    std::size_t t = furTriangles[index];
    // Uniformly distributed point on the triangle
    float r1 = std::sqrt(random01(generator)), r2 = random01(generator);
    Strand strand;
    strand.vertices[0] = triangles[t];
    strand.vertices[1] = triangles[t + 1];
    strand.vertices[2] = triangles[t + 2];
    strand.seed = generator();
    strand.barycentric[0] = 1.0f - r1;
    strand.barycentric[1] = r1 * (1.0f - r2);
    strand.lengthScale = 0.7f + 0.45f * random01(generator);
    strand.shade = random01(generator);
    strands.push_back(strand);
  }
  return strands;
}

std::vector<glm::vec3> bibo::fur::combDirections(
    const std::vector<glm::vec3>& normals, glm::vec3 forward, glm::vec3 up) {
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
