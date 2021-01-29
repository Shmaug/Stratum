#include "RenderGraph.hpp"

using namespace stm;

void RenderGraph::Validate(CommandBuffer& commandBuffer) {
	if (mValid) return;

	// Create new renderpasses/framebuffers
	vector<RenderPass::SubpassDescription> subpasses(mSubpasses.size());
	ranges::copy(ranges::values(mSubpasses), subpasses.begin());

	vector<shared_ptr<Texture>> attachments();

	mRenderPass = make_shared<RenderPass>("", commandBuffer.mDevice, subpasses);
	mFramebuffer = make_shared<Framebuffer>("", commandBuffer.mDevice, attachments);
	
	mValid = true;
}

shared_ptr<Framebuffer> RenderGraph::Render(CommandBuffer& commandBuffer) {  
	ProfilerRegion ps("RenderGraph::Render", commandBuffer);
  Validate(commandBuffer);

	commandBuffer.BeginRenderPass(mRenderPass, mFramebuffer);
	
	for (const auto& subpass : mRenderPass->SubpassDescriptions()) {
		ProfilerRegion ps(subpass.mName, commandBuffer);
		for (auto camera : mNode.mScene.get_components<Camera>())
			if (camera->RendersToSubpass(subpass))
				mNode.mScene.Render(commandBuffer, *camera);
		
		if (mRenderPass->SubpassDescriptions().size() > 1)
			commandBuffer.NextSubpass();
	}
	
	commandBuffer.EndRenderPass();
	return mFramebuffer;
}