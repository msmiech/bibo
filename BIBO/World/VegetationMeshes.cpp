#include "VegetationMeshes.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

#include "MeshBuilder.hpp"
#include "Noise.hpp"

namespace bibo::world {

namespace {
const float kTau = 6.28318531f;
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);

/**
 * Tapered cylinder along y, e.g. a trunk
 */
void AddTrunk(MeshBuilder& builder, float height, float base_radius,
              float top_radius, int sides, int rings, glm::vec3 bottom_color,
              glm::vec3 top_color, float bottom_occlusion, float flare) {
  std::uint32_t first =
      static_cast<std::uint32_t>(builder.mesh.vertices.size());
  float around = std::max(1.0f, std::round(base_radius * 7.0f));
  for (int k = 0; k <= rings; ++k) {
    float t = static_cast<float>(k) / static_cast<float>(rings);
    float y = height * t;
    float radius = glm::mix(base_radius, top_radius, t) +
                   flare * base_radius * std::exp(-y * 3.0f);
    for (int j = 0; j <= sides; ++j) {
      float angle = kTau * static_cast<float>(j) / static_cast<float>(sides);
      glm::vec3 direction(std::cos(angle), 0.0f, std::sin(angle));
      builder.Add(
          direction * radius + glm::vec3(0.0f, y, 0.0f), direction,
          glm::vec2(around * static_cast<float>(j) / static_cast<float>(sides),
                    y / 1.4f),
          glm::mix(bottom_color, top_color, t), kVegetationBark, 0.3f * t * t,
          0.15f, glm::mix(bottom_occlusion, 1.0f, t));
    }
  }
  for (int k = 0; k < rings; ++k) {
    for (int j = 0; j < sides; ++j) {
      std::uint32_t a = first + k * (sides + 1) + j;
      std::uint32_t b = a + sides + 1;
      builder.Quad(a, b, b + 1, a + 1);
    }
  }
}

/**
 * A branch of a conifer: a frond that leaves the trunk, droops towards its tip
 * and curls up a bit at the very end. Its cross section is a flat roof (the
 * needles go to the sides and down), its edges are serrated (the needle tufts).
 * Seen from both sides (the vegetation of trees is drawn without culling).
 */
void AddBranch(MeshBuilder& builder, noise::Random& random, float y,
               float angle, float length, float width, int segments,
               bool serrated, glm::vec3 color, float height_fraction,
               float lower_occlusion, float droop_factor) {
  glm::vec3 direction(std::cos(angle), 0.0f, std::sin(angle));
  glm::vec3 side(-direction.z, 0.0f, direction.x);
  float rise = 0.18f * length, droop = droop_factor * length;
  float twist = random.Range(-0.15f, 0.15f);
  std::uint32_t previous[3] = {};
  for (int s = 0; s <= segments; ++s) {
    float t = static_cast<float>(s) / static_cast<float>(segments);
    float curl = 0.25f * std::max(0.0f, t - 0.75f);
    glm::vec3 spine = direction * (0.12f + length * t) +
                      kUp * (y + rise * t - droop * t * t + length * curl);
    float shape = std::pow(
        std::sin(std::numbers::pi_v<float> * std::min(t * 0.92f + 0.08f, 1.0f)),
        0.6f);
    float half_width =
        0.5f * width * (0.35f + 0.65f * shape) * (1.0f - 0.85f * t * t * t);
    if (serrated && s % 2 == 1) {
      half_width *= 0.68f;
    }
    glm::vec3 across = glm::normalize(side + kUp * twist);
    glm::vec3 tangent =
        glm::normalize(direction * length + kUp * (rise - 2.0f * droop * t));
    glm::vec3 up = glm::normalize(glm::cross(tangent, across)) * -1.0f;
    if (up.y < 0.0f) {
      up = -up;
    }
    // Bent normals: the branch is lit like a part of a round crown
    glm::vec3 normal = glm::normalize(up + direction * (0.6f * t) + kUp * 0.2f);
    float sag = 0.22f * half_width;
    float occlusion = glm::mix(0.35f, 1.0f, std::pow(t, 0.7f)) *
                      glm::mix(lower_occlusion, 1.0f, height_fraction);
    glm::vec3 shade = color * glm::mix(0.7f, 1.15f, t);
    float wind = t * (0.3f + 0.7f * height_fraction);
    std::uint32_t current[3] = {
        builder.Add(spine - across * half_width - kUp * sag,
                    normal - across * 0.35f, glm::vec2(0.0f, t * length), shade,
                    kVegetationNeedles, wind, 1.0f, occlusion),
        builder.Add(spine + kUp * (0.12f * half_width), normal,
                    glm::vec2(0.5f, t * length), shade * 1.05f,
                    kVegetationNeedles, wind, 1.0f, occlusion),
        builder.Add(spine + across * half_width - kUp * sag,
                    normal + across * 0.35f, glm::vec2(1.0f, t * length), shade,
                    kVegetationNeedles, wind, 1.0f, occlusion)};
    if (s > 0) {
      builder.Quad(previous[0], current[0], current[1], previous[1]);
      builder.Quad(previous[1], current[1], current[2], previous[2]);
    }
    std::copy(current, current + 3, previous);
  }
}

/**
 * The branches of a conifer at one height, around the trunk
 */
void AddBranchWhorl(MeshBuilder& builder, noise::Random& random, float y,
                    float radius, int branches, int segments, bool serrated,
                    glm::vec3 color, float height_fraction,
                    float lower_occlusion, float droop) {
  float rotation = random.Range(0.0f, kTau);
  float width =
      std::max(0.35f, kTau * radius / static_cast<float>(branches) * 0.8f);
  for (int b = 0; b < branches; ++b) {
    float angle =
        rotation + kTau * (static_cast<float>(b) + random.Range(-0.2f, 0.2f)) /
                       static_cast<float>(branches);
    float length = radius * random.Range(0.82f, 1.1f);
    AddBranch(builder, random, y + random.Range(-0.15f, 0.15f), angle, length,
              width * random.Range(0.85f, 1.15f), segments, serrated,
              color * random.Range(0.9f, 1.1f), height_fraction,
              lower_occlusion, droop);
  }
}

/**
 * Dark cone inside of the crown: through the gaps between the branches the
 * inside of the tree is seen, not the sky
 */
void AddCore(MeshBuilder& builder, float bottom, float top, float radius,
             glm::vec3 color, int sides) {
  std::uint32_t apex = builder.Add(kUp * top, kUp, glm::vec2(0.0f), color,
                                   kVegetationNeedles, 0.8f, 0.0f, 0.3f);
  std::vector<std::uint32_t> ring;
  for (int i = 0; i <= sides; ++i) {
    float angle = kTau * static_cast<float>(i) / static_cast<float>(sides);
    glm::vec3 direction(std::cos(angle), 0.0f, std::sin(angle));
    ring.push_back(
        builder.Add(direction * radius + kUp * bottom, direction + kUp * 0.3f,
                    glm::vec2(static_cast<float>(i) / sides, 0.0f) * 3.0f,
                    color, kVegetationNeedles, 0.2f, 0.0f, 0.25f));
  }
  for (int i = 0; i < sides; ++i) {
    builder.Triangle(apex, ring[i], ring[i + 1]);
  }
}

struct SpruceShape {
  float height, radius, crown_start, trunk_radius;
  int whorls, tips;
  glm::vec3 needles;
  std::uint32_t seed;
};

VegetationMesh BuildSpruce(const SpruceShape& shape, int detail) {
  MeshBuilder builder;
  noise::Random random(shape.seed);
  const int sides[3] = {8, 5, 3};
  const int trunk_rings[3] = {6, 2, 1};
  AddTrunk(builder, shape.height * 0.96f, shape.trunk_radius,
           shape.trunk_radius * 0.08f, sides[detail], trunk_rings[detail],
           Rgb(150, 132, 120), Rgb(160, 140, 125), 0.45f, 0.45f);
  const int whorl_divisor[3] = {1, 2, 3};
  const int branch_count[3] = {shape.tips, shape.tips - 2, shape.tips - 3};
  const int segments[3] = {7, 3, 2};
  int whorls = std::max(4, shape.whorls / whorl_divisor[detail]);
  float bottom = shape.crown_start * shape.height, top = shape.height * 0.93f;
  if (detail < 2) {
    AddCore(builder, bottom, bottom + (top - bottom) * 0.8f,
            shape.radius * 0.32f, shape.needles * 0.55f, detail == 0 ? 8 : 5);
  }
  for (int w = 0; w < whorls; ++w) {
    float t = static_cast<float>(w) / static_cast<float>(whorls - 1);
    float y = bottom + (top - bottom) * std::pow(t, 0.9f);
    // Fewer whorls are a bit wider and broader, so the crown is as dense
    float radius =
        shape.radius * std::pow(1.0f - t, 0.9f) * random.Range(0.88f, 1.1f) +
        0.35f + 0.15f * static_cast<float>(detail);
    int branches =
        std::max(4, branch_count[detail] - static_cast<int>(t * 3.0f));
    AddBranchWhorl(builder, random, y, radius, branches, segments[detail],
                   detail == 0, shape.needles, y / shape.height, 0.5f, 0.42f);
  }
  // The leader at the tip
  AddBranch(builder, random, top - 0.3f, 0.0f, 0.01f, 0.4f, 1, false,
            shape.needles, 1.0f, 1.0f, 0.0f);
  std::uint32_t apex = builder.Add(glm::vec3(0.0f, shape.height, 0.0f), kUp,
                                   glm::vec2(0.5f, 1.0f), shape.needles,
                                   kVegetationNeedles, 1.0f, 0.5f, 1.0f);
  const int spike = 5;
  std::vector<std::uint32_t> base;
  for (int i = 0; i < spike; ++i) {
    float angle = kTau * static_cast<float>(i) / spike;
    glm::vec3 direction(std::cos(angle), 0.0f, std::sin(angle));
    glm::vec3 position = direction * 0.3f + glm::vec3(0.0f, top - 0.4f, 0.0f);
    base.push_back(builder.Add(position, direction + kUp * 0.5f,
                               glm::vec2(static_cast<float>(i) / spike, 0.0f),
                               shape.needles * 0.9f, kVegetationNeedles, 0.9f,
                               0.5f, 0.9f));
  }
  for (int i = 0; i < spike; ++i) {
    builder.Triangle(apex, base[i], base[(i + 1) % spike]);
  }
  return builder.Finish();
}

/**
 * Sphere (latitude and longitude) whose radius is changed by noise, e.g. a
 * clump of needles or leaves
 */
void AddBlob(MeshBuilder& builder, glm::vec3 center, glm::vec3 radii,
             int longitude, int latitude, std::uint32_t seed, float bumpiness,
             glm::vec3 color, VegetationMaterial material, float wind,
             float bottom_occlusion) {
  std::uint32_t first =
      static_cast<std::uint32_t>(builder.mesh.vertices.size());
  for (int i = 0; i <= latitude; ++i) {
    float polar = std::numbers::pi_v<float> * static_cast<float>(i) /
                  static_cast<float>(latitude);
    for (int j = 0; j <= longitude; ++j) {
      float azimuth = kTau * static_cast<float>(j % longitude) /
                      static_cast<float>(longitude);
      glm::vec3 direction(std::sin(polar) * std::cos(azimuth), std::cos(polar),
                          std::sin(polar) * std::sin(azimuth));
      float bump =
          1.0f +
          bumpiness * noise::Fbm(direction * 1.7f +
                                     glm::vec3(static_cast<float>(seed % 97u)),
                                 3, seed);
      glm::vec3 position = center + direction * radii * bump;
      // Soft lighting: the normal points away from the middle of the clump, a
      // bit up
      glm::vec3 normal = glm::normalize(direction + kUp * 0.35f);
      float occlusion =
          glm::mix(bottom_occlusion, 1.0f, 0.5f + 0.5f * direction.y);
      builder.Add(
          position, normal,
          glm::vec2(
              static_cast<float>(j) / static_cast<float>(longitude) * 2.0f,
              static_cast<float>(i) / static_cast<float>(latitude)),
          color * glm::mix(0.8f, 1.1f, 0.5f + 0.5f * direction.y), material,
          wind, direction.y > 0.2f ? 1.0f : 0.0f, occlusion);
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

/**
 * Scots pine: a tall bare trunk, orange at the top, and a flat crown of needle
 * pads on the ends of a few big branches
 */
VegetationMesh BuildPine(int detail) {
  MeshBuilder builder;
  noise::Random random(4242u);
  const float height = 17.0f;
  const int sides[3] = {8, 5, 3};
  AddTrunk(builder, height * 0.92f, 0.28f, 0.07f, sides[detail],
           detail == 0 ? 6 : 2, Rgb(140, 120, 105), Rgb(195, 140, 100), 0.6f,
           0.35f);
  const int pads[3] = {9, 5, 3};
  const int branch_count[3] = {8, 6, 5};
  const int segments[3] = {5, 2, 2};
  for (int p = 0; p < pads[detail]; ++p) {
    float t = static_cast<float>(p) / static_cast<float>(pads[detail] - 1);
    float angle = random.Range(0.0f, kTau);
    float distance = (1.0f - 0.7f * t) * random.Range(1.0f, 2.2f) *
                     (p == pads[detail] - 1 ? 0.0f : 1.0f);
    glm::vec3 center(
        std::cos(angle) * distance,
        glm::mix(0.52f, 0.94f, t) * height + random.Range(-0.3f, 0.3f),
        std::sin(angle) * distance);
    float radius = random.Range(2.0f, 2.9f) * (1.0f - 0.35f * t) *
                   (detail == 2 ? 1.25f : 1.0f);
    // A flat whorl of short fronds, moved to the end of its branch
    MeshBuilder pad;
    AddBranchWhorl(pad, random, 0.0f, radius, branch_count[detail],
                   segments[detail], detail == 0,
                   Rgb(82, 108, 70) * random.Range(0.9f, 1.1f),
                   center.y / height, 0.6f, 0.18f);
    if (detail == 0) {
      // A smaller layer on top
      AddBranchWhorl(pad, random, 0.35f, radius * 0.6f,
                     branch_count[detail] - 2, segments[detail], true,
                     Rgb(90, 118, 74), center.y / height, 0.7f, 0.1f);
    }
    std::uint32_t offset =
        static_cast<std::uint32_t>(builder.mesh.vertices.size());
    for (VegetationVertex vertex : pad.mesh.vertices) {
      vertex.position += center;
      builder.mesh.vertices.push_back(vertex);
    }
    for (std::uint32_t index : pad.mesh.indices) {
      builder.mesh.indices.push_back(index + offset);
    }
    if (detail < 2 && distance > 0.3f) {
      // The branch from the trunk to the pad
      glm::vec3 from(0.0f, center.y - 0.9f, 0.0f);
      glm::vec3 axis = center - from;
      glm::vec3 side = glm::normalize(glm::cross(axis, kUp)) * 0.08f;
      glm::vec3 lift = glm::normalize(glm::cross(side, axis)) * 0.08f;
      std::uint32_t corners[8];
      for (int k = 0; k < 4; ++k) {
        glm::vec3 around = (k == 0   ? side
                            : k == 1 ? lift
                            : k == 2 ? -side
                                     : -lift);
        corners[k] =
            builder.Add(from + around, around, glm::vec2(k * 0.25f, 0.0f),
                        Rgb(190, 130, 92), kVegetationBark, 0.3f, 0.5f, 0.6f);
        corners[k + 4] = builder.Add(
            center + around * 0.5f, around, glm::vec2(k * 0.25f, 1.0f),
            Rgb(190, 130, 92), kVegetationBark, 0.5f, 0.5f, 0.6f);
      }
      for (int k = 0; k < 4; ++k) {
        builder.Quad(corners[k], corners[(k + 1) % 4], corners[(k + 1) % 4 + 4],
                     corners[k + 4]);
      }
    }
  }
  return builder.Finish();
}

VegetationMesh BuildBush(int detail) {
  MeshBuilder builder;
  noise::Random random(777u);
  int blobs = detail == 0 ? 7 : 3;
  for (int b = 0; b < blobs; ++b) {
    float angle = random.Range(0.0f, kTau),
          distance = b == 0 ? 0.0f : random.Range(0.25f, 0.6f);
    float radius = (detail == 0 ? random.Range(0.35f, 0.55f) : 0.65f);
    glm::vec3 center(std::cos(angle) * distance,
                     radius * 0.8f + random.Range(0.0f, 0.35f),
                     std::sin(angle) * distance);
    AddBlob(builder, center, glm::vec3(radius, radius * 0.85f, radius),
            detail == 0 ? 9 : 6, detail == 0 ? 6 : 4, 300u + b, 0.3f,
            Rgb(72, 108, 58) * random.Range(0.85f, 1.15f), kVegetationLeaves,
            0.5f, 0.4f);
  }
  if (detail == 0) {
    // Red berries
    for (int i = 0; i < 9; ++i) {
      float angle = random.Range(0.0f, kTau), height = random.Range(0.4f, 1.0f);
      glm::vec3 position(std::cos(angle) * 0.62f, height,
                         std::sin(angle) * 0.62f);
      AddBlob(builder, position, glm::vec3(0.045f), 5, 3, 500u + i, 0.0f,
              Rgb(175, 22, 28), kVegetationPlain, 0.5f, 0.6f);
    }
  }
  return builder.Finish();
}

VegetationMesh BuildJuniper(int detail) {
  MeshBuilder builder;
  noise::Random random(31337u);
  const float radii[3] = {1.5f, 1.15f, 0.7f};
  for (int layer = 0; layer < 3; ++layer) {
    float y = 0.18f + 0.24f * static_cast<float>(layer);
    AddBranchWhorl(builder, random, y, radii[layer], detail == 0 ? 9 : 6,
                   detail == 0 ? 4 : 2, detail == 0, Rgb(88, 118, 104),
                   y / 0.9f, 0.5f, 0.3f);
  }
  // A low bush holds less snow than the trees
  for (VegetationVertex& vertex : builder.mesh.vertices) {
    vertex.parameters[2] = static_cast<std::uint8_t>(vertex.parameters[2] / 5);
  }
  return builder.Finish();
}

VegetationMesh BuildFern() {
  MeshBuilder builder;
  noise::Random random(2024u);
  const int fronds = 9, segments = 12;
  for (int f = 0; f < fronds; ++f) {
    float angle =
        kTau * static_cast<float>(f) / fronds + random.Range(-0.25f, 0.25f);
    float length = random.Range(0.7f, 1.0f);
    glm::vec3 out(std::cos(angle), 0.0f, std::sin(angle));
    glm::vec3 side(-out.z, 0.0f, out.x);
    glm::vec3 color = Rgb(70, 118, 52) * random.Range(0.85f, 1.15f);
    std::uint32_t previous[3] = {};
    for (int s = 0; s <= segments; ++s) {
      float t = static_cast<float>(s) / segments;
      glm::vec3 spine = out * (length * t) +
                        kUp * (0.02f + length * (0.85f * t - 0.8f * t * t));
      // Leaflets: the width alternates along the frond
      float width = 0.17f * length *
                    std::pow(std::sin(std::numbers::pi_v<float> *
                                      std::min(t * 1.1f, 1.0f)),
                             0.6f) *
                    (s % 2 == 0 ? 1.0f : 0.6f);
      glm::vec3 tangent =
          glm::normalize(out * length + kUp * (length * (0.85f - 1.6f * t)));
      glm::vec3 normal = glm::normalize(glm::cross(tangent, side)) * -1.0f;
      if (normal.y < 0.0f) {
        normal = -normal;
      }
      float droop = 0.05f * width / std::max(0.01f, 0.17f * length);
      std::uint32_t current[3] = {
          builder.Add(spine - side * width - kUp * droop, normal,
                      glm::vec2(0.0f, t * 3.0f), color, kVegetationLeaves, t,
                      0.1f, glm::mix(0.5f, 1.0f, t)),
          builder.Add(spine, normal, glm::vec2(0.5f, t * 3.0f), color * 0.9f,
                      kVegetationLeaves, t, 0.1f, glm::mix(0.5f, 1.0f, t)),
          builder.Add(spine + side * width - kUp * droop, normal,
                      glm::vec2(1.0f, t * 3.0f), color, kVegetationLeaves, t,
                      0.1f, glm::mix(0.5f, 1.0f, t))};
      if (s > 0) {
        builder.Quad(previous[0], current[0], current[1], previous[1]);
        builder.Quad(previous[1], current[1], current[2], previous[2]);
      }
      std::copy(current, current + 3, previous);
    }
  }
  return builder.Finish();
}

VegetationMesh BuildGrass(bool dry) {
  MeshBuilder builder;
  noise::Random random(dry ? 99u : 98u);
  int blades = dry ? 16 : 13;
  const int segments = 4;
  glm::vec3 base_color = dry ? Rgb(110, 98, 58) : Rgb(48, 78, 34);
  glm::vec3 tip_color = dry ? Rgb(196, 176, 118) : Rgb(128, 156, 74);
  for (int b = 0; b < blades; ++b) {
    float angle = random.Range(0.0f, kTau);
    glm::vec3 direction(std::cos(angle), 0.0f, std::sin(angle));
    glm::vec3 side(-direction.z, 0.0f, direction.x);
    glm::vec3 base = direction * random.Range(0.0f, 0.12f);
    float height = random.Range(0.26f, 0.52f) * (dry ? 1.15f : 1.0f);
    float lean = height * random.Range(0.25f, 0.6f);
    float width = random.Range(0.03f, 0.055f);
    glm::vec3 tint = glm::vec3(random.Range(0.85f, 1.1f));
    std::uint32_t previous[2] = {};
    for (int s = 0; s <= segments; ++s) {
      float t = static_cast<float>(s) / segments;
      glm::vec3 center = base + kUp * (height * t) + direction * (lean * t * t);
      float half_width = 0.5f * width * std::pow(1.0f - t, 0.7f) + 0.002f;
      // Normals mostly up: the blades are lit like the ground they grow on
      glm::vec3 tangent =
          glm::normalize(kUp * height + direction * (2.0f * lean * t));
      glm::vec3 normal = glm::normalize(
          glm::normalize(glm::cross(side, tangent)) + kUp * 0.8f);
      glm::vec3 color = glm::mix(base_color, tip_color, t) * tint;
      float occlusion = glm::mix(0.35f, 1.0f, t);
      std::uint32_t current[2] = {
          builder.Add(center - side * half_width, normal, glm::vec2(0.0f, t),
                      color, kVegetationGrass, t * t, 0.0f, occlusion),
          builder.Add(center + side * half_width, normal, glm::vec2(1.0f, t),
                      color, kVegetationGrass, t * t, 0.0f, occlusion)};
      if (s > 0) {
        builder.Quad(previous[0], current[0], current[1], previous[1]);
      }
      previous[0] = current[0];
      previous[1] = current[1];
    }
  }
  return builder.Finish();
}

/**
 * A mushroom: stem along y and a cap (dome) on top with gills below
 */
VegetationMesh BuildMushroom(float stem_height, float stem_radius, float bulge,
                             float cap_radius, float cap_height,
                             glm::vec3 stem_color, glm::vec3 cap_color,
                             glm::vec3 gill_color,
                             VegetationMaterial cap_material,
                             VegetationMaterial stem_material) {
  MeshBuilder builder;
  const int sides = 10, rings = 4;
  std::uint32_t first =
      static_cast<std::uint32_t>(builder.mesh.vertices.size());
  for (int k = 0; k <= rings; ++k) {
    float t = static_cast<float>(k) / rings;
    float radius =
        stem_radius * (1.0f + bulge * std::sin(std::numbers::pi_v<float> *
                                               (0.3f + 0.6f * (1.0f - t))));
    for (int j = 0; j <= sides; ++j) {
      float angle = kTau * static_cast<float>(j) / sides;
      glm::vec3 direction(std::cos(angle), 0.0f, std::sin(angle));
      builder.Add(direction * radius + kUp * (stem_height * t), direction,
                  glm::vec2(static_cast<float>(j) / sides, t), stem_color,
                  stem_material, 0.2f * t, 0.0f, glm::mix(0.6f, 0.85f, t));
    }
  }
  for (int k = 0; k < rings; ++k) {
    for (int j = 0; j < sides; ++j) {
      std::uint32_t a = first + k * (sides + 1) + j, b = a + sides + 1;
      builder.Quad(a, b, b + 1, a + 1);
    }
  }
  // Cap: a flattened dome
  const int cap_rings = 5, cap_sides = 14;
  std::uint32_t apex =
      builder.Add(kUp * (stem_height + cap_height), kUp, glm::vec2(0.5f),
                  cap_color, cap_material, 0.25f, 1.0f, 1.0f);
  std::vector<std::uint32_t> previous;
  for (int r = 1; r <= cap_rings; ++r) {
    float polar = 1.5707963f * static_cast<float>(r) / cap_rings;
    std::vector<std::uint32_t> current;
    for (int j = 0; j <= cap_sides; ++j) {
      float angle = kTau * static_cast<float>(j) / cap_sides;
      glm::vec3 direction(std::sin(polar) * std::cos(angle), std::cos(polar),
                          std::sin(polar) * std::sin(angle));
      glm::vec3 position = glm::vec3(
          direction.x * cap_radius,
          stem_height - 0.15f * cap_height + direction.y * cap_height * 1.15f,
          direction.z * cap_radius);
      glm::vec3 normal = glm::normalize(glm::vec3(direction.x / cap_radius,
                                                  direction.y / cap_height,
                                                  direction.z / cap_radius));
      // uv: position on the cap from above (for the spots)
      current.push_back(builder.Add(
          position, normal, glm::vec2(direction.x, direction.z) * 0.5f + 0.5f,
          cap_color, cap_material, 0.25f, 1.0f, 1.0f));
    }
    for (int j = 0; j < cap_sides; ++j) {
      if (previous.empty()) {
        builder.Triangle(apex, current[j], current[j + 1]);
      } else {
        builder.Quad(previous[j], current[j], current[j + 1], previous[j + 1]);
      }
    }
    previous = current;
  }
  // Gills: from the rim of the cap to the stem
  std::vector<std::uint32_t> rim, inner;
  for (int j = 0; j <= cap_sides; ++j) {
    float angle = kTau * static_cast<float>(j) / cap_sides;
    glm::vec3 direction(std::cos(angle), 0.0f, std::sin(angle));
    rim.push_back(builder.Add(
        direction * cap_radius + kUp * (stem_height - 0.15f * cap_height), -kUp,
        glm::vec2(0.0f), gill_color, kVegetationPlain, 0.25f, 0.0f, 0.5f));
    inner.push_back(builder.Add(
        direction * stem_radius + kUp * (stem_height + 0.1f * cap_height), -kUp,
        glm::vec2(0.0f), gill_color * 0.7f, kVegetationPlain, 0.25f, 0.0f,
        0.3f));
  }
  for (int j = 0; j < cap_sides; ++j) {
    builder.Quad(rim[j], inner[j], inner[j + 1], rim[j + 1]);
  }
  return builder.Finish();
}

/**
 * Rock: a subdivided icosahedron, its radius changed by noise
 */
VegetationMesh BuildStone(glm::vec3 scale, float roughness, bool sharp,
                          std::uint32_t seed, int subdivisions,
                          glm::vec3 color) {
  std::vector<glm::vec3> positions;
  std::vector<std::uint32_t> triangles;
  const float golden = (1.0f + std::sqrt(5.0f)) / 2.0f;
  positions = {{-1, golden, 0},  {1, golden, 0},   {-1, -golden, 0},
               {1, -golden, 0},  {0, -1, golden},  {0, 1, golden},
               {0, -1, -golden}, {0, 1, -golden},  {golden, 0, -1},
               {golden, 0, 1},   {-golden, 0, -1}, {-golden, 0, 1}};
  triangles = {0, 11, 5, 0, 5,  1,  0,  1,  7,  0,  7, 10, 0, 10, 11,
               1, 5,  9, 5, 11, 4,  11, 10, 2,  10, 7, 6,  7, 1,  8,
               3, 9,  4, 3, 4,  2,  3,  2,  6,  3,  6, 8,  3, 8,  9,
               4, 9,  5, 2, 4,  11, 6,  2,  10, 8,  6, 7,  9, 8,  1};
  for (glm::vec3& position : positions) {
    position = glm::normalize(position);
  }
  for (int s = 0; s < subdivisions; ++s) {
    std::vector<std::uint32_t> next;
    std::map<std::uint64_t, std::uint32_t> middles;
    auto middle = [&](std::uint32_t a, std::uint32_t b) {
      std::uint64_t key =
          (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
      auto found = middles.find(key);
      if (found != middles.end()) {
        return found->second;
      }
      positions.push_back(glm::normalize(positions[a] + positions[b]));
      std::uint32_t index = static_cast<std::uint32_t>(positions.size() - 1);
      middles[key] = index;
      return index;
    };
    for (std::size_t t = 0; t < triangles.size(); t += 3) {
      std::uint32_t a = triangles[t], b = triangles[t + 1],
                    c = triangles[t + 2];
      std::uint32_t ab = middle(a, b), bc = middle(b, c), ca = middle(c, a);
      next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
    }
    triangles.swap(next);
  }
  MeshBuilder builder;
  for (const glm::vec3& direction : positions) {
    float bump = noise::Fbm(direction * 1.5f, 4, seed);
    float detail = noise::Fbm(direction * 5.0f, 2, seed + 1u);
    float radius = sharp ? 1.0f + roughness * (0.6f - 2.0f * std::abs(bump)) +
                               0.04f * detail
                         : 1.0f + roughness * bump + 0.05f * detail;
    // Flat bottom: rocks lie on the ground
    glm::vec3 position = direction * radius * scale * 0.6f;
    position.y = std::max(position.y, -0.35f * scale.y);
    position.y += 0.2f * scale.y;
    float occlusion =
        std::clamp(0.6f + 1.2f * (radius - 1.0f), 0.35f, 1.0f) *
        glm::mix(0.55f, 1.0f, glm::smoothstep(-0.6f, 0.4f, direction.y));
    builder.Add(position, direction, glm::vec2(0.0f),
                color * glm::mix(0.85f, 1.1f, 0.5f + 0.5f * detail),
                kVegetationStone, 0.0f, 1.0f, occlusion);
  }
  for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
    // faces outwards (the normals are the directions yet)
    builder.Triangle(triangles[t], triangles[t + 1], triangles[t + 2]);
  }
  builder.SmoothNormals(0, 0);
  return builder.Finish();
}

void AddDisk(MeshBuilder& builder, glm::vec3 center, glm::vec3 normal,
             glm::vec3 a, glm::vec3 b, float radius, int sides, glm::vec3 color,
             float wind) {
  std::uint32_t middle = builder.Add(center, normal, glm::vec2(0.5f), color,
                                     kVegetationWood, wind, 0.6f, 0.8f);
  std::vector<std::uint32_t> ring;
  for (int j = 0; j <= sides; ++j) {
    float angle = kTau * static_cast<float>(j) / static_cast<float>(sides);
    ring.push_back(builder.Add(
        center + (a * std::cos(angle) + b * std::sin(angle)) * radius, normal,
        glm::vec2(std::cos(angle), std::sin(angle)) * 0.5f + 0.5f, color,
        kVegetationWood, wind, 0.6f, 0.8f));
  }
  for (int j = 0; j < sides; ++j) {
    builder.Triangle(middle, ring[j], ring[j + 1]);
  }
}

VegetationMesh BuildLog(int detail) {
  MeshBuilder builder;
  const int sides = detail == 0 ? 10 : 6, segments = detail == 0 ? 6 : 2;
  const float length = 4.4f, base_radius = 0.33f;
  std::uint32_t first =
      static_cast<std::uint32_t>(builder.mesh.vertices.size());
  for (int k = 0; k <= segments; ++k) {
    float t = static_cast<float>(k) / segments;
    float x = (t - 0.5f) * length;
    float radius = base_radius * (1.0f - 0.18f * t);
    for (int j = 0; j <= sides; ++j) {
      float angle = kTau * static_cast<float>(j) / sides;
      glm::vec3 direction(0.0f, std::cos(angle), std::sin(angle));
      float bump = 1.0f + 0.06f * noise::Fbm(glm::vec3(x * 1.3f, direction.y,
                                                       direction.z),
                                             2, 77u);
      glm::vec3 position =
          glm::vec3(x, base_radius, 0.0f) + direction * radius * bump;
      builder.Add(position, direction,
                  glm::vec2(static_cast<float>(j) / sides * 3.0f, x / 1.4f),
                  Rgb(150, 125, 105), kVegetationBark, 0.0f,
                  direction.y > 0.3f ? 1.0f : 0.0f,
                  glm::mix(0.45f, 1.0f, 0.5f + 0.5f * direction.y));
    }
  }
  for (int k = 0; k < segments; ++k) {
    for (int j = 0; j < sides; ++j) {
      std::uint32_t a = first + k * (sides + 1) + j, b = a + sides + 1;
      builder.Quad(a, b, b + 1, a + 1);
    }
  }
  AddDisk(builder, glm::vec3(-length * 0.5f, base_radius, 0.0f),
          glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1),
          base_radius, sides, Rgb(185, 150, 110), 0.0f);
  AddDisk(builder, glm::vec3(length * 0.5f, base_radius, 0.0f),
          glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1),
          base_radius * 0.82f, sides, Rgb(185, 150, 110), 0.0f);
  return builder.Finish();
}

VegetationMesh BuildStump(int detail) {
  MeshBuilder builder;
  const int sides = detail == 0 ? 14 : 7, rings = detail == 0 ? 5 : 2;
  const float height = 0.6f, radius = 0.42f;
  std::uint32_t first =
      static_cast<std::uint32_t>(builder.mesh.vertices.size());
  for (int k = 0; k <= rings; ++k) {
    float t = static_cast<float>(k) / rings;
    float y = height * t;
    for (int j = 0; j <= sides; ++j) {
      float angle = kTau * static_cast<float>(j) / sides;
      glm::vec3 direction(std::cos(angle), 0.0f, std::sin(angle));
      // Roots spread at the bottom
      float roots = 1.0f +
                    0.45f *
                        std::pow(std::max(0.0f, std::cos(5.0f * angle)), 2.0f) *
                        std::exp(-y * 7.0f) +
                    0.25f * std::exp(-y * 5.0f);
      builder.Add(direction * radius * roots + kUp * (y - 0.05f), direction,
                  glm::vec2(static_cast<float>(j) / sides * 3.0f, y / 1.4f),
                  Rgb(150, 125, 105), kVegetationBark, 0.0f, 0.3f,
                  glm::mix(0.5f, 0.95f, t));
    }
  }
  for (int k = 0; k < rings; ++k) {
    for (int j = 0; j < sides; ++j) {
      std::uint32_t a = first + k * (sides + 1) + j, b = a + sides + 1;
      builder.Quad(a, b, b + 1, a + 1);
    }
  }
  AddDisk(builder, kUp * (height - 0.05f), kUp, glm::vec3(1, 0, 0),
          glm::vec3(0, 0, 1), radius, sides, Rgb(190, 155, 112), 0.0f);
  return builder.Finish();
}
}  // namespace

std::vector<VegetationMesh> BuildModel(Model model) {
  std::vector<VegetationMesh> lods;
  switch (model) {
    case Model::kSpruceTall:
    case Model::kSpruceWide:
    case Model::kSpruceYoung: {
      SpruceShape shape;
      if (model == Model::kSpruceTall) {
        shape = {15.0f, 3.6f, 0.16f, 0.27f, 18, 8, Rgb(108, 128, 100), 11u};
      } else if (model == Model::kSpruceWide) {
        shape = {11.0f, 4.0f, 0.07f, 0.26f, 14, 9, Rgb(100, 124, 92), 12u};
      } else {
        shape = {6.5f, 2.2f, 0.05f, 0.14f, 10, 7, Rgb(116, 140, 100), 13u};
      }
      for (int detail = 0; detail < 3; ++detail) {
        lods.push_back(BuildSpruce(shape, detail));
      }
      break;
    }
    case Model::kPine:
      for (int detail = 0; detail < 3; ++detail) {
        lods.push_back(BuildPine(detail));
      }
      break;
    case Model::kBush:
      lods.push_back(BuildBush(0));
      lods.push_back(BuildBush(1));
      break;
    case Model::kJuniper:
      lods.push_back(BuildJuniper(0));
      lods.push_back(BuildJuniper(1));
      break;
    case Model::kFern:
      lods.push_back(BuildFern());
      break;
    case Model::kGrass:
      lods.push_back(BuildGrass(false));
      break;
    case Model::kGrassDry:
      lods.push_back(BuildGrass(true));
      break;
    case Model::kMushroomRed:
      lods.push_back(BuildMushroom(0.22f, 0.022f, 0.25f, 0.085f, 0.055f,
                                   Rgb(235, 230, 215), Rgb(200, 30, 18),
                                   Rgb(240, 232, 210), kVegetationCapSpotted,
                                   kVegetationStem));
      break;
    case Model::kMushroomBrown:
      lods.push_back(BuildMushroom(0.13f, 0.034f, 0.35f, 0.075f, 0.045f,
                                   Rgb(225, 210, 175), Rgb(120, 72, 38),
                                   Rgb(205, 190, 110), kVegetationPlain,
                                   kVegetationStem));
      break;
    case Model::kMushroomGlow:
      lods.push_back(BuildMushroom(0.12f, 0.009f, 0.1f, 0.036f, 0.035f,
                                   Rgb(160, 230, 220), Rgb(90, 255, 230),
                                   Rgb(120, 220, 210), kVegetationGlow,
                                   kVegetationGlow));
      break;
    case Model::kStoneRound:
      for (int detail = 0; detail < 3; ++detail) {
        lods.push_back(BuildStone(glm::vec3(1.0f, 0.8f, 0.9f), 0.22f, false,
                                  41u, 3 - detail, Rgb(150, 146, 138)));
      }
      break;
    case Model::kStoneFlat:
      for (int detail = 0; detail < 3; ++detail) {
        lods.push_back(BuildStone(glm::vec3(1.15f, 0.5f, 1.0f), 0.2f, false,
                                  42u, 3 - detail, Rgb(140, 136, 130)));
      }
      break;
    case Model::kStoneSharp:
      for (int detail = 0; detail < 3; ++detail) {
        lods.push_back(BuildStone(glm::vec3(0.85f, 1.25f, 0.8f), 0.35f, true,
                                  43u, 3 - detail, Rgb(132, 130, 128)));
      }
      break;
    case Model::kStoneStanding:
      // Tall and narrow, set up by people long ago
      for (int detail = 0; detail < 3; ++detail) {
        lods.push_back(BuildStone(glm::vec3(0.6f, 2.6f, 0.42f), 0.16f, false,
                                  44u, 3 - detail, Rgb(128, 127, 122)));
      }
      break;
    case Model::kLog:
      lods.push_back(BuildLog(0));
      lods.push_back(BuildLog(1));
      break;
    case Model::kStump:
      lods.push_back(BuildStump(0));
      lods.push_back(BuildStump(1));
      break;
    case Model::kCount:
      break;
  }
  return lods;
}

}  // namespace bibo::world
