#pragma once

#include "../Core/Buffer.hpp"
#include "../Scene/TriangleBvh2.hpp"

namespace stm {
	
class Mesh {
public:
	struct VertexAttribute {
		ArrayBufferView mBufferView;
		uint32_t mElementOffset;
		VertexAttributeType mType;
		uint32_t mTypeIndex;
		vk::VertexInputRate mInputRate;
		inline VertexAttribute(ArrayBufferView bufferView, uint32_t elementOffset, VertexAttributeType type, uint32_t typeIndex, vk::VertexInputRate inputRate)
			: mBufferView(bufferView), mElementOffset(elementOffset), mType(type), mTypeIndex(typeIndex), mInputRate(inputRate) {}
	};

	class Submesh {
	public:
		uint32_t mVertexCount = 0;
		uint32_t mBaseVertex = 0;
		uint32_t mIndexCount = 0;
		uint32_t mBaseIndex = 0;
		TriangleBvh2* mBvh = nullptr;

		Submesh() = default;
		inline Submesh(uint32_t vertexCount, uint32_t baseVertex, uint32_t indexCount, uint32_t baseIndex, TriangleBvh2* bvh = nullptr)
			: mVertexCount(vertexCount), mBaseVertex(baseVertex), mIndexCount(indexCount), mBaseIndex(baseIndex), mBvh(bvh) {}
		inline ~Submesh() { safe_delete(mBvh); };

		STRATUM_API void Draw(CommandBuffer& commandBuffer, uint32_t instanceCount = 1, uint32_t firstInstance = 0);
	};

	const std::string mName;

	inline Mesh(const std::string& name, vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList) : mName(name), mTopology(topology) {}
	inline Mesh(const std::string& name, const std::unordered_map<uint64_t, VertexAttribute>& vertexAttributes, vk::PrimitiveTopology topology)
		: mName(name), mVertexAttributes(vertexAttributes), mTopology(topology) {}
	inline Mesh(const std::string& name, const std::unordered_map<uint64_t, VertexAttribute>& vertexAttributes, const ArrayBufferView& indexBuffer, vk::PrimitiveTopology topology)
		: mName(name), mVertexAttributes(vertexAttributes), mIndexBuffer(indexBuffer), mTopology(topology) {}
	inline ~Mesh() {}

	inline uint32_t SubmeshCount() const { return (uint32_t)mSubmeshes.size(); }
	inline const Submesh& GetSubmesh(uint32_t index) const { return mSubmeshes[index]; }
	inline void AddSubmesh(const Submesh& submesh) { mSubmeshes.push_back(submesh); }
	
	inline VertexAttribute* GetAttribute(VertexAttributeType type, uint32_t typeIndex = 0) {
		uint64_t idx = ((uint64_t)typeIndex << 32) & (uint64_t)type;
		if (mVertexAttributes.count(idx))
			return &mVertexAttributes.at(idx);
		return nullptr;
	}
	inline void SetVertexAttribute(const VertexAttribute& attribute) {
		uint64_t idx = ((uint64_t)attribute.mTypeIndex << 32) & (uint64_t)attribute.mType;
		mPipelineInputs.clear();
		mVertexAttributes.emplace(idx, attribute);
	}
	inline void SetVertexAttribute(VertexAttributeType type, uint32_t typeIndex, const ArrayBufferView& bufferView, uint32_t elementOffset, vk::VertexInputRate inputRate = vk::VertexInputRate::eVertex) {
		SetVertexAttribute(VertexAttribute(bufferView, elementOffset, type, typeIndex, inputRate));
	}
	inline void SetIndexBuffer(const ArrayBufferView& bufferView) {
		mIndexBuffer = bufferView;
	}

	inline vk::PrimitiveTopology Topology() const { return mTopology; }
	STRATUM_API vk::PipelineVertexInputStateCreateInfo PipelineInput(GraphicsPipeline* pipeline);

	STRATUM_API void Draw(CommandBuffer& commandBuffer, Camera* camera, uint32_t instanceCount = 1, uint32_t firstInstance = 0);
	STRATUM_API void Draw(CommandBuffer& commandBuffer, uint32_t submesh, uint32_t instanceCount = 1, uint32_t firstInstance = 0);

private:
	struct PipelineInputInfo {
		std::vector<vk::VertexInputBindingDescription> mBindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> mAttributeDescriptions;
		vk::PipelineVertexInputStateCreateInfo mCreateInfo;
	};

	ArrayBufferView mIndexBuffer;
	std::unordered_map<uint64_t, VertexAttribute> mVertexAttributes;
	std::unordered_map<GraphicsPipeline*, PipelineInputInfo> mPipelineInputs;
	std::vector<Submesh> mSubmeshes;

	vk::PrimitiveTopology mTopology = vk::PrimitiveTopology::eTriangleList;
};

}

namespace std {
template<>
struct hash<stm::Mesh::VertexAttribute> {
	inline size_t operator()(const stm::Mesh::VertexAttribute& v) const {
		return stm::hash_combine(v.mBufferView, v.mElementOffset, v.mType, v.mTypeIndex, v.mInputRate);
	}
};
}