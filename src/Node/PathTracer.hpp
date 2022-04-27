#pragma once

#include <Core/PipelineState.hpp>

#include "Scene.hpp"
#include "Denoiser.hpp"

namespace stm {

#pragma pack(push)
#pragma pack(1)
#include <HLSL/path_vertex.hlsli>
#include <HLSL/reservoir.hlsli>
#pragma pack(pop)

class PathTracer {
public:
	STRATUM_API PathTracer(Node& node);

	inline Node& node() const { return mNode; }

	STRATUM_API void create_pipelines();

	STRATUM_API void on_inspector_gui();
	STRATUM_API void update(CommandBuffer& commandBuffer, const float deltaTime);
	STRATUM_API void render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<ViewData>& views);

private:
	Node& mNode;

	shared_ptr<ComputePipelineState> mSamplePhotonsPipeline;
	shared_ptr<ComputePipelineState> mSampleVisibilityPipeline;
	shared_ptr<ComputePipelineState> mRandomWalkPipeline;
	shared_ptr<ComputePipelineState> mResolvePipeline;
	shared_ptr<ComputePipelineState> mTonemapPipeline;

	array<unordered_map<string, uint32_t>, 2> mDescriptorMap;
	array<shared_ptr<DescriptorSetLayout>, 2> mDescriptorSetLayouts;
	PathTracePushConstants mPushConstants;

	bool mRandomPerFrame = true;
	bool mDenoise = true;
	uint32_t mSpatialReservoirIterations = 0;

	struct FrameResources {
		shared_ptr<Fence> mFence;

		shared_ptr<DescriptorSet> mSceneDescriptors;
		shared_ptr<DescriptorSet> mViewDescriptors;

		shared_ptr<Scene::SceneData> mSceneData;

		Image::View mRadiance;
		Image::View mAlbedo;
		Buffer::View<VisibilityInfo> mVisibility;
		Buffer::View<uint32_t> mRadianceMutex;
		Buffer::View<ViewData> mViews;
		Buffer::View<uint32_t> mViewVolumeIndices;
		Buffer::View<Reservoir> mReservoirs;
		Buffer::View<PathState> mPathStates;
		Buffer::View<PathVertex> mPathStateVertices;
		Buffer::View<ShadingData> mPathStateShadingData;
		Buffer::View<PathVertex> mLightPathVertices;
		Buffer::View<ShadingData> mLightPathShadingData;

		Image::View mDenoiseResult;
		Image::View mTonemapResult;
		uint32_t mFrameNumber;
	};

	Buffer::View<uint32_t> mCounterValues;
	uint32_t mPrevCounterValue;
	float mRaysPerSecond;
	float mRaysPerSecondTimer;

	vector<shared_ptr<FrameResources>> mFrameResources;
	shared_ptr<FrameResources> mPrevFrame;
	shared_ptr<FrameResources> mCurFrame;
};

}