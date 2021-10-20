#pragma once

#include "Scene.hpp"

namespace stm {

namespace hlsl {
#pragma pack(push)
#pragma pack(1)
#include <HLSL/rt/rtscene.hlsli>
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
	struct RTData {
		hlsl::TransformData mCameraToWorld;
		hlsl::ProjectionData mProjection;
		Image::View mSamples;
		Image::View mNormalId;
		Image::View mZ;
		Image::View mPrevUV;
		Image::View mAlbedo;

		Image::View mAccumColor;
		Image::View mAccumMoments;
		Image::View mAccumLength;
	};

	STRATUM_API RayTraceScene(Node& node);

	inline Node& node() const { return mNode; }
		
	STRATUM_API void create_pipelines();
	
	STRATUM_API void on_inspector_gui();
	STRATUM_API void update(CommandBuffer& commandBuffer);
	STRATUM_API void draw(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, const Image::View& colorBuffer) const;
	STRATUM_API void denoise(CommandBuffer& commandBuffer, const RTData& cur, const RTData& prev, const Image::View& prevUV, const Image::View& colorBuffer) const;

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
	shared_ptr<AccelerationStructure> mTopLevel;
	unordered_map<size_t/* hash_args(Mesh*, firstIndex, indexCount) */, BLAS> mAccelerationStructures;
	unordered_map<MeshPrimitive*, hlsl::TransformData> mTransformHistory;

	component_ptr<ComputePipelineState> mCopyVerticesPipeline;
	component_ptr<ComputePipelineState> mTracePipeline;

	component_ptr<ComputePipelineState> mReprojectPipeline;
	
	component_ptr<ComputePipelineState> mTemporalAccumulationPipeline;
	component_ptr<ComputePipelineState> mEstimateVariancePipeline;
	component_ptr<ComputePipelineState> mAtrousPipeline;
	component_ptr<ComputePipelineState> mCreateGradientSamplesPipeline;
	component_ptr<ComputePipelineState> mAtrousGradientPipeline;

	bool     mNaiveAccumulation = true;
	bool     mModulateAlbedo = true;
	uint32_t mNumIterations  = 5;
};

}