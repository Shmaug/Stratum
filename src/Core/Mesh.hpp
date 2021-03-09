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
	
	inline Mesh(const string& name) : mName(name) {}
	inline const GeometryData& Geometry() { return mGeometry; }
	inline const vector<Submesh>& Submeshes() const { return mSubmeshes; }

	inline uint32_t Index(uint32_t i, uint32_t baseIndex = 0, uint32_t baseVertex = 0) const {
		if (mIndices) {
			byte* addr = mIndices.data() + mIndices.stride()*(baseIndex + i);
			switch (mIndices.stride()) {
				default:
				case sizeof(uint32_t): return *reinterpret_cast<uint32_t*>(addr);
				case sizeof(uint16_t): return *reinterpret_cast<uint16_t*>(addr);
				case sizeof(uint8_t):  return *reinterpret_cast<uint8_t*>(addr);
			}
		}
		else
			return baseVertex + i;
	}

	inline void AddSubmesh(uint32_t vertexCount, uint32_t baseIndex = 0, uint32_t baseVertex = 0) {
		mSubmeshes.emplace_back(vertexCount, baseIndex, baseVertex);
	}

	inline void Draw(CommandBuffer& commandBuffer, uint32_t instanceCount = 1, uint32_t firstInstance = 0) {
		auto pipeline = dynamic_pointer_cast<GraphicsPipeline>(commandBuffer.BoundPipeline());
		if (!pipeline) throw logic_error("cannot draw a mesh without a bound graphics pipeline\n");

		for (uint32_t i = 0; i < mGeometry.mBindings.size(); i++)
			commandBuffer.BindVertexBuffer(i, mGeometry.mBindings[i].mBuffer);
		if (mIndices) commandBuffer.BindIndexBuffer(mIndices);

		uint32_t primCount = 0;
		for (const Submesh& s : mSubmeshes) {
			if (mIndices)
				commandBuffer->drawIndexed(s.mPrimitiveCount*PrimitiveDegree(mGeometry.mPrimitiveTopology), instanceCount, s.mFirstIndex, s.mFirstVertex, firstInstance);
			 else
				commandBuffer->draw(s.mPrimitiveCount*PrimitiveDegree(mGeometry.mPrimitiveTopology), instanceCount, s.mFirstVertex, firstInstance);
			primCount += s.mPrimitiveCount;
		}
		commandBuffer.mPrimitiveCount += instanceCount * primCount;
	}

private:
	GeometryData mGeometry;
	Buffer::ArrayView<> mIndices;
	vector<Submesh> mSubmeshes;
	string mName;
};

}