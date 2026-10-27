#include "Bear.hpp"

#include "../Util/Console.hpp"
#include "../Util/Util.hpp"

using namespace bibo;
using namespace scene;

const char* const Bear::MODEL_FILE = "Model/BearCub/BearCub.glb";

namespace {
glm::vec3 startPosition(const aiScene* track) {
  // The node of the bear in the track is only used as start position (center of
  // the character capsule)
  aiNode* node = track ? track->mRootNode->FindNode("BearBody") : nullptr;
  if (!node) {
    util::fatalError(
        "The track has no node BearBody (start position of the bear)");
  }
  aiMatrix4x4 transformation = node->mTransformation;
  resources::Model::transformNode(&transformation, node);
  return glm::vec3(util::ConvertMatrix(transformation)[3]);
}

std::unique_ptr<resources::SkinnedModel> loadModel() {
  std::string error;
  std::unique_ptr<resources::SkinnedModel> model =
      resources::SkinnedModel::load(Bear::MODEL_FILE, error);
  if (!model) {
    util::fatalError(std::string("Could not load ") + Bear::MODEL_FILE + ": " +
                     error);
  }
  BearRig rig(model->skeleton);
  if (!rig.isValid()) {
    util::fatalError(std::string("The skeleton of ") + Bear::MODEL_FILE +
                     " has no bones " + rig.getMissingBones());
  }
  return model;
}
}  // namespace

Bear::Bear(GLFWwindow& window, const aiScene* assimpScene,
           physics::PhysicsWorld* physicsWorld,
           physics::CharacterController::HitCallback onHit)
    : Geometry(mat4(1.0f), window), model(loadModel()) {
  // The bear starts looking along -z
  character = std::make_unique<BearCharacter>(
      *physicsWorld, *model, startPosition(assimpScene),
      vec3(0.0f, 0.0f, -1.0f), std::move(onHit));
  renderer = std::make_unique<BearRenderer>(*model, STRAND_COUNT);
  modelMatrix[3] = vec4(character->getPosition(), 1.0f);
}

Bear::~Bear() = default;

physics::CharacterControllerDesc Bear::characterControllerDesc(vec3 position) {
  return BearCharacter::characterControllerDesc(position);
}

void Bear::update(float time_delta) {
  character->update(time_delta, input);
  modelMatrix[3] = vec4(character->getPosition(), 1.0f);
}

void Bear::uploadIfChanged() {
  if (uploadedVersion != character->getVersion()) {
    renderer->upload(character->getPositions(), character->getNormals(),
                     character->getFurOffsets(),
                     character->getCombDirections());
    uploadedVersion = character->getVersion();
  }
}

void Bear::draw(Shader* shader) {
  uploadIfChanged();
  renderer->drawSurface(shader);
}

void Bear::drawShaded(const BearRenderContext& context) {
  uploadIfChanged();
  renderer->drawShaded(context, character->getBoundsCenter());
}

vec3 Bear::getPosition() { return character->getPosition(); }

vec3 Bear::getDirection() { return character->getDirection(); }

bool Bear::isVisible(const ViewFrustum& frustum) {
  return frustum.sphereInFrustum(character->getBoundsCenter(),
                                 character->getBoundsRadius()) !=
         ViewFrustum::OUTSIDE;
}

void Bear::reset() {
  character->reset();
  modelMatrix[3] = vec4(character->getPosition(), 1.0f);
}

void Bear::knockDown(const physics::CharacterController::Hit& hit) {
  character->knockDown(hit);
}

bool Bear::isKnockedDown() const {
  return character->getState() != BearCharacter::State::CONTROL;
}

void Bear::setObstacles(const std::vector<fur::SphereCollider>& obstacles) {
  character->setObstacles(obstacles);
}
