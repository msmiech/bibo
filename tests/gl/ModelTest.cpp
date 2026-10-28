#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Resources/Model.hpp"
#include "Scene/Geometry.hpp"
#include "TestSupport.hpp"

using bibo::test::AssetPath;
using bibo::test::GameNodeNames;
using bibo::test::GlContext;
using bibo::test::IsNear;
using bibo::test::kTrackFile;
using bibo::test::NodeModelMatrix;
using bibo::test::NormalMappedNodeNames;
using bibo::test::TrackNode;
using bibo::test::TrackScene;

using bibo::Shader;
using bibo::Texture;
using bibo::resources::Mesh;
using bibo::resources::Model;
using bibo::scene::Geometry;
using bibo::scene::ViewFrustum;
using glm::mat4;
using glm::vec3;

TEST_CASE("The meshes of every node of the game are uploaded completely",
          "[gl][model]") {
  GlContext gl;
  Shader shader(AssetPath("Shader/main_shader.vert").string(),
                AssetPath("Shader/main_shader.frag").string());
  const std::string name = GENERATE(from_range(GameNodeNames()));
  CAPTURE(name);

  Model model(&shader, &TrackScene(), name.c_str(), nullptr);

  const auto& normal_mapped = NormalMappedNodeNames();
  const bool has_normal_map =
      std::ranges::find(normal_mapped, name) != normal_mapped.end();

  REQUIRE_FALSE(model.meshes.empty());
  for (Mesh* mesh : model.meshes) {
    REQUIRE(mesh->vertices != nullptr);
    REQUIRE(mesh->indices != nullptr);
    CHECK(mesh->normals != nullptr);
    if (has_normal_map) {
      CHECK(mesh->tex_coords != nullptr);
      CHECK(mesh->tangents != nullptr);
      CHECK(mesh->bitangents != nullptr);
    }
    CHECK(mesh->num_faces > 0);
    for (unsigned int i = 0; i < mesh->num_faces * 3; ++i) {
      if (mesh->indices[i] >= mesh->num_vertices) {
        FAIL("Index " << i << " is " << mesh->indices[i]
                      << ", but there are only " << mesh->num_vertices
                      << " vertices");
      }
    }
  }
  CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("A node that does not exist gives an empty model", "[gl][model]") {
  GlContext gl;
  Shader shader(AssetPath("Shader/main_shader.vert").string(),
                AssetPath("Shader/main_shader.frag").string());

  Model model(&shader, &TrackScene(), "DoesNotExist", nullptr);

  CHECK(model.meshes.empty());
}

TEST_CASE("A geometry is placed where its node is in the track",
          "[gl][model]") {
  GlContext gl;
  Shader shader(AssetPath("Shader/main_shader.vert").string(),
                AssetPath("Shader/main_shader.frag").string());
  auto texture = Texture::CreateCheckerboard(16, 2, 0);
  const mat4 node_matrix = NodeModelMatrix(TrackNode("Rock1"));

  SECTION("Loaded from the file") {
    Geometry rock(mat4(1.0f), &shader, gl.Window(),
                  AssetPath(kTrackFile).string().c_str(), "Rock1",
                  texture.get());
    CHECK_THAT(rock.model_matrix, IsNear(node_matrix, 1e-4f));
    CHECK(rock.GetPosition() == vec3(rock.model_matrix[3]));
    CHECK_FALSE(rock.GetHasNormalMap());
  }

  SECTION(
      "The given model matrix is applied before the transformation of the "
      "node") {
    const mat4 scaled = glm::scale(mat4(1.0f), vec3(2.0f));
    Geometry rock(scaled, &shader, gl.Window(), &TrackScene(), "Rock1",
                  texture.get());
    CHECK_THAT(rock.model_matrix, IsNear(node_matrix * scaled, 1e-4f));
  }

  SECTION("With normal map") {
    auto normal_map = Texture::CreateCheckerboard(16, 2, 2);
    Geometry rock(mat4(1.0f), &shader, gl.Window(),
                  AssetPath(kTrackFile).string().c_str(), "Rock1",
                  texture.get(), normal_map.get());
    CHECK(rock.GetHasNormalMap());

    shader.Enable();
    rock.Draw(&shader);
    CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());
  }
}

TEST_CASE(
    "A geometry with a physics body keeps the scale and the rotation of its "
    "node",
    "[gl][model][physics]") {
  GlContext gl;
  Shader shader(AssetPath("Shader/main_shader.vert").string(),
                AssetPath("Shader/main_shader.frag").string());
  auto texture = Texture::CreateCheckerboard(16, 2, 0);
  bibo::physics::PhysicsWorld world;
  Geometry rock(mat4(1.0f), &shader, gl.Window(), &TrackScene(), "Rock1",
                texture.get());
  const mat4 node_matrix = rock.model_matrix;

  SECTION("A static body that is not turned (like the rocks)") {
    rock.SetPhysicsBody(&world,
                        world.CreateStaticSphere(
                            rock.GetPosition() + vec3(0.1f, 0.2f, 0.0f), 0.7f));
    rock.Update(1.0f / 60.0f);
    CHECK_THAT(rock.model_matrix, IsNear(node_matrix, 1e-4f));
  }

  SECTION("A dynamic body follows the body") {
    bibo::physics::BodyHandle body =
        world.CreateDynamicBox(rock.GetPosition(), vec3(0.5f), 1.0f);
    rock.SetPhysicsBody(&world, body);
    world.ResetBody(body, rock.GetPosition() + vec3(0.0f, 3.0f, 0.0f),
                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    rock.Update(1.0f / 60.0f);
    CHECK_THAT(
        rock.model_matrix,
        IsNear(glm::translate(mat4(1.0f), vec3(0.0f, 3.0f, 0.0f)) * node_matrix,
               1e-4f));
  }
}

TEST_CASE("The bounding sphere of a model contains all of its vertices",
          "[gl][model][frustum]") {
  GlContext gl;
  Shader shader(AssetPath("Shader/main_shader.vert").string(),
                AssetPath("Shader/main_shader.frag").string());
  const std::string name = GENERATE(from_range(GameNodeNames()));
  CAPTURE(name);

  Model model(&shader, &TrackScene(), name.c_str(), nullptr);

  const vec3 center = model.GetBoundingSphereCenter();
  const float radius = model.GetBoundingSphereRadius();
  CAPTURE(center, radius);
  REQUIRE(radius > 0.0f);
  float farthest = 0.0f;
  for (Mesh* mesh : model.meshes) {
    for (unsigned int i = 0; i < mesh->num_vertices; ++i) {
      farthest = std::max(
          farthest, glm::distance(center, vec3(mesh->vertices[i * 3],
                                               mesh->vertices[i * 3 + 1],
                                               mesh->vertices[i * 3 + 2])));
    }
  }
  CHECK(farthest <= radius * 1.0001f);
  CHECK(farthest >= radius * 0.9999f);  // and is not larger than needed
}

TEST_CASE("A geometry is visible if its bounding sphere is in the view frustum",
          "[gl][model][frustum]") {
  GlContext gl;
  Shader shader(AssetPath("Shader/main_shader.vert").string(),
                AssetPath("Shader/main_shader.frag").string());
  Geometry rock(mat4(1.0f), &shader, gl.Window(), &TrackScene(), "Rock1",
                nullptr);
  const vec3 rock_center =
      vec3(rock.model_matrix *
           glm::vec4(rock.GetModel()->GetBoundingSphereCenter(), 1.0f));
  const vec3 camera = rock_center + vec3(0.0f, 0.0f, 20.0f);

  auto frustum_looking_at = [&](vec3 look_at) {
    ViewFrustum frustum;
    frustum.SetCamInternals(60.0f, 4.0f / 3.0f, 0.1f, 200.0f);
    frustum.SetCamDef(camera, look_at, vec3(0.0f, 1.0f, 0.0f));
    return frustum;
  };

  CHECK(rock.IsVisible(frustum_looking_at(rock_center)));
  // behind the camera
  CHECK_FALSE(
      rock.IsVisible(frustum_looking_at(camera + vec3(0.0f, 0.0f, 1.0f))));
  // next to the camera
  CHECK_FALSE(
      rock.IsVisible(frustum_looking_at(camera + vec3(1.0f, 0.0f, 0.0f))));

  SECTION("The scale of the model matrix enlarges the bounding sphere") {
    // Scaled up so much that it surrounds the camera, which looks away from the
    // center
    rock.model_matrix = glm::translate(mat4(1.0f), rock_center) *
                        glm::scale(mat4(1.0f), vec3(100.0f)) *
                        glm::translate(mat4(1.0f), -rock_center) *
                        rock.model_matrix;
    CHECK(rock.IsVisible(frustum_looking_at(camera + vec3(0.0f, 0.0f, 1.0f))));
  }
}
