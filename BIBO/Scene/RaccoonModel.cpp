#include "RaccoonModel.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace bibo::scene {

using resources::SkinnedModel;

namespace {
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);
const glm::vec3 kBackwards(0.0f, 0.0f, -1.0f);
// The body around this point, higher at the hips
const glm::vec3 kBodyCenter(0.0f, 0.33f, 0.03f);
const glm::vec3 kBodyRadii(0.16f, 0.15f, 0.27f);
const glm::vec3 kHips = glm::normalize(glm::vec3(0.0f, 0.7f, -0.7f));
// The head around this point (in the space of the head, the origin is the
// neck), the pointed muzzle forward and a bit down
const glm::vec3 kHeadCenter(0.0f, 0.04f, 0.08f);
const glm::vec3 kHeadRadii(0.11f, 0.09f, 0.1f);
const glm::vec3 kMuzzle = glm::normalize(glm::vec3(0.0f, -0.2f, 1.0f));
// Under the muzzle the head has a hollow for the lower jaw
const glm::vec3 kChin = glm::normalize(glm::vec3(0.0f, -0.75f, 0.66f));
const float kEyeRadius = 0.021f;

// Colors (linear)
const glm::vec3 kBackColor(0.085f, 0.08f, 0.075f);
const glm::vec3 kBellyColor(0.2f, 0.19f, 0.17f);
const glm::vec3 kHeadColor(0.17f, 0.165f, 0.155f);
const glm::vec3 kWhiteColor(0.62f, 0.6f, 0.57f);
const glm::vec3 kMaskColor(0.018f, 0.016f, 0.015f);
const glm::vec3 kLegColor(0.04f, 0.037f, 0.035f);
const glm::vec3 kPawColor(0.03f, 0.026f, 0.025f);  // bare black paws
const glm::vec3 kTailLight(0.26f, 0.23f, 0.19f);
const glm::vec3 kTailDark(0.025f, 0.022f, 0.02f);
const glm::vec3 kNoseColor(0.012f, 0.011f, 0.01f);
const glm::vec3 kTeethColor(0.75f, 0.72f, 0.64f);
const glm::vec3 kMouthColor(0.1f, 0.02f, 0.02f);
const glm::vec3 kTongueColor(0.45f, 0.13f, 0.13f);
const glm::vec3 kClawColor(0.035f, 0.03f, 0.025f);

glm::vec3 CombAlong(glm::vec3 normal, glm::vec3 wanted) {
  glm::vec3 tangent = wanted - normal * glm::dot(wanted, normal);
  return glm::length(tangent) > 1e-4f
             ? glm::normalize(tangent)
             : glm::normalize(glm::cross(normal, glm::vec3(1.0f, 0.0f, 0.0f)) +
                              glm::vec3(0.0f, -1e-3f, 0.0f));
}

float BodyRadius(glm::vec3 direction, float girth) {
  // A well fed belly hangs down and bulges to the sides
  float belly = glm::smoothstep(0.2f, -0.8f, direction.y);
  glm::vec3 radii =
      kBodyRadii * glm::vec3(1.0f + (girth - 1.0f) * (0.4f + 0.6f * belly),
                             1.0f + 0.5f * (girth - 1.0f) * belly, 1.0f);
  float radius = 1.0f / glm::length(direction / radii);
  // Higher at the hips, narrower at the chest
  radius += 0.035f * glm::smoothstep(0.3f, 0.9f, glm::dot(direction, kHips));
  radius -= 0.02f * glm::smoothstep(0.5f, 0.95f, direction.z);
  return radius;
}

float HeadRadius(glm::vec3 direction) {
  // Broad cheeks, the muzzle is narrow and pointed
  float taper = 1.0f - 0.35f * glm::smoothstep(0.2f, 0.95f, direction.z);
  float radius =
      1.0f /
      glm::length(
          direction /
          (kHeadRadii *
           glm::vec3(taper * (1.0f + 0.12f * glm::smoothstep(0.6f, -0.2f,
                                                             direction.z)),
                     taper, 1.0f)));
  float muzzle = glm::smoothstep(0.75f, 1.0f, glm::dot(direction, kMuzzle));
  float chin = glm::smoothstep(0.88f, 0.98f, glm::dot(direction, kChin));
  return radius + 0.085f * std::pow(muzzle, 1.6f) - 0.022f * chin;
}

glm::vec3 HeadPoint(glm::vec3 direction) {
  return kHeadCenter + direction * HeadRadius(direction);
}

glm::vec3 EyeDirection(bool left) {
  return glm::normalize(glm::vec3(0.45f * (left ? 1.0f : -1.0f), 0.3f, 0.84f));
}

/**
 * Small dark eye, shaded like the eyes of the bear (the color is the direction
 * in eye space)
 */
void AddEye(FurryModelBuilder& builder, glm::vec3 center, glm::vec3 gaze,
            float radius, int part) {
  glm::vec3 x = glm::normalize(glm::cross(kUp, gaze));
  glm::vec3 y = glm::cross(gaze, x);
  builder.Sphere(14, 10, [&](glm::vec3 d) {
    glm::vec4 color(
        glm::vec3(glm::dot(d, x), glm::dot(d, y), glm::dot(d, gaze)) * 0.5f +
            0.5f,
        0.0f);
    return builder.Add(center + d * radius, d, color, SkinnedModel::kEye, part);
  });
}

void AddFurryEllipsoid(FurryModelBuilder& builder, glm::vec3 center,
                       glm::vec3 radii, glm::vec3 color, float fur_length,
                       glm::vec3 comb, int part) {
  std::size_t first = builder.Sphere(14, 10, [&](glm::vec3 d) {
    glm::vec3 normal = glm::normalize(d / radii);
    return builder.Add(center + d * radii, normal, glm::vec4(color, fur_length),
                       SkinnedModel::kFur, part, false,
                       CombAlong(normal, comb));
  });
  builder.GrowFur(first, builder.TriangleCount());
}

/**
 * The long fingers of a paw fanning out forward, each with a small claw
 */
void AddFingers(FurryModelBuilder& builder, glm::vec3 base, float length,
                float thickness, int part) {
  const int count = 5;
  for (int finger = 0; finger < count; ++finger) {
    float angle =
        1.1f *
        (static_cast<float>(finger) / static_cast<float>(count - 1) - 0.5f);
    glm::vec3 direction =
        glm::normalize(glm::vec3(std::sin(angle), -0.1f, std::cos(angle)));
    glm::vec3 x = glm::normalize(glm::cross(kUp, direction)),
              y = glm::cross(direction, x);
    glm::vec3 center = base + direction * (0.5f * length);
    glm::vec3 radii(thickness, 0.8f * thickness, 0.5f * length);
    builder.Sphere(8, 6, [&](glm::vec3 d) {
      glm::vec3 local = d * radii;
      glm::vec3 normal = glm::normalize(d / radii);
      return builder.Add(
          center + x * local.x + y * local.y + direction * local.z,
          x * normal.x + y * normal.y + direction * normal.z,
          glm::vec4(kPawColor, 0.0f), SkinnedModel::kFur, part);
    });
    builder.Ellipsoid(
        base + direction * (length + 0.004f),
        glm::vec3(0.5f * thickness, 0.4f * thickness, 0.5f * thickness) +
            glm::abs(direction) * 0.004f,
        6, 4, glm::vec4(kClawColor, 0.0f), SkinnedModel::kClaw, part);
  }
}

/**
 * A leg hanging from its joint (the origin) to the ground: the furry upper leg,
 * the dark lower leg and the bare paw
 */
void AddLeg(FurryModelBuilder& builder, float length, glm::vec3 upper_radii,
            float paw_length, int part) {
  AddFurryEllipsoid(builder, glm::vec3(0.0f, -0.25f * length, 0.0f),
                    upper_radii, glm::mix(kBackColor, kLegColor, 0.3f), 0.6f,
                    -kUp, part);
  AddFurryEllipsoid(builder, glm::vec3(0.0f, -0.65f * length, 0.0f),
                    glm::vec3(0.035f, 0.3f * length, 0.039f), kLegColor, 0.35f,
                    -kUp, part);
  glm::vec3 paw(0.0f, -length + 0.014f, 0.3f * paw_length);
  builder.Ellipsoid(paw, glm::vec3(0.035f, 0.014f, paw_length), 12, 8,
                    glm::vec4(kPawColor, 0.0f), SkinnedModel::kFur, part);
  AddFingers(builder, paw + glm::vec3(0.0f, -0.002f, 0.7f * paw_length), 0.035f,
             0.0075f, part);
}
}  // namespace

RaccoonModel RaccoonModel::Build(float girth) {
  RaccoonModel model;
  FurryModelBuilder builder(model);

  // Body: thick gray fur, darker on the back, lighter on the belly
  std::size_t first_body_triangle =
      builder.Sphere(48, 36, [&](glm::vec3 direction) {
        float belly = glm::smoothstep(0.1f, -0.7f, direction.y);
        glm::vec3 color = glm::mix(kBackColor, kBellyColor, belly);
        return builder.Add(
            kBodyCenter + direction * BodyRadius(direction, girth), direction,
            glm::vec4(color, glm::mix(0.9f, 0.7f, belly)), SkinnedModel::kFur,
            kBody);
      });
  std::size_t last_body_triangle = builder.TriangleCount();
  builder.SmoothNormals(first_body_triangle, last_body_triangle);
  builder.GrowFur(first_body_triangle, last_body_triangle);

  // Head: the black mask around the eyes and a dark stripe down the nose, white
  // brows and white sides of the muzzle
  std::size_t first_head_triangle =
      builder.Sphere(44, 32, [&](glm::vec3 direction) {
        float white = 0.0f, mask = 0.0f;
        for (float side : {1.0f, -1.0f}) {
          mask = std::max(
              mask,
              glm::smoothstep(0.8f, 0.92f,
                              glm::dot(direction, EyeDirection(side > 0.0f))));
          white = std::max(
              white,
              glm::smoothstep(
                  0.86f, 0.95f,
                  glm::dot(direction, glm::normalize(glm::vec3(
                                          0.38f * side, 0.62f, 0.69f)))));
          white = std::max(
              white,
              glm::smoothstep(
                  0.84f, 0.94f,
                  glm::dot(direction, glm::normalize(glm::vec3(
                                          0.42f * side, -0.3f, 0.86f)))));
        }
        mask = std::max(
            mask, 0.7f * glm::smoothstep(
                             0.95f, 0.99f,
                             glm::dot(direction, glm::normalize(glm::vec3(
                                                     0.0f, 0.2f, 1.0f)))));
        glm::vec3 color = glm::mix(glm::mix(kHeadColor, kWhiteColor, white),
                                   kMaskColor, mask);
        float muzzle =
            glm::smoothstep(0.8f, 1.0f, glm::dot(direction, kMuzzle));
        return builder.Add(HeadPoint(direction), direction,
                           glm::vec4(color, glm::mix(0.55f, 0.2f, muzzle)),
                           SkinnedModel::kFur, kHead);
      });
  std::size_t last_head_triangle = builder.TriangleCount();
  builder.SmoothNormals(first_head_triangle, last_head_triangle);
  builder.GrowFur(first_head_triangle, last_head_triangle);
  // The fur follows the smoothed normals: backwards on the body and on the head
  for (std::size_t v = 0; v < model.positions.size(); ++v) {
    model.combs[v] = CombAlong(model.normals[v], model.parts[v] == kBody
                                                     ? kBackwards - 0.4f * kUp
                                                     : kBackwards - 0.2f * kUp);
  }

  // Black nose at the tip of the muzzle, the mouth in the hollow under it
  glm::vec3 nose_direction = glm::normalize(glm::vec3(0.0f, -0.08f, 1.0f));
  builder.Ellipsoid(HeadPoint(nose_direction) - nose_direction * 0.004f,
                    glm::vec3(0.024f, 0.017f, 0.018f), 12, 8,
                    glm::vec4(kNoseColor, 0.0f), SkinnedModel::kNose, kHead);
  glm::vec3 hollow = HeadPoint(kChin);
  builder.Ellipsoid(hollow + glm::vec3(0.0f, 0.004f, 0.01f),
                    glm::vec3(0.035f, 0.016f, 0.045f), 12, 8,
                    glm::vec4(kMouthColor, 0.0f), SkinnedModel::kMouth, kHead);

  for (float side : {1.0f, -1.0f}) {
    // Dark shiny eyes in the mask
    bool left = side > 0.0f;
    glm::vec3 eye = EyeDirection(left);
    AddEye(builder, HeadPoint(eye) - eye * 0.01f,
           glm::normalize(glm::vec3(0.3f * side, 0.05f, 0.95f)), kEyeRadius,
           kHead);

    // Pointed ears, dark inside with white rims
    glm::vec3 ear_direction =
        glm::normalize(glm::vec3(0.6f * side, 0.75f, -0.15f));
    glm::vec3 ear_center =
        HeadPoint(ear_direction) + glm::vec3(0.0f, 0.025f, 0.0f);
    std::size_t first = builder.Sphere(12, 10, [&](glm::vec3 d) {
      // Narrower towards the tip
      glm::vec3 radii(
          0.042f * (1.0f - 0.45f * glm::smoothstep(-0.2f, 1.0f, d.y)), 0.055f,
          0.016f);
      float rim = glm::smoothstep(0.6f, 0.9f, glm::length(glm::vec2(d.x, d.y)));
      glm::vec3 color =
          glm::mix(glm::mix(kBackColor, kMaskColor, 0.5f), kWhiteColor, rim);
      glm::vec3 normal = glm::normalize(d / radii);
      return builder.Add(ear_center + d * radii, normal,
                         glm::vec4(color, 0.25f), SkinnedModel::kFur, kHead,
                         false, CombAlong(normal, kUp));
    });
    builder.GrowFur(first, builder.TriangleCount());
  }

  // Lower jaw: fills the hollow under the muzzle (white like the muzzle), the
  // tongue and small teeth on it
  glm::vec3 chin_center =
      hollow + glm::vec3(0.0f, -0.003f, -0.004f) - kRaccoonJaw;
  AddFurryEllipsoid(builder, chin_center, glm::vec3(0.048f, 0.026f, 0.058f),
                    kWhiteColor * 0.8f, 0.3f, kBackwards - kUp, kJaw);
  builder.Ellipsoid(chin_center + glm::vec3(0.0f, 0.02f, 0.0f),
                    glm::vec3(0.028f, 0.01f, 0.038f), 10, 6,
                    glm::vec4(kTongueColor, 0.0f), SkinnedModel::kTongue, kJaw);
  for (float side : {1.0f, -1.0f}) {
    builder.Ellipsoid(chin_center + glm::vec3(0.014f * side, 0.024f, 0.042f),
                      glm::vec3(0.005f, 0.01f, 0.005f), 6, 4,
                      glm::vec4(kTeethColor, 0.0f), SkinnedModel::kTeeth, kJaw);
  }

  // Legs (in the space of their joints): slim, dark below, with black hand-like
  // paws; the hind legs have thicker thighs
  for (int part : {kFrontLeft, kFrontRight}) {
    AddLeg(builder, kRaccoonFrontLeg, glm::vec3(0.055f, 0.1f, 0.06f), 0.038f,
           part);
  }
  for (int part : {kHindLeft, kHindRight}) {
    AddLeg(builder, kRaccoonHindLeg,
           glm::vec3(0.07f * std::sqrt(girth), 0.12f, 0.085f), 0.06f, part);
  }

  // The bushy tail with dark rings and a dark tip (in the space of the tail, it
  // reaches into the body)
  const glm::vec3 tail_center(0.0f, -0.035f, -0.21f),
      tail_radii(0.065f, 0.065f, 0.23f);
  std::size_t first_tail = builder.Sphere(24, 20, [&](glm::vec3 d) {
    // Thinner at the root and at the tip
    float thickness = 0.75f + 0.25f * (1.0f - d.z * d.z) +
                      0.1f * glm::smoothstep(0.0f, -0.7f, d.z);
    glm::vec3 radii = tail_radii * glm::vec3(thickness, thickness, 1.0f);
    glm::vec3 position = tail_center + d * radii;
    float ring =
        0.5f + 0.5f * std::sin(position.z * glm::two_pi<float>() / 0.085f);
    float dark = std::max(glm::smoothstep(0.35f, 0.65f, ring),
                          glm::smoothstep(-0.36f, -0.42f, position.z));
    glm::vec3 normal = glm::normalize(d / radii);
    return builder.Add(position, normal,
                       glm::vec4(glm::mix(kTailLight, kTailDark, dark), 1.0f),
                       SkinnedModel::kFur, kTail, false,
                       CombAlong(normal, kBackwards));
  });
  builder.GrowFur(first_tail, builder.TriangleCount());
  return model;
}

}  // namespace bibo::scene
