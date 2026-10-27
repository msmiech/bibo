#include "TestSupport.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <assimp/Importer.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

#include "Resources/Model.hpp"
#include "Resources/SkinnedModel.hpp"
#include "Util/Util.hpp"
#include "World/TrackTerrain.hpp"
#include "World/WorldGenerator.hpp"

namespace bibo::test {
std::filesystem::path AssetPath(const std::string& relative_path) {
  return std::filesystem::path(BIBO_ASSET_DIR) / relative_path;
}

AssetWorkingDirectory::AssetWorkingDirectory()
    : previous_(std::filesystem::current_path()) {
  std::filesystem::current_path(BIBO_ASSET_DIR);
}

AssetWorkingDirectory::~AssetWorkingDirectory() {
  std::filesystem::current_path(previous_);
}

const std::vector<ShaderProgram>& GameShaderPrograms() {
  static const std::vector<ShaderProgram> kPrograms = {
      {"Shader/depth_shader.vert", "Shader/depth_shader.frag"},
      {"Shader/main_shader.vert", "Shader/main_shader.frag"},
      {"Shader/text_shader.vert", "Shader/text_shader.frag"},
      {"Shader/light_source_shader.vert", "Shader/light_source_shader.frag"},
      {"Shader/bear_skin.vert", "Shader/bear_skin.frag"},
      {"Shader/bear_fur.vert", "Shader/bear_fur.frag"},
      // The world (BIBO/World)
      {"Shader/fullscreen.vert", "Shader/material_generator.frag"},
      {"Shader/fullscreen.vert", "Shader/material_normals.frag"},
      {"Shader/fullscreen.vert", "Shader/sky_environment.frag"},
      {"Shader/sky.vert", "Shader/sky.frag"},
      {"Shader/terrain.vert", "Shader/terrain.frag"},
      {"Shader/terrain.vert", "Shader/terrain.frag", "LOD_DITHER"},
      {"Shader/vegetation.vert", "Shader/vegetation.frag"},
      {"Shader/vegetation.vert", "Shader/vegetation.frag", "LOD_DITHER"},
      {"Shader/vegetation_depth.vert", "Shader/depth_shader.frag"},
      {"Shader/water.vert", "Shader/water.frag"},
      {"Shader/snow.vert", "Shader/snow.frag"},
      // Post processing (BIBO/Render/Renderer.cpp)
      {"Shader/fullscreen.vert", "Shader/bright_pass.frag"},
      {"Shader/fullscreen.vert", "Shader/gaussian_blur_shader.frag"},
      {"Shader/fullscreen.vert", "Shader/luminance.frag"},
      {"Shader/fullscreen.vert", "Shader/exposure.frag"},
      {"Shader/fullscreen.vert", "Shader/post_processing_shader.frag"}};
  return kPrograms;
}

const std::vector<std::string>& GameNodeNames() {
  static const std::vector<std::string> kNames = {
      "BearBody", "Boden",  "FinishLine", "Torus",  "Water",  "Cube",
      "Rock1",    "Rock2",  "Rock3",      "Rock4",  "Rock5",  "Rock6",
      "Rock7",    "Rock8",  "Rock9",      "Rock10", "Rock11", "Rock12",
      "Pfeil0",   "Pfeil1", "Pfeil2",     "Pfeil3"};
  return kNames;
}

const std::vector<std::string>& NormalMappedNodeNames() {
  static const std::vector<std::string> kNames = {"Cube"};
  return kNames;
}

const aiScene& TrackScene() {
  static Assimp::Importer importer;
  // Same post processing as Model
  static const aiScene* scene = importer.ReadFile(
      AssetPath(kTrackFile).string(),
      aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
  if (!scene) {
    FAIL("Could not load " << kTrackFile << ": " << importer.GetErrorString());
  }
  return *scene;
}

const resources::SkinnedModel& BearModel() {
  static std::string error;
  static std::unique_ptr<resources::SkinnedModel> model =
      resources::SkinnedModel::Load(AssetPath(kBearFile).string(), error);
  if (!model) {
    FAIL("Could not load " << kBearFile << ": " << error);
  }
  return *model;
}

const world::WorldGenerator& SharedWorldGenerator() {
  static std::unique_ptr<world::WorldGenerator> generator = [] {
    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
    REQUIRE(world::TrackTerrain::LoadNodeMesh(TrackScene(), "Boden", vertices,
                                              indices));
    world::WorldGenerator::Description description;
    description.track =
        std::make_unique<world::TrackTerrain>(vertices, indices);
    glm::vec3 start = glm::vec3(NodeModelMatrix(TrackNode("BearBody"))[3]);
    description.start = glm::vec2(start.x, start.z);
    description.start_direction = glm::vec2(0.0f, -1.0f);
    description.checkpoints.assign(std::begin(scene::rules::kCheckpoints),
                                   std::end(scene::rules::kCheckpoints));
    glm::vec3 water;
    float water_radius;
    REQUIRE(world::TrackTerrain::NodeSphere(TrackScene(), "Water", water,
                                            water_radius));
    description.sea_level = water.y;
    std::vector<std::string> keep = {"FinishLine", "Cube",   "Torus", "Pfeil0",
                                     "Pfeil1",     "Pfeil2", "Pfeil3"};
    for (int i = 1; i <= 12; ++i) {
      keep.push_back("Rock" + std::to_string(i));
    }
    for (const std::string& name : keep) {
      world::KeepClear object;
      REQUIRE(world::TrackTerrain::NodeSphere(TrackScene(), name.c_str(),
                                              object.center, object.radius));
      description.keep_clear.push_back(object);
    }
    return std::make_unique<world::WorldGenerator>(std::move(description));
  }();
  return *generator;
}

aiNode& TrackNode(const std::string& name) {
  aiNode* node = TrackScene().mRootNode->FindNode(name.c_str());
  if (!node) {
    FAIL("The track has no node " << name);
  }
  return *node;
}

glm::mat4 NodeModelMatrix(aiNode& node) {
  aiMatrix4x4 transformation = node.mTransformation;
  resources::Model::TransformNode(&transformation, &node);
  return util::ConvertMatrix(transformation);
}

scene::rules::FinishLine TrackFinishLine() {
  aiNode& node = TrackNode("FinishLine");
  REQUIRE(node.mNumMeshes > 0);
  const aiMesh* mesh = TrackScene().mMeshes[node.mMeshes[0]];
  float half_length = 0.0f;
  for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
    half_length = std::max(half_length, std::abs(mesh->mVertices[i].x));
  }
  return scene::rules::MakeFinishLine(NodeModelMatrix(node), half_length);
}

Clipped Clip(const glm::mat4& view_projection, glm::vec3 point) {
  glm::dvec4 clipped = glm::dmat4(view_projection) * glm::dvec4(point, 1.0);
  if (clipped.w <= 0.0) {
    return Clipped::kOutside;  // behind the camera
  }
  // Normalized device coordinates: the visible volume is -1 to 1 on all axes
  glm::dvec3 ndc = glm::abs(glm::dvec3(clipped) / clipped.w);
  double largest = std::max({ndc.x, ndc.y, ndc.z});
  if (std::abs(largest - 1.0) < 1e-4) {
    return Clipped::kBorder;
  }
  return largest < 1.0 ? Clipped::kInside : Clipped::kOutside;
}

namespace {
std::string last_glfw_error;

void StoreGlfwError(int code, const char* description) {
  last_glfw_error = description;
}

bool InitGlfw() {
  static const bool kInitialized = [] {
    glfwSetErrorCallback(StoreGlfwError);
    if (!glfwInit()) {
      return false;
    }
    std::atexit(glfwTerminate);
    return true;
  }();
  return kInitialized;
}

void GLAPIENTRY CollectErrors(GLenum source, GLenum type, GLuint id,
                              GLenum severity, GLsizei length,
                              const GLchar* message, const void* user_param) {
  if (type == GL_DEBUG_TYPE_ERROR) {
    auto errors =
        static_cast<std::vector<std::string>*>(const_cast<void*>(user_param));
    errors->push_back(message);
  }
}
}  // namespace

GlContext::GlContext(int width, int height) {
  if (!InitGlfw()) {
    SKIP("GLFW could not be initialized (no display?): " << last_glfw_error);
  }

  // Same context as the game (Main.cpp), but invisible
  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
  glfw_window_ =
      glfwCreateWindow(width, height, "BIBO tests", nullptr, nullptr);
  if (!glfw_window_) {
    SKIP("No OpenGL 4.3 core context: " << last_glfw_error);
  }
  glfwMakeContextCurrent(glfw_window_);

  glewExperimental = GL_TRUE;
  GLenum glew_status = glewInit();
#ifdef GLEW_ERROR_NO_GLX_DISPLAY
  // On Wayland there is no GLX display, but the OpenGL functions were loaded
  // nevertheless
  if (glew_status == GLEW_ERROR_NO_GLX_DISPLAY) {
    glew_status = GLEW_OK;
  }
#endif
  if (glew_status != GLEW_OK) {
    glfwDestroyWindow(glfw_window_);
    FAIL("Could not load the OpenGL functions: "
         << glewGetErrorString(glew_status));
  }
  glGetError();  // glewInit can leave an error behind

  glEnable(GL_DEBUG_OUTPUT);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(CollectErrors, &reported_errors_);
}

GlContext::~GlContext() {
  glDebugMessageCallback(nullptr, nullptr);
  glfwMakeContextCurrent(nullptr);
  glfwDestroyWindow(glfw_window_);
}
}  // namespace bibo::test
