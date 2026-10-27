#include "CreatureMeshes.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

#include "../World/MeshBuilder.hpp"

namespace bibo::scene {

using world::MeshBuilder;
using world::Rgb;
using world::VegetationMaterial;

namespace {
const float kPi = glm::pi<float>();

/**
 * Ellipsoid made of rings of latitude, the color may change with the direction
 * from the center
 */
template <typename ColorFunction>
void AddEllipsoid(MeshBuilder& builder, glm::vec3 center, glm::vec3 radii,
                  int longitude, int latitude, VegetationMaterial material,
                  ColorFunction color) {
  std::uint32_t first =
      static_cast<std::uint32_t>(builder.mesh.vertices.size());
  for (int i = 0; i <= latitude; ++i) {
    float polar = kPi * static_cast<float>(i) / static_cast<float>(latitude);
    for (int j = 0; j <= longitude; ++j) {
      float azimuth = 2.0f * kPi * static_cast<float>(j % longitude) /
                      static_cast<float>(longitude);
      glm::vec3 direction(std::sin(polar) * std::cos(azimuth), std::cos(polar),
                          std::sin(polar) * std::sin(azimuth));
      // darker underneath
      float occlusion = glm::mix(0.6f, 1.0f, 0.5f + 0.5f * direction.y);
      builder.Add(center + direction * radii, direction / radii,
                  glm::vec2(0.0f), color(direction), material, 0.0f, 0.0f,
                  occlusion);
    }
  }
  for (int i = 0; i < latitude; ++i) {
    for (int j = 0; j < longitude; ++j) {
      std::uint32_t a = first + i * (longitude + 1) + j;
      std::uint32_t b = a + longitude + 1;
      builder.Quad(a, b, b + 1, a + 1);
    }
  }
}

void AddEllipsoid(MeshBuilder& builder, glm::vec3 center, glm::vec3 radii,
                  int longitude, int latitude, VegetationMaterial material,
                  glm::vec3 color) {
  AddEllipsoid(builder, center, radii, longitude, latitude, material,
               [color](glm::vec3) { return color; });
}

// The salmon: half its height and width along its length (z from -0.5 at the
// tail to 0.5 at the snout)
float SalmonProfile(float z) {
  float u = std::clamp(z + 0.5f, 0.0f, 1.0f);
  float shape =
      std::pow(std::max(0.0f, std::sin(kPi * std::pow(u, 0.8f))), 0.6f);
  return u < 0.5f ? std::max(shape, 0.22f) : shape;
}
const float kSalmonHeight = 0.15f, kSalmonWidth = 0.09f;

glm::vec3 SalmonColor(glm::vec3 direction) {
  // Dark blue back, pink sides, white belly
  if (direction.y > 0.0f) {
    return glm::mix(Rgb(238, 158, 142), Rgb(58, 78, 98),
                    glm::smoothstep(0.25f, 0.7f, direction.y));
  }
  return glm::mix(Rgb(238, 158, 142), Rgb(242, 240, 236),
                  glm::smoothstep(0.1f, 0.6f, -direction.y));
}

/**
 * A stretch of the body of the salmon between two places along its length, with
 * the pink flesh where it is cut
 * @param shift Added to the positions (the tail has its origin at its joint)
 */
void AddSalmonPart(MeshBuilder& builder, float from, float to, bool cap_from,
                   bool cap_to, float shift) {
  const int sides = 14, rings = 10;
  std::uint32_t first =
      static_cast<std::uint32_t>(builder.mesh.vertices.size());
  for (int k = 0; k <= rings; ++k) {
    float z =
        glm::mix(from, to, static_cast<float>(k) / static_cast<float>(rings));
    float profile = SalmonProfile(z);
    // Slope of the profile for the normals
    float slope = (SalmonProfile(z + 0.01f) - SalmonProfile(z - 0.01f)) / 0.02f;
    for (int j = 0; j <= sides; ++j) {
      float angle = 2.0f * kPi * static_cast<float>(j % sides) /
                    static_cast<float>(sides);
      glm::vec3 direction(std::cos(angle), std::sin(angle), 0.0f);
      glm::vec3 position(direction.x * kSalmonWidth * profile,
                         direction.y * kSalmonHeight * profile, z + shift);
      glm::vec3 normal(direction.x / kSalmonWidth, direction.y / kSalmonHeight,
                       0.0f);
      normal = glm::normalize(normal) - glm::vec3(0.0f, 0.0f, slope * 0.6f);
      builder.Add(position, normal, glm::vec2(0.0f), SalmonColor(direction),
                  world::kVegetationGlossy, 0.0f, 0.0f,
                  glm::mix(0.75f, 1.0f, 0.5f + 0.5f * direction.y));
    }
  }
  for (int k = 0; k < rings; ++k) {
    for (int j = 0; j < sides; ++j) {
      std::uint32_t a = first + k * (sides + 1) + j;
      std::uint32_t b = a + sides + 1;
      builder.Quad(a, b, b + 1, a + 1);
    }
  }
  auto cap = [&](float z, float facing) {
    float profile = SalmonProfile(z);
    std::uint32_t middle = builder.Add(
        glm::vec3(0.0f, 0.0f, z + shift), glm::vec3(0.0f, 0.0f, facing),
        glm::vec2(0.0f), Rgb(246, 128, 92), world::kVegetationPlain, 0.0f, 0.0f,
        0.9f);
    std::uint32_t ring[sides];
    for (int j = 0; j < sides; ++j) {
      float angle =
          2.0f * kPi * static_cast<float>(j) / static_cast<float>(sides);
      ring[j] = builder.Add(
          glm::vec3(std::cos(angle) * kSalmonWidth * profile,
                    std::sin(angle) * kSalmonHeight * profile, z + shift),
          glm::vec3(0.0f, 0.0f, facing), glm::vec2(0.0f), Rgb(236, 112, 80),
          world::kVegetationPlain, 0.0f, 0.0f, 0.9f);
    }
    for (int j = 0; j < sides; ++j) {
      builder.Triangle(middle, ring[j], ring[(j + 1) % sides]);
    }
  };
  if (cap_from) {
    cap(from, -1.0f);
  }
  if (cap_to) {
    cap(to, 1.0f);
  }
}

/**
 * A fin: flat triangles, seen from both sides
 */
void AddFin(MeshBuilder& builder, std::initializer_list<glm::vec3> points,
            glm::vec3 color) {
  std::vector<std::uint32_t> indices;
  for (glm::vec3 point : points) {
    indices.push_back(builder.Add(point, glm::vec3(1.0f, 0.0f, 0.0f),
                                  glm::vec2(0.0f), color,
                                  world::kVegetationGlossy, 0.0f, 0.0f, 1.0f));
  }
  for (std::size_t i = 1; i + 1 < indices.size(); ++i) {
    builder.Triangle(indices[0], indices[i], indices[i + 1]);
  }
}

/**
 * Exclamation mark: a stroke that gets thinner towards the bottom and a dot
 * under it, glowing
 */
world::VegetationMesh BuildQuestMarker(glm::vec3 color) {
  MeshBuilder builder;
  const glm::vec3 stroke_center(0.0f, 0.63f, 0.0f),
      stroke_radii(0.12f, 0.37f, 0.12f);
  std::uint32_t first =
      static_cast<std::uint32_t>(builder.mesh.vertices.size());
  const int longitude = 16, latitude = 14;
  for (int i = 0; i <= latitude; ++i) {
    float polar = kPi * static_cast<float>(i) / static_cast<float>(latitude);
    for (int j = 0; j <= longitude; ++j) {
      float azimuth = 2.0f * kPi * static_cast<float>(j % longitude) /
                      static_cast<float>(longitude);
      glm::vec3 direction(std::sin(polar) * std::cos(azimuth), std::cos(polar),
                          std::sin(polar) * std::sin(azimuth));
      float taper = glm::mix(0.45f, 1.0f, 0.5f + 0.5f * direction.y);
      glm::vec3 radii = stroke_radii * glm::vec3(taper, 1.0f, taper);
      builder.Add(stroke_center + direction * radii, direction / radii,
                  glm::vec2(0.0f), color, world::kVegetationGlow, 0.0f, 0.0f,
                  1.0f);
    }
  }
  for (int i = 0; i < latitude; ++i) {
    for (int j = 0; j < longitude; ++j) {
      std::uint32_t a = first + i * (longitude + 1) + j;
      std::uint32_t b = a + longitude + 1;
      builder.Quad(a, b, b + 1, a + 1);
    }
  }
  AddEllipsoid(builder, glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(0.1f), 12, 8,
               world::kVegetationGlow, color);
  return builder.Finish();
}

world::VegetationMesh BuildSalmonHead() {
  MeshBuilder builder;
  AddSalmonPart(builder, kSalmonHeadJoint, 0.5f, true, false, 0.0f);
  // Big round eyes
  const float eye_z = 0.38f;
  float profile = SalmonProfile(eye_z);
  for (float side : {1.0f, -1.0f}) {
    glm::vec3 eye(side * kSalmonWidth * profile * 0.8f,
                  kSalmonHeight * profile * 0.22f, eye_z);
    AddEllipsoid(builder, eye, glm::vec3(0.012f, 0.03f, 0.03f), 10, 6,
                 world::kVegetationPlain, Rgb(250, 250, 245));
    AddEllipsoid(builder, eye + glm::vec3(side * 0.01f, 0.003f, 0.004f),
                 glm::vec3(0.008f, 0.019f, 0.019f), 8, 5,
                 world::kVegetationGlossy, Rgb(15, 15, 20));
  }
  return builder.Finish();
}

world::VegetationMesh BuildSalmonBody() {
  MeshBuilder builder;
  AddSalmonPart(builder, kSalmonTailJoint, kSalmonHeadJoint, true, true, 0.0f);
  float top = kSalmonHeight * SalmonProfile(0.03f) * 0.95f;
  AddFin(builder,
         {glm::vec3(0.0f, top, -0.08f), glm::vec3(0.0f, top + 0.085f, -0.04f),
          glm::vec3(0.0f, top + 0.02f, 0.12f)},
         Rgb(62, 82, 104));
  return builder.Finish();
}

world::VegetationMesh BuildSalmonTail() {
  MeshBuilder builder;
  const float shift = -kSalmonTailJoint;
  AddSalmonPart(builder, -0.47f, kSalmonTailJoint, false, true, shift);
  // Forked tail fin
  AddFin(builder,
         {glm::vec3(0.0f, 0.0f, -0.43f + shift),
          glm::vec3(0.0f, 0.15f, -0.6f + shift),
          glm::vec3(0.0f, 0.02f, -0.53f + shift),
          glm::vec3(0.0f, -0.02f, -0.53f + shift),
          glm::vec3(0.0f, -0.15f, -0.6f + shift)},
         Rgb(70, 88, 110));
  return builder.Finish();
}
}  // namespace

world::VegetationMesh BuildCreatureModel(CreatureModel model) {
  switch (model) {
    case CreatureModel::kSalmonHead:
      return BuildSalmonHead();
    case CreatureModel::kSalmonBody:
      return BuildSalmonBody();
    case CreatureModel::kSalmonTail:
      return BuildSalmonTail();
    case CreatureModel::kDroplet: {
      MeshBuilder builder;
      AddEllipsoid(builder, glm::vec3(0.0f), glm::vec3(1.0f), 8, 6,
                   world::kVegetationGlossy, Rgb(215, 232, 245));
      return builder.Finish();
    }
    case CreatureModel::kQuestMarker:
      return BuildQuestMarker(Rgb(255, 196, 36));
    case CreatureModel::kQuestMarkerRepeatable:
      return BuildQuestMarker(Rgb(70, 160, 255));
    case CreatureModel::kCount:
      break;
  }
  return world::VegetationMesh();
}

}  // namespace bibo::scene
