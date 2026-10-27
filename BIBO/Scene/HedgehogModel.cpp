#include "HedgehogModel.hpp"

#include <algorithm>
#include <cmath>

#include "../Resources/SkinnedModel.hpp"

namespace bibo::scene {

using resources::SkinnedModel;

namespace {
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);
// The body is a surface around this point: an egg with a pointed snout that
// points forward and a bit down, flat underneath
const glm::vec3 kCenter(0.0f, kHedgehogBodyHeight, -0.02f);
const glm::vec3 kRadii(0.33f, 0.25f, 0.47f);
const glm::vec3 kSnout = glm::normalize(glm::vec3(0.0f, -0.22f, 1.0f));
const float kBellyHeight = 0.03f;

// Colors (linear)
const glm::vec3 kSpineColor(
    0.36f, 0.29f,
    0.19f);  // the light parts of the spines (the shader adds the dark band)
const glm::vec3 kFaceColor(0.2f, 0.14f, 0.09f);
const glm::vec3 kBellyColor(0.24f, 0.19f, 0.13f);
const glm::vec3 kSnoutColor(0.11f, 0.075f, 0.055f);
const glm::vec3 kSkinColor(0.11f, 0.075f, 0.065f);  // bare feet
const glm::vec3 kNoseColor(0.02f, 0.015f, 0.014f);
const glm::vec3 kClawColor(0.08f, 0.065f, 0.05f);

float SmoothMin(float a, float b, float k) {
  float h = std::clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
  return glm::mix(b, a, h) - k * h * (1.0f - h);
}

/**
 * Distance of the surface of the body from its center in a direction
 */
float BodyRadius(glm::vec3 direction) {
  // The head tapers to the snout (a teardrop seen from above and from the side)
  float taper = 1.0f - 0.45f * glm::smoothstep(0.2f, 0.95f, direction.z);
  float radius =
      1.0f / glm::length(direction / (kRadii * glm::vec3(taper, taper, 1.0f)));
  float snout = glm::smoothstep(0.86f, 1.0f, glm::dot(direction, kSnout));
  radius += 0.16f * std::pow(snout, 1.5f);
  if (direction.y < 0.0f) {
    radius = SmoothMin(
        radius, (kCenter.y - kBellyHeight) / std::max(-direction.y, 1e-3f),
        0.04f);
  }
  return radius;
}

glm::vec3 BodyPoint(glm::vec3 direction) {
  return kCenter + direction * BodyRadius(direction);
}

/**
 * > 0 on the face, < 0 where the spines grow (they cover the forehead like a
 * cap and reach further forward on top)
 */
float FaceSide(glm::vec3 direction) {
  return direction.z - (0.5f + 0.3f * std::max(direction.y, 0.0f));
}
}  // namespace

HedgehogModel HedgehogModel::Build() {
  HedgehogModel model;
  FurryModelBuilder builder(model);

  // Body: spines on the back and the sides, short fur on the face and the belly
  std::size_t first_body_triangle =
      builder.Sphere(72, 44, [&](glm::vec3 direction) {
        float face = FaceSide(direction);
        bool belly = direction.y < -0.6f + 0.25f * std::max(direction.z, 0.0f);
        bool spine = face < 0.0f && !belly;
        glm::vec4 color;
        if (spine) {
          // Shorter at the edge of the spines
          float edge = glm::smoothstep(0.0f, 0.14f, -face) *
                       glm::smoothstep(-0.6f, -0.35f, direction.y);
          color = glm::vec4(kSpineColor, glm::mix(0.5f, 1.0f, edge));
        } else {
          float snout =
              glm::smoothstep(0.85f, 1.0f, glm::dot(direction, kSnout));
          glm::vec3 fur =
              belly ? kBellyColor : glm::mix(kFaceColor, kSnoutColor, snout);
          color = glm::vec4(fur, belly ? 0.8f : glm::mix(0.75f, 0.2f, snout));
        }
        return builder.Add(BodyPoint(direction), direction, color,
                           SkinnedModel::kFur, kBody, spine);
      });
  std::size_t last_body_triangle = builder.TriangleCount();
  builder.SmoothNormals(first_body_triangle, last_body_triangle);
  builder.GrowFurAndSpines(first_body_triangle, last_body_triangle);

  // Wet nose at the tip of the snout
  glm::vec3 tip = BodyPoint(kSnout);
  builder.Ellipsoid(tip + kSnout * 0.005f, glm::vec3(0.035f, 0.03f, 0.03f), 14,
                    10, glm::vec4(kNoseColor, 0.0f), SkinnedModel::kNose,
                    kBody);

  // Small dark eyes (shaded like the eyes of the bear: the color is the
  // direction in eye space, gaze along +z)
  for (float side : {1.0f, -1.0f}) {
    glm::vec3 direction = glm::normalize(glm::vec3(0.36f * side, 0.33f, 0.87f));
    glm::vec3 center = BodyPoint(direction) - direction * 0.012f;
    glm::vec3 gaze = glm::normalize(glm::vec3(0.55f * side, 0.05f, 0.83f));
    glm::vec3 x = glm::normalize(glm::cross(kUp, gaze));
    glm::vec3 y = glm::cross(gaze, x);
    const float eye_radius = 0.026f;
    builder.Sphere(16, 12, [&](glm::vec3 d) {
      glm::vec4 color(
          glm::vec3(glm::dot(d, x), glm::dot(d, y), glm::dot(d, gaze)) * 0.5f +
              0.5f,
          0.0f);
      return builder.Add(center + d * eye_radius, d, color, SkinnedModel::kEye,
                         kBody, false);
    });

    // Round ears just in front of the spines
    glm::vec3 ear_direction =
        glm::normalize(glm::vec3(0.5f * side, 0.62f, 0.4f));
    glm::vec3 ear = BodyPoint(ear_direction);
    std::size_t first_ear_triangle = builder.Ellipsoid(
        ear, glm::vec3(0.045f, 0.05f, 0.022f), 12, 8,
        glm::vec4(kFaceColor * 1.1f, 0.12f), SkinnedModel::kFur, kBody);
    builder.GrowFur(first_ear_triangle, builder.TriangleCount());
  }

  // Feet with bare soles and dark claws, each in the space of its foot
  for (int part = kFootFrontLeft; part <= kFootHindRight; ++part) {
    builder.Ellipsoid(glm::vec3(0.0f, 0.025f, 0.0f),
                      glm::vec3(0.042f, 0.027f, 0.058f), 14, 8,
                      glm::vec4(kSkinColor, 0.0f), SkinnedModel::kFur, part);
    for (int claw = 0; claw < 4; ++claw) {
      float x = (static_cast<float>(claw) - 1.5f) * 0.02f;
      builder.Ellipsoid(glm::vec3(x, 0.01f, 0.06f),
                        glm::vec3(0.006f, 0.006f, 0.018f), 6, 4,
                        glm::vec4(kClawColor, 0.0f), SkinnedModel::kClaw, part);
    }
  }
  return model;
}

}  // namespace bibo::scene
