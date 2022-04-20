#pragma once

#include <Core/PipelineState.hpp>
#include "AccelerationStructure.hpp"
#include "Scene.hpp"

namespace stm {

#pragma pack(push)
#pragma pack(1)
namespace hlsl {
#include <HLSL/path_vertex.hlsli>
#include <HLSL/reservoir.hlsli>
}
#pragma pack(pop)

class RayTraceScene {
public:
	STRATUM_API RayTraceScene(Node& node);

	inline Node& node() const { return mNode; }
		
	STRATUM_API void create_pipelines();
	
	STRATUM_API void on_inspector_gui();
	STRATUM_API void update(CommandBuffer& commandBuffer, const float deltaTime);
	STRATUM_API void render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<hlsl::ViewData>& views);

private:
	struct MeshAS {
		shared_ptr<AccelerationStructure> mAccelerationStructure;
		Buffer::StrideView mIndices;
	};

	Node& mNode;
	
	unordered_map<size_t, shared_ptr<AccelerationStructure>> mAABBs;

	unordered_map<Mesh*, Buffer::View<hlsl::PackedVertexData>> mMeshVertices;
	unordered_map<Mesh*, MeshAS> mMeshAccelerationStructures;
	
	component_ptr<ComputePipelineState> mCopyVerticesPipeline;
	
	component_ptr<ComputePipelineState> mSamplePhotonsPipeline;
	component_ptr<ComputePipelineState> mSampleVisibilityPipeline;
	component_ptr<ComputePipelineState> mRandomWalkPipeline;
	component_ptr<ComputePipelineState> mResolvePipeline;
	component_ptr<ComputePipelineState> mTonemapPipeline;
	
	component_ptr<ComputePipelineState> mSpatialReusePipeline;
	
	// A-SVGF pipelines
	component_ptr<ComputePipelineState> mGradientForwardProjectPipeline;
	component_ptr<ComputePipelineState> mTemporalAccumulationPipeline;
	component_ptr<ComputePipelineState> mEstimateVariancePipeline;
	component_ptr<ComputePipelineState> mAtrousPipeline;
	component_ptr<ComputePipelineState> mCreateGradientSamplesPipeline;
	component_ptr<ComputePipelineState> mAtrousGradientPipeline;
	component_ptr<ComputePipelineState> mCopyRGBPipeline;

	array<unordered_map<string, uint32_t>, 2> mPathTraceDescriptorMap;
	array<shared_ptr<DescriptorSetLayout>, 2> mPathTraceDescriptorSetLayouts;
	hlsl::PathTracePushConstants mPathTracePushConstants;

	bool mRandomPerFrame = true;
	bool mReprojection = true;
	bool mUpdateSceneEachFrame = true;
	uint32_t mDiffAtrousIterations = 0;
	uint32_t mAtrousIterations = 0;
	uint32_t mSpatialReservoirIterations = 0;
	uint32_t mHistoryTap = 0;

	struct FrameResources {
		shared_ptr<Fence> mFence;

		hlsl::ResourcePool mResources;

		shared_ptr<AccelerationStructure> mScene;
		unordered_map<void* /* geometry component */, pair<hlsl::TransformData, uint32_t /* instance index */ >> mInstanceTransformMap;

		Buffer::View<hlsl::PackedVertexData> mVertices;
		Buffer::View<byte> mIndices;
		Buffer::View<byte> mMaterialData;
		Buffer::View<hlsl::InstanceData> mInstances;
		Buffer::View<uint32_t> mLightInstances;
		Buffer::View<float> mDistributionData;
		Buffer::View<uint32_t> mInstanceIndexMap;
		shared_ptr<DescriptorSet> mPathTraceDescriptorSet;
		
		Buffer::View<hlsl::ViewData> mViews;
		Buffer::View<uint32_t> mViewVolumeIndices;
		Buffer::View<hlsl::Reservoir> mReservoirs;
		Buffer::View<hlsl::PathState> mPathStates;
		Buffer::View<hlsl::PathVertex> mPathStateVertices;
		Buffer::View<hlsl::ShadingData> mPathStateShadingData;
		Buffer::View<hlsl::PathVertex> mLightPathVertices;
		Buffer::View<hlsl::ShadingData> mLightPathShadingData;
		Buffer::View<hlsl::VisibilityInfo> mVisibility;

		Image::View mRadiance;
		Image::View mAlbedo;
		Image::View mAccumColor;
		Image::View mAccumMoments;
		Image::View mGradientSamples;
		array<Image::View, 2> mTemp;
		array<array<Image::View, 2>, 2> mDiffTemp;

		uint32_t mFrameNumber;
		uint32_t mMaterialCount;
	};

	Buffer::View<uint32_t> mCounterValues;
	uint32_t mPrevCounterValue;
	float mRaysPerSecond;
	float mRaysPerSecondTimer;

	vector<shared_ptr<FrameResources>> mFrameResources;
	shared_ptr<FrameResources> mPrevFrame;
	shared_ptr<FrameResources> mCurFrame;
	
	STRATUM_API void update_scene(CommandBuffer& commandBuffer, const float deltaTime);
};

}