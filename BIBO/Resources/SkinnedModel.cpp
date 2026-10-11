#include "SkinnedModel.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

using namespace bibo;
using namespace resources;
using animation::Transform;

namespace
{
	glm::mat4 toGlm(const aiMatrix4x4& m)
	{
		return glm::mat4(
			m.a1, m.b1, m.c1, m.d1,
			m.a2, m.b2, m.c2, m.d2,
			m.a3, m.b3, m.c3, m.d3,
			m.a4, m.b4, m.c4, m.d4);
	}

	glm::mat4 globalTransform(const aiNode* node)
	{
		glm::mat4 transform(1.0f);
		for (; node; node = node->mParent)
		{
			transform = toGlm(node->mTransformation) * transform;
		}
		return transform;
	}

	const aiNode* findMeshNode(const aiNode* node, unsigned int mesh)
	{
		for (unsigned int i = 0; i < node->mNumMeshes; ++i)
		{
			if (node->mMeshes[i] == mesh)
			{
				return node;
			}
		}
		for (unsigned int i = 0; i < node->mNumChildren; ++i)
		{
			if (const aiNode* found = findMeshNode(node->mChildren[i], mesh))
			{
				return found;
			}
		}
		return nullptr;
	}

	SkinnedModel::Surface surfaceOf(const aiScene* scene, const aiMesh* mesh)
	{
		std::string name = mesh->mMaterialIndex < scene->mNumMaterials ? scene->mMaterials[mesh->mMaterialIndex]->GetName().C_Str() : "";
		if (name.find("Eye") != std::string::npos)
			return SkinnedModel::EYE;
		if (name.find("Nose") != std::string::npos)
			return SkinnedModel::NOSE;
		if (name.find("Mouth") != std::string::npos)
			return SkinnedModel::MOUTH;
		if (name.find("Tongue") != std::string::npos)
			return SkinnedModel::TONGUE;
		if (name.find("Teeth") != std::string::npos)
			return SkinnedModel::TEETH;
		if (name.find("Claw") != std::string::npos)
			return SkinnedModel::CLAW;
		return SkinnedModel::FUR;
	}
}

std::unique_ptr<SkinnedModel> SkinnedModel::load(const std::string& file, std::string& error)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(file, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights);
	if (!scene || !scene->mRootNode)
	{
		error = importer.GetErrorString();
		return nullptr;
	}

	//The skeleton is the node hierarchy below the topmost bone (all bones have the names of their nodes)
	std::set<std::string> boneNames;
	for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
	{
		for (unsigned int b = 0; b < scene->mMeshes[m]->mNumBones; ++b)
		{
			boneNames.insert(scene->mMeshes[m]->mBones[b]->mName.C_Str());
		}
	}
	if (boneNames.empty())
	{
		error = "the model has no bones";
		return nullptr;
	}
	const aiNode* rootBone = scene->mRootNode->FindNode(boneNames.begin()->c_str());
	if (!rootBone)
	{
		error = "there is no node for the bone " + *boneNames.begin();
		return nullptr;
	}
	while (rootBone->mParent && boneNames.count(rootBone->mParent->mName.C_Str()))
	{
		rootBone = rootBone->mParent;
	}

	auto model = std::make_unique<SkinnedModel>();
	animation::Skeleton& skeleton = model->skeleton;
	skeleton.rootParent = globalTransform(rootBone->mParent);
	std::function<void(const aiNode*, int)> addBones = [&](const aiNode* node, int parent) {
		animation::Bone bone;
		bone.name = node->mName.C_Str();
		bone.parent = parent;
		bone.bindLocal = Transform::fromMatrix(toGlm(node->mTransformation));
		skeleton.bones.push_back(bone);
		int index = static_cast<int>(skeleton.bones.size()) - 1;
		for (unsigned int i = 0; i < node->mNumChildren; ++i)
		{
			addBones(node->mChildren[i], index);
		}
	};
	addBones(rootBone, -1);
	//The node transforms are the bind pose (the rest position of the armature)
	skeleton.computeInverseBind();

	for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
	{
		const aiMesh* mesh = scene->mMeshes[m];
		if (!mesh->HasPositions() || !mesh->HasFaces())
		{
			continue;
		}
		const aiNode* node = findMeshNode(scene->mRootNode, m);
		glm::mat4 toModel = node ? globalTransform(node) : glm::mat4(1.0f);
		glm::mat3 normalToModel = glm::transpose(glm::inverse(glm::mat3(toModel)));
		unsigned int firstVertex = static_cast<unsigned int>(model->positions.size());
		Surface surface = surfaceOf(scene, mesh);

		for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
		{
			const aiVector3D& p = mesh->mVertices[i];
			model->positions.push_back(glm::vec3(toModel * glm::vec4(p.x, p.y, p.z, 1.0f)));
			glm::vec3 normal = mesh->HasNormals() ? glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z) : glm::vec3(0.0f, 1.0f, 0.0f);
			model->normals.push_back(glm::normalize(normalToModel * normal));
			if (mesh->HasVertexColors(0))
			{
				const aiColor4D& c = mesh->mColors[0][i];
				model->colors.push_back(glm::clamp(glm::vec4(c.r, c.g, c.b, c.a), 0.0f, 1.0f));
			}
			else
			{
				model->colors.push_back(glm::vec4(0.2f, 0.12f, 0.06f, surface == FUR ? 1.0f : 0.0f));
			}
			model->surfaces.push_back(static_cast<float>(surface));
		}

		//The 4 largest weights of every vertex
		std::vector<std::vector<std::pair<float, int>>> influences(mesh->mNumVertices);
		for (unsigned int b = 0; b < mesh->mNumBones; ++b)
		{
			const aiBone* bone = mesh->mBones[b];
			int index = skeleton.find(bone->mName.C_Str());
			if (index < 0)
			{
				continue;
			}
			for (unsigned int w = 0; w < bone->mNumWeights; ++w)
			{
				if (bone->mWeights[w].mVertexId < mesh->mNumVertices && bone->mWeights[w].mWeight > 0.0f)
				{
					influences[bone->mWeights[w].mVertexId].push_back({ bone->mWeights[w].mWeight, index });
				}
			}
		}
		for (auto& vertexInfluences : influences)
		{
			std::sort(vertexInfluences.begin(), vertexInfluences.end(), std::greater<>());
			glm::ivec4 joints(0);
			glm::vec4 weights(0.0f);
			for (std::size_t i = 0; i < vertexInfluences.size() && i < 4; ++i)
			{
				joints[static_cast<int>(i)] = vertexInfluences[i].second;
				weights[static_cast<int>(i)] = vertexInfluences[i].first;
			}
			float sum = weights.x + weights.y + weights.z + weights.w;
			if (sum > 0.0f)
			{
				weights /= sum;
			}
			else
			{
				weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f); //not skinned: moves with the root bone
			}
			model->joints.push_back(joints);
			model->weights.push_back(weights);
		}

		Part part{ surface, static_cast<unsigned int>(model->indices.size()), 0 };
		for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
		{
			const aiFace& face = mesh->mFaces[f];
			if (face.mNumIndices != 3)
			{
				continue; //points and lines
			}
			for (unsigned int i = 0; i < 3; ++i)
			{
				model->indices.push_back(firstVertex + face.mIndices[i]);
			}
		}
		part.indexCount = static_cast<unsigned int>(model->indices.size()) - part.firstIndex;
		model->parts.push_back(part);
	}
	if (model->positions.empty())
	{
		error = "the model has no meshes";
		return nullptr;
	}
	return model;
}

void SkinnedModel::skin(const std::vector<glm::mat4>& skinMatrices, std::vector<glm::vec3>& skinnedPositions, std::vector<glm::vec3>& skinnedNormals,
	const std::vector<glm::vec3>* directions, std::vector<glm::vec3>* skinnedDirections) const
{
	const std::size_t count = positions.size();
	skinnedPositions.resize(count);
	skinnedNormals.resize(count);
	bool withDirections = directions && skinnedDirections && directions->size() == count;
	if (withDirections)
	{
		skinnedDirections->resize(count);
	}
	for (std::size_t i = 0; i < count; ++i)
	{
		const glm::ivec4& j = joints[i];
		const glm::vec4& w = weights[i];
		glm::mat4 blended = skinMatrices[j.x] * w.x + skinMatrices[j.y] * w.y + skinMatrices[j.z] * w.z + skinMatrices[j.w] * w.w;
		skinnedPositions[i] = glm::vec3(blended * glm::vec4(positions[i], 1.0f));
		glm::mat3 rotation(blended);
		skinnedNormals[i] = glm::normalize(rotation * normals[i]);
		if (withDirections)
		{
			glm::vec3 direction = rotation * (*directions)[i];
			float length = glm::length(direction);
			(*skinnedDirections)[i] = length > 1e-6f ? direction / length : skinnedNormals[i];
		}
	}
}

std::vector<unsigned int> SkinnedModel::triangles(Surface surface) const
{
	std::vector<unsigned int> result;
	for (const Part& part : parts)
	{
		if (part.surface == surface)
		{
			result.insert(result.end(), indices.begin() + part.firstIndex, indices.begin() + part.firstIndex + part.indexCount);
		}
	}
	return result;
}
