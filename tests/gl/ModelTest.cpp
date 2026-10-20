#include <algorithm>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Resources/Model.hpp"
#include "Scene/Geometry.hpp"
#include "TestSupport.hpp"

using namespace bibo::test;
using bibo::Shader;
using bibo::Texture;
using bibo::resources::Mesh;
using bibo::resources::Model;
using bibo::scene::Geometry;
using bibo::scene::ViewFrustum;
using glm::mat4;
using glm::vec3;

TEST_CASE("The meshes of every node of the game are uploaded completely", "[gl][model]")
{
	GlContext gl;
	Shader shader(assetPath("Shader/main_shader.vert").string(), assetPath("Shader/main_shader.frag").string());
	const std::string name = GENERATE(from_range(gameNodeNames()));
	CAPTURE(name);

	Model model(&shader, &trackScene(), name.c_str(), nullptr);

	const auto& normalMapped = normalMappedNodeNames();
	const bool hasNormalMap = std::find(normalMapped.begin(), normalMapped.end(), name) != normalMapped.end();

	REQUIRE_FALSE(model.meshes.empty());
	for (Mesh* mesh : model.meshes)
	{
		REQUIRE(mesh->vertices != nullptr);
		REQUIRE(mesh->indices != nullptr);
		CHECK(mesh->normals != nullptr);
		if (hasNormalMap)
		{
			CHECK(mesh->texCoords != nullptr);
			CHECK(mesh->tangents != nullptr);
			CHECK(mesh->bitangents != nullptr);
		}
		CHECK(mesh->numFaces > 0);
		for (unsigned int i = 0; i < mesh->numFaces * 3; ++i)
		{
			if (mesh->indices[i] >= mesh->numVertices)
			{
				FAIL("Index " << i << " is " << mesh->indices[i] << ", but there are only " << mesh->numVertices << " vertices");
			}
		}
	}
	CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("A node that does not exist gives an empty model", "[gl][model]")
{
	GlContext gl;
	Shader shader(assetPath("Shader/main_shader.vert").string(), assetPath("Shader/main_shader.frag").string());

	Model model(&shader, &trackScene(), "DoesNotExist", nullptr);

	CHECK(model.meshes.empty());
}

TEST_CASE("A geometry is placed where its node is in the track", "[gl][model]")
{
	GlContext gl;
	Shader shader(assetPath("Shader/main_shader.vert").string(), assetPath("Shader/main_shader.frag").string());
	auto texture = Texture::createCheckerboard(16, 2, 0);
	const mat4 nodeMatrix = nodeModelMatrix(trackNode("Rock1"));

	SECTION("Loaded from the file")
	{
		Geometry rock(mat4(1.0f), &shader, gl.window(), assetPath(TRACK_FILE).string().c_str(), "Rock1", texture.get());
		CHECK_THAT(rock.modelMatrix, IsNear(nodeMatrix, 1e-4f));
		CHECK(rock.getPosition() == vec3(rock.modelMatrix[3]));
		CHECK_FALSE(rock.getHasNormalMap());
	}

	SECTION("The given model matrix is applied before the transformation of the node")
	{
		const mat4 scaled = glm::scale(mat4(1.0f), vec3(2.0f));
		Geometry rock(scaled, &shader, gl.window(), &trackScene(), "Rock1", texture.get());
		CHECK_THAT(rock.modelMatrix, IsNear(nodeMatrix * scaled, 1e-4f));
	}

	SECTION("With normal map")
	{
		auto normalMap = Texture::createCheckerboard(16, 2, 2);
		Geometry rock(mat4(1.0f), &shader, gl.window(), assetPath(TRACK_FILE).string().c_str(), "Rock1", texture.get(), normalMap.get());
		CHECK(rock.getHasNormalMap());

		shader.enable();
		rock.draw(&shader);
		CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
	}
}

TEST_CASE("A geometry with a physics body keeps the scale and the rotation of its node", "[gl][model][physics]")
{
	GlContext gl;
	Shader shader(assetPath("Shader/main_shader.vert").string(), assetPath("Shader/main_shader.frag").string());
	auto texture = Texture::createCheckerboard(16, 2, 0);
	bibo::physics::PhysicsWorld world;
	Geometry rock(mat4(1.0f), &shader, gl.window(), &trackScene(), "Rock1", texture.get());
	const mat4 nodeMatrix = rock.modelMatrix;

	SECTION("A static body that is not turned (like the rocks)")
	{
		rock.setPhysicsBody(&world, world.createStaticSphere(rock.getPosition() + vec3(0.1f, 0.2f, 0.0f), 0.7f));
		rock.update(1.0f / 60.0f);
		CHECK_THAT(rock.modelMatrix, IsNear(nodeMatrix, 1e-4f));
	}

	SECTION("A dynamic body follows the body")
	{
		bibo::physics::BodyHandle body = world.createDynamicBox(rock.getPosition(), vec3(0.5f), 1.0f);
		rock.setPhysicsBody(&world, body);
		world.resetBody(body, rock.getPosition() + vec3(0.0f, 3.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
		rock.update(1.0f / 60.0f);
		CHECK_THAT(rock.modelMatrix, IsNear(glm::translate(mat4(1.0f), vec3(0.0f, 3.0f, 0.0f)) * nodeMatrix, 1e-4f));
	}
}

TEST_CASE("The bounding sphere of a model contains all of its vertices", "[gl][model][frustum]")
{
	GlContext gl;
	Shader shader(assetPath("Shader/main_shader.vert").string(), assetPath("Shader/main_shader.frag").string());
	const std::string name = GENERATE(from_range(gameNodeNames()));
	CAPTURE(name);

	Model model(&shader, &trackScene(), name.c_str(), nullptr);

	const vec3 center = model.getBoundingSphereCenter();
	const float radius = model.getBoundingSphereRadius();
	CAPTURE(center, radius);
	REQUIRE(radius > 0.0f);
	float farthest = 0.0f;
	for (Mesh* mesh : model.meshes)
	{
		for (unsigned int i = 0; i < mesh->numVertices; ++i)
		{
			farthest = std::max(farthest, glm::distance(center, vec3(mesh->vertices[i * 3], mesh->vertices[i * 3 + 1], mesh->vertices[i * 3 + 2])));
		}
	}
	CHECK(farthest <= radius * 1.0001f);
	CHECK(farthest >= radius * 0.9999f); //and is not larger than needed
}

TEST_CASE("A geometry is visible if its bounding sphere is in the view frustum", "[gl][model][frustum]")
{
	GlContext gl;
	Shader shader(assetPath("Shader/main_shader.vert").string(), assetPath("Shader/main_shader.frag").string());
	Geometry rock(mat4(1.0f), &shader, gl.window(), &trackScene(), "Rock1", nullptr);
	const vec3 rockCenter = vec3(rock.modelMatrix * glm::vec4(rock.getModel()->getBoundingSphereCenter(), 1.0f));
	const vec3 camera = rockCenter + vec3(0.0f, 0.0f, 20.0f);

	auto frustumLookingAt = [&](vec3 lookAt) {
		ViewFrustum frustum;
		frustum.setCamInternals(60.0f, 4.0f / 3.0f, 0.1f, 200.0f);
		frustum.setCamDef(camera, lookAt, vec3(0.0f, 1.0f, 0.0f));
		return frustum;
	};

	CHECK(rock.isVisible(frustumLookingAt(rockCenter)));
	CHECK_FALSE(rock.isVisible(frustumLookingAt(camera + vec3(0.0f, 0.0f, 1.0f)))); //behind the camera
	CHECK_FALSE(rock.isVisible(frustumLookingAt(camera + vec3(1.0f, 0.0f, 0.0f)))); //next to the camera

	SECTION("The scale of the model matrix enlarges the bounding sphere")
	{
		//Scaled up so much that it surrounds the camera, which looks away from the center
		rock.modelMatrix = glm::translate(mat4(1.0f), rockCenter) * glm::scale(mat4(1.0f), vec3(100.0f)) * glm::translate(mat4(1.0f), -rockCenter) * rock.modelMatrix;
		CHECK(rock.isVisible(frustumLookingAt(camera + vec3(0.0f, 0.0f, 1.0f))));
	}
}
