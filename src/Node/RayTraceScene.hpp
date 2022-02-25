#pragma once

#include <Core/PipelineState.hpp>

#include "Scene.hpp"

namespace stm {

#pragma pack(push)
#pragma pack(1)
namespace hlsl {
#include <HLSL/visibility_buffer.hlsli>
#include <HLSL/reservoir.hlsli>
}
#pragma pack(pop)

class AccelerationStructure : public DeviceResource {
public:
	AccelerationStructure() = delete;
	AccelerationStructure(const AccelerationStructure&) = delete;
	AccelerationStructure(AccelerationStructure&&) = delete;
	STRATUM_API AccelerationStructure(CommandBuffer& commandBuffer, const string& name, vk::AccelerationStructureTypeKHR type, const vk::ArrayProxyNoTemporaries<const vk::AccelerationStructureGeometryKHR>& geometries, const vk::ArrayProxyNoTemporaries<const vk::AccelerationStructureBuildRangeInfoKHR>& buildRanges);
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
	STRATUM_API void update(CommandBuffer& commandBuffer, float deltaTime);
	STRATUM_API void render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<hlsl::ViewData>& views);

private:
	struct MeshAS {
		shared_ptr<AccelerationStructure> mAccelerationStructure;
		Buffer::StrideView mIndices;
	};

	Node& mNode;
	shared_ptr<AccelerationStructure> mTopLevel;
	shared_ptr<AccelerationStructure> mUnitCubeAS;
	unordered_map<Mesh*, Buffer::View<hlsl::PackedVertexData>> mMeshVertices;
	unordered_map<Mesh*, MeshAS> mMeshAccelerationStructures;
	unordered_map<void*, pair<hlsl::TransformData, uint32_t>> mTransformHistory;
	
	component_ptr<ComputePipelineState> mCopyVerticesPipeline;
	
	component_ptr<ComputePipelineState> mTraceVisibilityPipeline;
	component_ptr<ComputePipelineState> mTraceBouncePipeline;
	component_ptr<ComputePipelineState> mDemodulateAlbedoPipeline;
	component_ptr<ComputePipelineState> mTonemapPipeline;
	
	component_ptr<ComputePipelineState> mSpatialReusePipeline;
	
	// A-SVGF pipelines
	component_ptr<ComputePipelineState> mGradientForwardProjectPipeline;
	component_ptr<ComputePipelineState> mTemporalAccumulationPipeline;
	component_ptr<ComputePipelineState> mEstimateVariancePipeline;
	component_ptr<ComputePipelineState> mAtrousPipeline;
	component_ptr<ComputePipelineState> mCreateGradientSamplesPipeline;
	component_ptr<ComputePipelineState> mAtrousGradientPipeline;

	bool mRandomPerFrame = true;
	bool mReprojection = true;
	bool mDemodulateAlbedo = true;
	uint32_t mDiffAtrousIterations = 0;
	uint32_t mAtrousIterations = 0;
	uint32_t mSpatialReservoirIterations = 0;
	uint32_t mHistoryTap = 0;
	uint32_t mMinDepth = 2;
	uint32_t mMaxDepth = 5;
	uint32_t mDirectLightDepth = 4;

	struct FrameData {
		Buffer::View<hlsl::PackedVertexData> mVertices;
		Buffer::View<byte> mIndices;
		Buffer::View<byte> mMaterialData;
		Buffer::View<hlsl::InstanceData> mInstances;
		Buffer::View<uint32_t> mLightInstances;
		Buffer::View<float> mDistributionData;
		Buffer::View<hlsl::Reservoir> mReservoirs;
		Buffer::View<hlsl::PathBounceState> mPathBounceData;

		Buffer::View<hlsl::ViewData> mViews;
		
		array<Image::View, VISIBILITY_BUFFER_COUNT> mVisibility;
		Image::View mRadiance;
		Image::View mAlbedo;

		Image::View mAccumColor;
		Image::View mAccumMoments;
		
		Image::View mGradientSamples;
		array<Image::View, 2> mTemp;
		array<array<Image::View, 2>, 2> mDiffTemp;

		uint32_t mFrameId;
	};

	Buffer::View<uint32_t> mCounterValues;
	uint32_t mPrevCounterValue;
	float mRaysPerSecond;
	float mRaysPerSecondTimer;

	unique_ptr<FrameData> mCurFrame, mPrevFrame;
};

}