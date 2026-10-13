#pragma once
#include <memory>
#include <unordered_map>
#include <GL/glew.h>
#include "Chunk.hpp"
#include "WorldStreamer.hpp"
#include "../Render/ShadowCascades.hpp"
#include "../Resources/Shader.hpp"
#include "../Scene/ViewFrustum.hpp"

namespace bibo
{
	namespace world
	{
		/**
		* Draws the meshes of the chunks (in the level of detail the streamer gave them), only those in the view frustum
		*/
		class TerrainRenderer
		{
		public:
			TerrainRenderer();
			~TerrainRenderer();
			TerrainRenderer(const TerrainRenderer&) = delete;
			TerrainRenderer& operator=(const TerrainRenderer&) = delete;

			/**
			* Uploads the new mesh of a chunk (or removes it)
			*/
			void apply(const WorldStreamer::MeshUpdate& update);
			/**
			* @param frustum Chunks outside of it are not drawn (nullptr: all)
			* @return Number of drawn chunks
			*/
			int draw(const scene::ViewFrustum* frustum);
			/**
			* Into a shadow map (depth only)
			*/
			void drawShadow(const render::ShadowCascade& cascade);
			int getChunkCount() const { return static_cast<int>(chunks.size()); }
			/**
			* @return Triangles drawn by the last draw
			*/
			std::size_t getDrawnTriangles() const { return drawnTriangles; }

		private:
			struct GpuChunk
			{
				GLuint vertexArray = 0, vertexBuffer = 0, indexBuffer = 0;
				GLsizei indexCount = 0;
				Box bounds;
				int lod = 0;
			};
			void release(GpuChunk& chunk);

			std::unique_ptr<Shader> shader;
			std::unique_ptr<Shader> depthShader;
			std::unordered_map<ChunkKey, GpuChunk, ChunkKeyHash> chunks;
			std::size_t drawnTriangles = 0;
		};
	}
}
