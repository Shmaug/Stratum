#pragma once

#include "GeometryData.hpp"
#include "CommandBuffer.hpp"
#include "Pipeline.hpp"

namespace stm {

class Mesh {
public:
	struct Submesh {
		uint32_t mPrimitiveCount;
		uint32_t mFirstVertex;
		uint32_t mFirstIndex;
	};
	
	inline Mesh(const string& name, const GeometryData& geometry = { vk::PrimitiveTopology::eTriangleList }) : mName(name), mGeometry(geometry) {}
	STRATUM_API Mesh(CommandBuffer& commandBuffer, const fs::path& filename);
	
	inline Buffer::StrideView& indices() { return mIndices; }
	inline GeometryData& geometry() { return mGeometry; }
	inline vector<Submesh>& submeshes() { return mSubmeshes; }

	inline uint32_t Index(uint32_t i, uint32_t baseIndex = 0, uint32_t baseVertex = 0) const {
		if (mIndices) {
			byte* addr = mIndices.data() + mIndices.stride()*(baseIndex + i);
			switch (mIndices.stride()) {
				default:
				case sizeof(uint32_t): return baseVertex + *reinterpret_cast<uint32_t*>(addr);
				case sizeof(uint16_t): return baseVertex + *reinterpret_cast<uint16_t*>(addr);
				case sizeof(uint8_t):  return baseVertex + *reinterpret_cast<uint8_t*>(addr);
			}
		}
		else
			return baseVertex + i;
	}
	
	inline void draw(CommandBuffer& commandBuffer, uint32_t instanceCount = 1, uint32_t firstInstance = 0) {
		auto pipeline = dynamic_pointer_cast<GraphicsPipeline>(commandBuffer.bound_pipeline());
		if (!pipeline) throw logic_error("cannot draw a mesh without a bound graphics pipeline\n");

		for (uint32_t i = 0; i < mGeometry.mBindings.size(); i++)
			commandBuffer.bind_vertex_buffer(i, get<0>(mGeometry.mBindings[i]));
		if (mIndices) commandBuffer.bind_index_buffer(mIndices);

		uint32_t primCount = 0;
		for (const Submesh& s : mSubmeshes) {
			if (mIndices)
				commandBuffer->drawIndexed(s.mPrimitiveCount*verts_per_prim(mGeometry.mPrimitiveTopology), instanceCount, s.mFirstIndex, s.mFirstVertex, firstInstance);
			 else
				commandBuffer->draw(s.mPrimitiveCount*verts_per_prim(mGeometry.mPrimitiveTopology), instanceCount, s.mFirstVertex, firstInstance);
			primCount += s.mPrimitiveCount;
		}
		commandBuffer.mPrimitiveCount += instanceCount * primCount;
	}

private:
	GeometryData mGeometry;
	Buffer::StrideView mIndices;
	vector<Submesh> mSubmeshes;
	string mName;
};

}