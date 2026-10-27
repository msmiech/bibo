#include "BeaverModel.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace bibo::scene {

using resources::SkinnedModel;

namespace {
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);
const glm::vec3 kBackwards(0.0f, 0.0f, -1.0f);
// The body is a surface around this point: a pear, wide at the bottom where it
// sits, narrow at the neck
const glm::vec3 kBodyCenter(0.0f, 0.41f, -0.02f);
const glm::vec3 kBodyRadii(0.29f, 0.4f, 0.26f);
const float kSeatHeight = 0.02f;
// The head around this point (in the space of the head, the origin is the
// neck), the muzzle points forward and down
const glm::vec3 kHeadCenter(0.0f, 0.14f, 0.07f);
const glm::vec3 kHeadRadii(0.16f, 0.15f, 0.17f);
const glm::vec3 kMuzzle = glm::normalize(glm::vec3(0.0f, -0.35f, 1.0f));
// Under the muzzle the head has a hollow for the lower jaw, which fills it when
// the mouth is closed
const glm::vec3 kChin = glm::normalize(glm::vec3(0.0f, -0.8f, 0.6f));
// The haunches bulge out at the lower front of the body (left, the right one
// mirrored)
const glm::vec3 kHaunch = glm::normalize(glm::vec3(0.75f, -0.55f, 0.4f));

// Colors (linear)
const glm::vec3 kBackColor(0.12f, 0.062f, 0.03f);
const glm::vec3 kBellyColor(0.19f, 0.11f, 0.055f);
const glm::vec3 kHeadColor(0.14f, 0.075f, 0.036f);
const glm::vec3 kMuzzleColor(0.09f, 0.05f, 0.028f);
const glm::vec3 kSkinColor(0.055f, 0.034f, 0.024f);  // palms and soles
const glm::vec3 kTailColor(0.12f, 0.085f, 0.062f);
const glm::vec3 kNoseColor(0.015f, 0.012f, 0.011f);
const glm::vec3 kTeethColor(0.62f, 0.2f,
                            0.035f);  // the enamel of beaver teeth is orange
const glm::vec3 kMouthColor(0.1f, 0.018f, 0.015f);
const glm::vec3 kTongueColor(0.42f, 0.12f, 0.11f);
const glm::vec3 kClawColor(0.035f, 0.03f, 0.025f);

float SmoothMin(float a, float b, float k) {
  float h = std::clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
  return glm::mix(b, a, h) - k * h * (1.0f - h);
}

/**
 * Direction of the fur on a surface with this normal: down the surface (along
 * the wanted direction as far as possible)
 */
glm::vec3 CombAlong(glm::vec3 normal, glm::vec3 wanted) {
  glm::vec3 tangent = wanted - normal * glm::dot(wanted, normal);
  return glm::length(tangent) > 1e-4f
             ? glm::normalize(tangent)
             : glm::normalize(glm::cross(normal, glm::vec3(1.0f, 0.0f, 0.0f)) +
                              glm::vec3(0.0f, -1e-3f, 0.0f));
}

float BodyRadius(glm::vec3 direction) {
  // Wider at the bottom, narrower towards the neck, the belly bulges forward
  float width = 1.0f + 0.16f * glm::smoothstep(0.1f, -0.8f, direction.y) -
                0.3f * glm::smoothstep(0.25f, 0.95f, direction.y);
  float radius =
      1.0f /
      glm::length(direction / (kBodyRadii * glm::vec3(width, 1.0f, width)));
  radius += 0.05f * std::max(direction.z, 0.0f) *
            glm::smoothstep(0.5f, -0.3f, direction.y);
  // Haunches: the thighs of the hind legs it sits on
  glm::vec3 haunch(kHaunch.x * (direction.x < 0.0f ? -1.0f : 1.0f), kHaunch.y,
                   kHaunch.z);
  radius += 0.075f * glm::smoothstep(0.78f, 0.98f, glm::dot(direction, haunch));
  if (direction.y < 0.0f) {
    radius = SmoothMin(
        radius, (kBodyCenter.y - kSeatHeight) / std::max(-direction.y, 1e-3f),
        0.05f);
  }
  return radius;
}

glm::vec3 BodyPoint(glm::vec3 direction) {
  return kBodyCenter + direction * BodyRadius(direction);
}

float HeadRadius(glm::vec3 direction) {
  // The muzzle is narrower than the skull and blunt
  float taper = 1.0f - 0.3f * glm::smoothstep(0.3f, 0.95f, direction.z);
  float radius =
      1.0f /
      glm::length(direction / (kHeadRadii * glm::vec3(taper, taper, 1.0f)));
  float muzzle = glm::smoothstep(0.72f, 1.0f, glm::dot(direction, kMuzzle));
  float chin = glm::smoothstep(0.88f, 0.98f, glm::dot(direction, kChin));
  return radius + 0.085f * std::pow(muzzle, 1.2f) - 0.03f * chin;
}

glm::vec3 HeadPoint(glm::vec3 direction) {
  return kHeadCenter + direction * HeadRadius(direction);
}

const float kEyeRadius = 0.024f;

glm::vec3 EyeDirection(bool left) {
  return glm::normalize(glm::vec3(0.6f * (left ? 1.0f : -1.0f), 0.38f, 0.7f));
}

/**
 * Small dark eye, shaded like the eyes of the bear (the color is the direction
 * in eye space, gaze along +z)
 */
void AddEye(FurryModelBuilder& builder, glm::vec3 center, glm::vec3 gaze,
            float radius, int part) {
  glm::vec3 x = glm::normalize(glm::cross(kUp, gaze));
  glm::vec3 y = glm::cross(gaze, x);
  builder.Sphere(16, 12, [&](glm::vec3 d) {
    glm::vec4 color(
        glm::vec3(glm::dot(d, x), glm::dot(d, y), glm::dot(d, gaze)) * 0.5f +
            0.5f,
        0.0f);
    return builder.Add(center + d * radius, d, color, SkinnedModel::kEye, part);
  });
}

/**
 * Ellipsoid with fur, combed along a direction
 */
void AddFurryEllipsoid(FurryModelBuilder& builder, glm::vec3 center,
                       glm::vec3 radii, glm::vec3 color, float fur_length,
                       glm::vec3 comb, int part) {
  std::size_t first = builder.Sphere(16, 12, [&](glm::vec3 d) {
    glm::vec3 normal = glm::normalize(d / radii);
    return builder.Add(center + d * radii, normal, glm::vec4(color, fur_length),
                       SkinnedModel::kFur, part, false,
                       CombAlong(normal, comb));
  });
  builder.GrowFur(first, builder.TriangleCount());
}

/**
 * Fingers or toes fanning out forward from a point, each with a claw at its tip
 * @param spread Angle between the outermost ones (radians)
 */
void AddDigits(FurryModelBuilder& builder, glm::vec3 base, int count,
               float spread, float length, float thickness, glm::vec3 color,
               int part) {
  for (int digit = 0; digit < count; ++digit) {
    float angle =
        spread *
        (static_cast<float>(digit) / static_cast<float>(count - 1) - 0.5f);
    glm::vec3 direction(std::sin(angle), -0.12f, std::cos(angle));
    direction = glm::normalize(direction);
    glm::vec3 x = glm::normalize(glm::cross(kUp, direction)),
              y = glm::cross(direction, x);
    glm::vec3 center = base + direction * (0.5f * length);
    builder.Sphere(10, 6, [&](glm::vec3 d) {
      glm::vec3 local =
          d * glm::vec3(thickness, 0.8f * thickness, 0.5f * length);
      glm::vec3 normal = glm::normalize(
          d / glm::vec3(thickness, 0.8f * thickness, 0.5f * length));
      return builder.Add(
          center + x * local.x + y * local.y + direction * local.z,
          x * normal.x + y * normal.y + direction * normal.z,
          glm::vec4(color, 0.0f), SkinnedModel::kFur, part);
    });
    glm::vec3 tip = base + direction * length;
    builder.Ellipsoid(
        tip + direction * 0.006f - y * 0.003f,
        glm::vec3(0.5f * thickness, 0.4f * thickness, 0.5f * thickness) +
            glm::abs(direction) * 0.006f,
        6, 4, glm::vec4(kClawColor, 0.0f), SkinnedModel::kClaw, part);
  }
}
}  // namespace

glm::vec3 BeaverModel::EyeCenter(bool left) {
  return HeadPoint(EyeDirection(left)) - EyeDirection(left) * 0.012f;
}

glm::vec3 BeaverModel::EyeGaze(bool left) {
  return glm::normalize(glm::vec3(0.5f * (left ? 1.0f : -1.0f), 0.08f, 0.86f));
}

BeaverModel BeaverModel::Build() {
  BeaverModel model;
  FurryModelBuilder builder(model);

  // Body: thick fur, darker on the back, lighter on the belly, combed down
  std::size_t first_body_triangle =
      builder.Sphere(56, 40, [&](glm::vec3 direction) {
        float belly = glm::smoothstep(-0.1f, 0.6f, direction.z) *
                      glm::smoothstep(0.7f, 0.2f, direction.y);
        glm::vec3 color = glm::mix(kBackColor, kBellyColor, belly);
        float length =
            glm::mix(0.75f, 0.55f, belly) *
            glm::smoothstep(-0.98f, -0.8f, direction.y);  // short where it sits
        return builder.Add(BodyPoint(direction), direction,
                           glm::vec4(color, length), SkinnedModel::kFur, kBody,
                           false, glm::vec3(0.0f));
      });
  std::size_t last_body_triangle = builder.TriangleCount();
  builder.SmoothNormals(first_body_triangle, last_body_triangle);
  builder.GrowFur(first_body_triangle, last_body_triangle);

  // Head: short fur, darker on the muzzle, combed from the nose to the back of
  // the head
  std::size_t first_head_triangle =
      builder.Sphere(48, 36, [&](glm::vec3 direction) {
        float muzzle =
            glm::smoothstep(0.8f, 1.0f, glm::dot(direction, kMuzzle));
        glm::vec3 color = glm::mix(kHeadColor, kMuzzleColor, muzzle);
        return builder.Add(HeadPoint(direction), direction,
                           glm::vec4(color, glm::mix(0.45f, 0.18f, muzzle)),
                           SkinnedModel::kFur, kHead, false, glm::vec3(0.0f));
      });
  std::size_t last_head_triangle = builder.TriangleCount();
  builder.SmoothNormals(first_head_triangle, last_head_triangle);
  builder.GrowFur(first_head_triangle, last_head_triangle);
  // The fur follows the smoothed normals: down on the body, backwards on the
  // head
  for (std::size_t v = 0; v < model.positions.size(); ++v) {
    model.combs[v] = CombAlong(model.normals[v], model.parts[v] == kBody
                                                     ? -kUp + 0.15f * kBackwards
                                                     : kBackwards - 0.3f * kUp);
  }

  // Wet black nose on top of the tip of the muzzle
  glm::vec3 nose_direction = glm::normalize(glm::vec3(0.0f, -0.12f, 1.0f));
  builder.Ellipsoid(HeadPoint(nose_direction) - nose_direction * 0.004f,
                    glm::vec3(0.038f, 0.026f, 0.026f), 14, 10,
                    glm::vec4(kNoseColor, 0.0f), SkinnedModel::kNose, kHead);

  // The mouth in the hollow under the muzzle: hidden by the lower jaw when it
  // is closed, dark when it opens
  glm::vec3 hollow = HeadPoint(kChin);
  builder.Ellipsoid(hollow + glm::vec3(0.0f, 0.004f, 0.012f),
                    glm::vec3(0.05f, 0.022f, 0.06f), 14, 10,
                    glm::vec4(kMouthColor, 0.0f), SkinnedModel::kMouth, kHead);

  // The famous orange front teeth: they come out under the upper lip at the
  // front of the muzzle and reach over the lower jaw
  glm::vec3 lip = HeadPoint(glm::normalize(glm::vec3(0.0f, -0.45f, 0.89f)));
  for (float side : {1.0f, -1.0f}) {
    builder.Ellipsoid(glm::vec3(0.0155f * side, lip.y - 0.012f, lip.z - 0.012f),
                      glm::vec3(0.0145f, 0.034f, 0.008f), 10, 8,
                      glm::vec4(kTeethColor, 0.0f), SkinnedModel::kTeeth,
                      kHead);
  }

  for (float side : {1.0f, -1.0f}) {
    // Small dark eyes high on the sides of the head (in the space of the eye,
    // the origin is its center)
    bool left = side > 0.0f;
    AddEye(builder, glm::vec3(0.0f), EyeGaze(left), kEyeRadius,
           left ? kEyeLeft : kEyeRight);

    // Small round ears
    glm::vec3 ear_direction =
        glm::normalize(glm::vec3(0.72f * side, 0.6f, -0.2f));
    AddFurryEllipsoid(builder, HeadPoint(ear_direction),
                      glm::vec3(0.022f, 0.036f, 0.032f), kHeadColor * 0.8f,
                      0.15f, -kUp, kHead);
  }

  // Lower jaw: fills the hollow under the muzzle, furred like the head so that
  // it looks like one with it when the mouth is closed; the tongue and the
  // lower teeth on top of it (in the space of the jaw, the origin is its hinge)
  glm::vec3 chin_center =
      hollow + glm::vec3(0.0f, -0.004f, -0.004f) - kBeaverJaw;
  AddFurryEllipsoid(builder, chin_center, glm::vec3(0.075f, 0.042f, 0.08f),
                    glm::mix(kHeadColor, kMuzzleColor, 0.4f), 0.35f,
                    kBackwards - kUp, kJaw);
  builder.Ellipsoid(chin_center + glm::vec3(0.0f, 0.03f, 0.0f),
                    glm::vec3(0.04f, 0.014f, 0.05f), 12, 8,
                    glm::vec4(kTongueColor, 0.0f), SkinnedModel::kTongue, kJaw);
  for (float side : {1.0f, -1.0f}) {
    builder.Ellipsoid(chin_center + glm::vec3(0.009f * side, 0.034f, 0.058f),
                      glm::vec3(0.009f, 0.016f, 0.006f), 8, 6,
                      glm::vec4(kTeethColor, 0.0f), SkinnedModel::kTeeth, kJaw);
  }

  // Arms (in the space of the shoulder, which is inside the chest): the upper
  // arm grows out of the body, the wrist and the hand with bare palm, five
  // fingers and claws at its end
  for (int part : {kArmLeft, kArmRight}) {
    AddFurryEllipsoid(builder, glm::vec3(0.0f, -0.06f, 0.025f),
                      glm::vec3(0.07f, 0.15f, 0.075f),
                      glm::mix(kBackColor, kBellyColor, 0.4f), 0.55f, -kUp,
                      part);
    AddFurryEllipsoid(builder, glm::vec3(0.0f, -0.175f, 0.07f),
                      glm::vec3(0.05f, 0.06f, 0.055f),
                      glm::mix(kBackColor, kBellyColor, 0.5f), 0.35f, -kUp,
                      part);
    builder.Ellipsoid(glm::vec3(0.0f, -0.212f, 0.088f),
                      glm::vec3(0.034f, 0.018f, 0.032f), 12, 8,
                      glm::vec4(kSkinColor, 0.0f), SkinnedModel::kFur, part);
    AddDigits(builder, glm::vec3(0.0f, -0.216f, 0.1f), 5, 1.0f, 0.04f, 0.008f,
              kSkinColor, part);
  }

  // Hind legs (in the space of the foot, the origin on the ground under the
  // heel): the shin rises into the haunch, the large foot lies flat on the
  // ground with five long toes webbed together and claws
  for (int part : {kFootLeft, kFootRight}) {
    AddFurryEllipsoid(builder, glm::vec3(0.0f, 0.085f, 0.0f),
                      glm::vec3(0.075f, 0.1f, 0.085f), kBackColor, 0.55f, -kUp,
                      part);
    builder.Ellipsoid(glm::vec3(0.0f, 0.022f, 0.08f),
                      glm::vec3(0.065f, 0.022f, 0.12f), 16, 8,
                      glm::vec4(kSkinColor, 0.0f), SkinnedModel::kFur, part);
    // The web between the toes: a flat fan
    builder.Ellipsoid(
        glm::vec3(0.0f, 0.012f, 0.19f), glm::vec3(0.1f, 0.01f, 0.085f), 16, 6,
        glm::vec4(kSkinColor * 0.8f, 0.0f), SkinnedModel::kFur, part);
    AddDigits(builder, glm::vec3(0.0f, 0.025f, 0.15f), 5, 0.95f, 0.13f, 0.014f,
              kSkinColor, part);
  }

  // Tail: a furry root that grows out of the back of the body (part of the
  // body) and the flat, oval paddle with scales lying on the ground behind it
  // (in the space of the tail, which turns at the end of the root)
  AddFurryEllipsoid(builder, glm::vec3(0.0f, 0.045f, -0.34f),
                    glm::vec3(0.085f, 0.048f, 0.17f), kBackColor, 0.45f,
                    kBackwards, kBody);
  const glm::vec3 paddle_center(0.0f, -0.008f, -0.18f),
      paddle_radii(0.15f, 0.022f, 0.26f);
  builder.Sphere(32, 16, [&](glm::vec3 d) {
    // Narrower where it joins the root
    float width = 0.5f + 0.5f * glm::smoothstep(0.95f, 0.2f, d.z);
    glm::vec3 position =
        paddle_center + d * paddle_radii * glm::vec3(width, 1.0f, 1.0f);
    // Rows of scales: darker grooves in a diamond pattern
    float u = (position.x + position.z) * 150.0f,
          v = (position.x - position.z) * 150.0f;
    float grooves = std::min(std::abs(std::sin(u)), std::abs(std::sin(v)));
    glm::vec3 color =
        kTailColor *
        glm::mix(0.55f, 1.15f, glm::smoothstep(0.0f, 0.35f, grooves));
    return builder.Add(position,
                       d / (paddle_radii * glm::vec3(width, 1.0f, 1.0f)),
                       glm::vec4(color, 0.0f), SkinnedModel::kFur, kTail);
  });
  return model;
}

}  // namespace bibo::scene
