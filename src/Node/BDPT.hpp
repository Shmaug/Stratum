#pragma once

#include "Scene.hpp"
#include "Denoiser.hpp"

#include <Shaders/bdpt.h>

namespace stm {

struct HashGridData;

class BDPT {
public:
	STRATUM_API BDPT(Node& node);

	inline Node& node() const { return mNode; }

	inline Image::View prev_result() { return mPrevFrame ? mPrevFrame->mTonemapResult : Image::View(); }

	STRATUM_API void create_pipelines();

	STRATUM_API void on_inspector_gui();
	STRATUM_API void update(CommandBuffer& commandBuffer, const float deltaTime);
	STRATUM_API void render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<pair<ViewData,TransformData>>& views);

private:
	Node& mNode;

	enum RenderPipelineIndex {
		eSamplePhotons,
		eSampleVisibility,
		ePresampleLights,
		eTraceShadows,
		eAddLightTrace,
		eHashGridComputeIndices,
		eHashGridSwizzle,
		ePipelineCount
	};
	array<shared_ptr<ComputePipelineState>, RenderPipelineIndex::ePipelineCount> mRenderPipelines;

	shared_ptr<ComputePipelineState> mTonemapPipeline;
	shared_ptr<ComputePipelineState> mTonemapMaxReducePipeline;

	array<unordered_map<string, uint32_t>, 2> mDescriptorMap;
	array<shared_ptr<DescriptorSetLayout>, 2> mDescriptorSetLayouts;
	BDPTPushConstants mPushConstants;

	bool mHalfColorPrecision = false;
	bool mPauseRendering = false;
	bool mRandomPerFrame = true;
	bool mForceLambertian = false;
	bool mDenoise = true;
	uint32_t mSamplingFlags = 0;
	BDPTDebugMode mDebugMode = BDPTDebugMode::eNone;
	uint32_t mLightTraceQuantization = 65536;


	struct FrameResources {
		shared_ptr<Fence> mFence;

		shared_ptr<DescriptorSet> mSceneDescriptors;
		shared_ptr<DescriptorSet> mViewDescriptors;

		shared_ptr<Scene::SceneData> mSceneData;

		Buffer::View<ViewData> mViews;
		Buffer::View<TransformData> mViewTransforms;
		Buffer::View<TransformData> mViewInverseTransforms;
		Buffer::View<uint32_t> mViewMediumIndices;
		Image::View mRadiance;
		Image::View mAlbedo;
		Image::View mPrevUVs;
		Image::View mDebugImage;

		unordered_map<string, Buffer::View<byte>> mPathData;
		vector<shared_ptr<HashGridData>> mHashGrids;
		Buffer::View<VisibilityInfo> mSelectionData;
		bool mSelectionDataValid;

		Image::View mDenoiseResult;
		Buffer::View<uint4> mTonemapMax;
		Image::View mTonemapResult;
		uint32_t mFrameNumber;
	};

	Buffer::View<uint32_t> mRayCount;
	vector<uint32_t> mPrevRayCount;
	vector<float> mRaysPerSecond;
	float mRayCountTimer;

	list<shared_ptr<FrameResources>> mFrameResourcePool;
	shared_ptr<FrameResources> mPrevFrame, mCurFrame;
};

}