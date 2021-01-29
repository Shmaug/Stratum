#pragma once

#include "../Buffer.hpp"
#include "../CommandBuffer.hpp"
#include "../Pipeline.hpp"

namespace stm {


template<vk::PrimitiveTopology _Topology = vk::PrimitiveTopology::eTriangleList>
class Mesh {
public:
	struct VertexAttribute : public Buffer::ArrayView<> { vk::VertexInputRate mInputRate; };
	struct Submesh {
		uint32_t mPrimitiveCount;
		uint32_t mFirstVertex;
		uint32_t mFirstIndex;
	};
	typedef KdBVH<float, 3, pair<Submesh&, void*>> bvh_t;
	
	inline static const vk::PrimitiveTopology Topology = _Topology;
	inline static constexpr int PrimitiveDegree() {
		switch (_Topology) {
			default:
			case vk::PrimitiveTopology::ePointList:
				return 1;
			case vk::PrimitiveTopology::eLineList:
			case vk::PrimitiveTopology::eLineListWithAdjacency:
			case vk::PrimitiveTopology::eLineStrip:
			case vk::PrimitiveTopology::eLineStripWithAdjacency:
				return 2;
			case vk::PrimitiveTopology::eTriangleList:
			case vk::PrimitiveTopology::eTriangleListWithAdjacency:
			case vk::PrimitiveTopology::eTriangleStrip:
			case vk::PrimitiveTopology::eTriangleStripWithAdjacency:
				return 3;
		}
	} 


	inline Mesh(const string& name) : mName(name) {}
	inline const vector<Submesh>& Submeshes() const { return mSubmeshes; }
	inline const optional<bvh_t>& BVH() const { return mBVH; }

	inline VertexAttribute& operator[](const VertexAttributeId& id) { return mAttributeBuffers[id]; }
	struct VertexAttributeArrayView {
		Mesh& mMesh;
		VertexAttributeType mAttributeType;
		inline VertexAttribute& operator[](const VertexAttributeId& id) { return mMesh.mAttributeBuffers[id]; }
		inline Mesh::VertexAttribute& operator[](uint32_t idx) { return mMesh.mAttributeBuffers[VertexAttributeId(mAttributeType,idx)]; }
	};
	inline VertexAttributeArrayView operator[](const VertexAttributeType& type) { return VertexAttributeArrayView(*this, type); }
	inline VertexAttributeArrayView at(const VertexAttributeType& type, uint32_t idx = 0) { return mAttributeBuffers.at({type, idx}); }

	inline uint32_t Index(uint32_t i, uint32_t baseIndex = 0, uint32_t baseVertex = 0) const {
		if (mIndexBuffer)
			switch (mIndexBuffer.stride()) {
				case sizeof(uint16_t): return baseVertex + mIndexBuffer.at<uint16_t>(baseIndex + i);
				default:
				case sizeof(uint32_t): return baseVertex + mIndexBuffer.at<uint32_t>(baseIndex + i);
				case sizeof(uint8_t):  return baseVertex + mIndexBuffer.at<uint8_t>(baseIndex + i);
			}
		else
			return baseVertex + i;
	}

	inline void AddSubmesh(uint32_t vertexCount, uint32_t baseIndex=0, uint32_t baseVertex=0) {
		mSubmeshes.emplace_back(*this, vertexCount, baseIndex, baseVertex);
		vector<bvh_t::Object> prims;
		ranges::for_each(mSubmeshes, [&](Submesh& s) {
			for (uint32_t i = 0; i < s.mPrimitiveCount; i++)
				prims.push_back(make_pair(s, i));
		});
		mBVH = bvh_t(prims);
	}

	inline pair<vector<vk::VertexInputAttributeDescription>, vector<VertexAttribute>> CreateInput(const SpirvModule& vertexStage) {
		pair<vector<vk::VertexInputAttributeDescription>, vector<VertexAttribute>> result;
		auto&[attributes, bindings] = result;
		for (const auto&[varName, shaderInput] : vertexStage.mStageInputs)
			for (const auto&[attribId, attrib] : mAttributeBuffers)
				if (attribId.mType == shaderInput.mType && attribId.mTypeIndex == shaderInput.mTypeIndex) {
					// vertex stage takes attrib, create an attribute description
					uint32_t bufferBinding = (uint32_t)bindings.size();
					auto it = ranges::find(bindings, attrib);
					if (it == bindings.end())
						bindings.push_back(attrib);
					else
						bufferBinding = it - bindings.begin();
					attributes.emplace_back(shaderInput.mLocation, bufferBinding, shaderInput.mFormat, (uint32_t)attrib.mElementOffset);
				}
		return result;
	}

	inline void Draw(CommandBuffer& commandBuffer, uint32_t instanceCount, uint32_t firstInstance) {
		auto pipeline = dynamic_pointer_cast<GraphicsPipeline>(commandBuffer.BoundPipeline());
		if (!pipeline) throw logic_error("cannot draw a mesh without a bound graphics pipeline\n");

		// TODO: this generates an pair of vertex attributes and bindings each time it's called...
		auto[attributes,bindings] = CreateInput(pipeline->SpirvModules().front());

		for (uint32_t i = 0; i < bindings.size(); i++)
			commandBuffer.BindVertexBuffer(bindings[i].first, i);
		if (mIndexBuffer) commandBuffer.BindIndexBuffer(mIndexBuffer);

		uint32_t primCount = 0;
		for (const Submesh& s : mSubmeshes) {
			if (mIndexBuffer)
				commandBuffer->drawIndexed(s.mPrimitiveCount*PrimitiveDegree(), instanceCount, s.mFirstIndex, s.mFirstVertex, firstInstance);
			 else
				commandBuffer->draw(s.mPrimitiveCount*PrimitiveDegree(), instanceCount, s.mFirstVertex, firstInstance);
			primCount += s.mPrimitiveCount;
		}
		commandBuffer.mPrimitiveCount += instanceCount * primCount;
	}

private:
	unordered_map<VertexAttributeId, VertexAttribute> mAttributeBuffers;
	Buffer::ArrayView<> mIndexBuffer;

	string mName;
	vector<Submesh> mSubmeshes;
	optional<bvh_t> mBVH;
};

template<vk::PrimitiveTopology _Topology = vk::PrimitiveTopology::eTriangleList>
inline Mesh<_Topology>::bvh_t::Volume bounding_box(Mesh<_Topology>::bvh_t::Object ref) {
	auto&[mesh,] = ref;

	Map<Matrix<Scalar, Rows, Cols>> verts(ref.first.mMesh[VertexAttributeType::ePosition].data());
	ref.first.mMesh.Index(idx, s);
	Mesh<_Topology>::bvh_t::Volume v = verts[idx];
	v.expand();
	return (verts[idx], );
}

}