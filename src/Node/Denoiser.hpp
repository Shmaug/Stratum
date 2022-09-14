#pragma once

#include <Core/PipelineState.hpp>
#include "Scene.hpp"

#include <Shaders/denoiser.h>

namespace stm {

class Denoiser {
public:
	STRATUM_API Denoiser(Node& node);

	inline Node& node() const { return mNode; }

	STRATUM_API void create_pipelines();

	STRATUM_API void on_inspector_gui();

	STRATUM_API Image::View denoise(CommandBuffer& commandBuffer, const Image::View& radiance, const Image::View& albedo, const Buffer::View<ViewData>& views, const Buffer::View<VisibilityInfo>& visibility, const Image::View& prev_uvs);

	inline void reset_accumulation() { mResetAccumulation = true; mAccumulatedFrames = 0; }
	inline bool reprojection() const { return mTemporalAccumulationPipeline->specialization_constant<uint32_t>("gReprojection"); }
	inline bool demodulate_albedo() const { return mTemporalAccumulationPipeline->specialization_constant<uint32_t>("gDemodulateAlbedo"); }

private:
	Node& mNode;

	shared_ptr<ComputePipelineState> mTemporalAccumulationPipeline;
	shared_ptr<ComputePipelineState> mEstimateVariancePipeline;
	shared_ptr<ComputePipelineState> mAtrousPipeline;
	shared_ptr<ComputePipelineState> mCopyRGBPipeline;

	unordered_map<string, uint32_t> mDescriptorMap;
	shared_ptr<DescriptorSetLayout> mDescriptorSetLayout;

	struct FrameResources {
		shared_ptr<Fence> mFence;
		Buffer::View<ViewData> mViews;
		Image::View mRadiance;
		Image::View mAlbedo;
		Buffer::View<VisibilityInfo> mVisibility;
		Image::View mAccumColor;
		Image::View mAccumMoments;
		Image::View mDebugImage;
		array<Image::View, 2> mTemp;
		shared_ptr<DescriptorSet> mDescriptorSet;
	};

	vector<shared_ptr<FrameResources>> mFrameResources;
	shared_ptr<FrameResources> mPrevFrame;
	shared_ptr<FrameResources> mCurFrame;

	uint32_t mAccumulatedFrames = 0;
	uint32_t mAtrousIterations = 0;
	uint32_t mHistoryTap = 0;
	DenoiserDebugMode mDebugMode = DenoiserDebugMode::eNone;
	bool mResetAccumulation = false;
};

}