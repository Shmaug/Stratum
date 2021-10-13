#pragma once

#include "Scene.hpp"

namespace stm {

namespace hlsl {
#pragma pack(push)
#pragma pack(1)
#include <HLSL/pbr_rt.hlsli>
#pragma pack(pop)
}

class AccelerationStructure : public DeviceResource {
public:
	AccelerationStructure() = delete;
	AccelerationStructure(const AccelerationStructure&) = delete;
	AccelerationStructure(AccelerationStructure&&) = delete;
	STRATUM_API AccelerationStructure(CommandBuffer& commandBuffer, const string& name, vk::AccelerationStructureTypeKHR type, const vk::AccelerationStructureGeometryKHR& geometry,  const vk::AccelerationStructureBuildRangeInfoKHR& buildRange);
	STRATUM_API ~AccelerationStructure();
	inline const Buffer::View<byte>& buffer() const { return mBuffer; }
	inline const vk::AccelerationStructureKHR* operator->() const { return &mAccelerationStructure; }
	inline const vk::AccelerationStructureKHR& operator*() const { return mAccelerationStructure; }
private:
	vk::AccelerationStructureKHR mAccelerationStructure;
	Buffer::View<byte> mBuffer;
};

class RayTraceScene {
public:
	STRATUM_API RayTraceScene(Node& node);

	inline Node& node() const { return mNode; }
		
	STRATUM_API void create_pipelines();
	
	STRATUM_API void on_inspector_gui();
	STRATUM_API void update(CommandBuffer& commandBuffer);
	STRATUM_API void draw(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, const Image::View& colorBuffer, const Image::View& depthBuffer) const;

private:
	struct BLAS {
		shared_ptr<AccelerationStructure> mAccelerationStructure;
		uint32_t mVertexCount;
		VertexArrayObject::Attribute mPositions;
		VertexArrayObject::Attribute mNormals;
		VertexArrayObject::Attribute mTangents;
		VertexArrayObject::Attribute mTexcoords;
		Buffer::StrideView mIndices;
	};


	Node& mNode;
	component_ptr<ComputePipelineState> mCopyVerticesPipeline;
	component_ptr<ComputePipelineState> mTracePipeline;

	shared_ptr<AccelerationStructure> mTopLevel;
	unordered_map<size_t/* hash_args(Mesh*, firstIndex, indexCount) */, BLAS> mAccelerationStructures;
};

}