#include "RenderGraph.hpp"
#include "MeshRenderer.hpp"

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

void RenderGraph::RenderCamera(CommandBuffer& commandBuffer, Camera& camera) {
	vector<Renderer*> renderers;
	{
		ProfilerRegion ps("Culling/Sorting");

		vector<SceneNode*> nodes = BVH()->Intersect(camera.Frustum());
		renderers.reserve(nodes.size());
		for (SceneNode* o : nodes)
			if (Renderer* r = dynamic_cast<Renderer*>(o))
				if (r->Visible(commandBuffer.CurrentShaderPass()))
					renderers.push_back(r);
		
		// add any renderers that don't have bounds (thus are omitted by the BVH)
		for (Renderer* r : mRenderers)
			if (!r->Bounds() && r->Visible(commandBuffer.CurrentShaderPass()) && (camera.DrawSkybox() || r != mSkybox))
				renderers.push_back(r);

		ranges::sort(renderers, [&](Renderer* a, Renderer* b) {
			uint32_t qa = a->RenderQueue(commandBuffer.CurrentShaderPass());
			uint32_t qb = b->RenderQueue(commandBuffer.CurrentShaderPass());
			if (qa == qb) {
				MeshRenderer* ma = dynamic_cast<MeshRenderer*>(a);
				MeshRenderer* mb = dynamic_cast<MeshRenderer*>(b);
				if (ma && mb)
					if (ma->Material() == mb->Material())
						return ma->Mesh() < mb->Mesh();
					else
						return ma->Material() < mb->Material();
			}
			return qa < qb;
		});
	}

	camera.AspectRatio((float)commandBuffer.CurrentFramebuffer()->Extent().width / (float)commandBuffer.CurrentFramebuffer()->Extent().height);

	auto cameraBuffer = commandBuffer.GetBuffer("Camera Buffer", sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	camera.WriteUniformBuffer(cameraBuffer->Mapped());
	camera.SetViewportScissor(commandBuffer);

	ranges::for_each(mPlugins, [&](Plugin* p){ p->OnRenderCamera(commandBuffer, camera); });

	// draw loop

	shared_ptr<Buffer> instanceBuffer;
	uint32_t instanceCount = 0;
	Renderer* firstInstance = nullptr;
	for (Renderer* renderer : renderers) {
		if (firstInstance) {
			if (firstInstance->TryCombineInstances(commandBuffer, renderer, instanceBuffer, instanceCount))
				continue; // instanced, skip
			// draw firstInstance
			if (instanceCount > 1)
				firstInstance->OnDrawInstanced(commandBuffer, camera, instanceBuffer, instanceCount);
			else
				firstInstance->OnDraw(commandBuffer, camera);
		}
		instanceCount = 1;
		firstInstance = renderer;
	}
	if (firstInstance)
		if (instanceCount > 1)
			firstInstance->OnDrawInstanced(commandBuffer, camera, instanceBuffer, instanceCount);
		else
			firstInstance->OnDraw(commandBuffer, camera);
}

shared_ptr<Framebuffer> RenderGraph::Render(CommandBuffer& commandBuffer) {  
	ProfilerRegion ps("RenderGraph::Render", commandBuffer);
  Validate(commandBuffer);

	commandBuffer.BeginRenderPass(mRenderPass, mFramebuffer);
	
	for (const auto& subpass : mRenderPass->SubpassDescriptions()) {
		ProfilerRegion ps(subpass.mName, commandBuffer);
		for (auto camera : mNode.mScene.get_components<Camera>())
			if (camera.RendersToSubpass(subpass))
				mNode.mScene.Render(commandBuffer, *camera);
		
		if (mRenderPass->SubpassDescriptions().size() > 1)
			commandBuffer.NextSubpass();
	}
	
	commandBuffer.EndRenderPass();
	return mFramebuffer;
}
