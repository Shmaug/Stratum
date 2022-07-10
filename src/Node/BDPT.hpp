#pragma once

#include "Scene.hpp"
#include "Denoiser.hpp"

#include <Shaders/bdpt.h>

namespace stm {

class BDPT {
public:
	STRATUM_API BDPT(Node& node);

	inline Node& node() const { return mNode; }

	STRATUM_API void create_pipelines();

	STRATUM_API void on_inspector_gui();
	STRATUM_API void update(CommandBuffer& commandBuffer, const float deltaTime);
	STRATUM_API void render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<pair<ViewData,TransformData>>& views);

private:
	Node& mNode;

	shared_ptr<ComputePipelineState> mSamplePhotonsPipeline;
	shared_ptr<ComputePipelineState> mSampleVisibilityPipeline;
	shared_ptr<ComputePipelineState> mPresampleLightPipeline;
	unordered_map<IntegratorType, shared_ptr<ComputePipelineState>> mIntegratorPipelines;
	IntegratorType mIntegratorType = IntegratorType::eNaiveSingleBounce;
	shared_ptr<ComputePipelineState> mTonemapPipeline;
	shared_ptr<ComputePipelineState> mTonemapReducePipeline;

	array<unordered_map<string, uint32_t>, 2> mDescriptorMap;
	array<shared_ptr<DescriptorSetLayout>, 2> mDescriptorSetLayouts;
	BDPTPushConstants mPushConstants;

	bool mRandomPerFrame = true;
	bool mDenoise = true;
	uint32_t mSamplingFlags = BDPT_FLAG_REMAP_THREADS | BDPT_FLAG_RAY_CONES | BDPT_FLAG_SAMPLE_BSDFS;
	DebugMode mDebugMode = DebugMode::eNone;

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
		Image::View mDebugImage;

		Buffer::View<PresampledLightPoint> mPresampledLights;
		unordered_map<string, Buffer::View<byte>> mPathData;

		Image::View mDenoiseResult;
		Buffer::View<uint4> mTonemapMax;
		Image::View mTonemapResult;
		uint32_t mFrameNumber;
	};

	Buffer::View<uint32_t> mRayCount;
	uint32_t mPrevCounterValue;
	float mRaysPerSecond;
	float mRaysPerSecondTimer;

	vector<shared_ptr<FrameResources>> mFrameResourcePool;
	shared_ptr<FrameResources> mPrevFrame, mCurFrame;
};

}