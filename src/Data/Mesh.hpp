#pragma once

#include <Core/Buffer.hpp>
#include <Scene/TriangleBvh2.hpp>

namespace stm {
	
struct VertexAttribute {
	BufferView mBufferView;
	VertexAttributeType mType;
	uint32_t mTypeIndex;
	uint32_t mElementOffset;
	uint32_t mElementStride;
	vk::VertexInputRate mInputRate;
};

class Mesh {
public:
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
	inline Mesh(const std::string& name, const std::vector<VertexAttribute>& vertexAttributes, vk::PrimitiveTopology topology)
		: mName(name), mVertexAttributes(vertexAttributes), mTopology(topology) {}
	inline Mesh(const std::string& name, const std::vector<VertexAttribute>& vertexAttributes, BufferView indexBuffer, vk::IndexType indexType, vk::PrimitiveTopology topology)
		: mName(name), mVertexAttributes(vertexAttributes), mIndexBuffer(indexBuffer), mIndexType(indexType), mTopology(topology) {}
	inline ~Mesh() {}

	inline uint32_t SubmeshCount() const { return (uint32_t)mSubmeshes.size(); }
	inline const Submesh& GetSubmesh(uint32_t index) const { return mSubmeshes[index]; }
	
	inline vk::PrimitiveTopology Topology() const { return mTopology; }

	STRATUM_API vk::PipelineVertexInputStateCreateInfo PipelineInput(GraphicsPipeline* pipeline);

	inline void AddSubmesh(const Submesh& submesh) { mSubmeshes.push_back(submesh); }

	STRATUM_API void SetAttribute(VertexAttributeType type, uint32_t typeIndex, const BufferView& bufferView, uint32_t elementOffset, uint32_t elementStride, vk::VertexInputRate inputRate = vk::VertexInputRate::eVertex);
	inline void SetIndexBuffer(const BufferView& bufferView, vk::IndexType indexType) {
		mIndexBuffer = bufferView;
		mIndexType = indexType;
	}

	STRATUM_API void Draw(CommandBuffer& commandBuffer, Camera* camera, uint32_t instanceCount = 1, uint32_t firstInstance = 0);
	STRATUM_API void Draw(CommandBuffer& commandBuffer, uint32_t submesh, uint32_t instanceCount = 1, uint32_t firstInstance = 0);

private:
	struct PipelineInputInfo {
		std::vector<vk::VertexInputBindingDescription> mBindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> mAttributeDescriptions;
		vk::PipelineVertexInputStateCreateInfo mCreateInfo;
	};

	BufferView mIndexBuffer;
	std::vector<VertexAttribute> mVertexAttributes;
	std::unordered_map<GraphicsPipeline*, PipelineInputInfo> mPipelineInputs;
	std::vector<Submesh> mSubmeshes;

	vk::IndexType mIndexType = vk::IndexType::eUint32;
	vk::PrimitiveTopology mTopology = vk::PrimitiveTopology::eTriangleList;
};

}

namespace std {
template<>
struct hash<stm::VertexAttribute> {
	inline size_t operator()(const stm::VertexAttribute& v) const {
		return stm::hash_combine(v.mBufferView, v.mType, v.mTypeIndex, v.mElementOffset, v.mElementStride);
	}
};
}