#include "Core/Window.hpp"
#include "Scene/RenderGraph.hpp"

using namespace stm;

int main(int argc, char** argv) {
	#if defined(WIN32) && defined(_DEBUG)
	_CrtMemState s0;
	_CrtMemCheckpoint(&s0);
	#endif

	unique_ptr<Instance> instance = make_unique<Instance>(argc, argv);
	unique_ptr<Scene> scene = make_unique<Scene>(*instance);
	Window& window = instance->Window();

	RenderGraph& rg = scene->Root().CreateComponent<RenderGraph>("window_rendergraph");

	auto frameSemaphore = make_shared<Semaphore>("Frame Semaphore", instance->Device());
	
	while (instance->AdvanceFrame()) {
		auto commandBuffer = instance->Device().GetCommandBuffer("Frame CommandBuffer");
		
		scene->Update(*commandBuffer);
		if (window.Swapchain()) {

			auto fb = rg.Render(*commandBuffer);

			// copy to window
			shared_ptr<Texture> srcImage = fb->GetAttachment("stm_main_resolve");
			if (!srcImage) srcImage = fb->GetAttachment("stm_main_render");

			srcImage->TransitionBarrier(*commandBuffer, vk::ImageLayout::eTransferSrcOptimal);
			commandBuffer->TransitionBarrier(window.BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal);
			vk::ImageCopy rgn = {};
			rgn.dstSubresource = rgn.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
			rgn.extent = vk::Extent3D(window.SwapchainExtent(), 1);
			(*commandBuffer)->copyImage(**srcImage, vk::ImageLayout::eTransferSrcOptimal, window.BackBuffer(), vk::ImageLayout::eTransferDstOptimal, { rgn });
			commandBuffer->TransitionBarrier(window.BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR);
			commandBuffer->SignalOnComplete(vk::PipelineStageFlagBits::eTransfer, frameSemaphore);
		}
		
		instance->Device().Execute(move(commandBuffer));
		instance->PresentFrame({ **frameSemaphore, *window.ImageAvailableSemaphore() });
	}
	instance->Device().Flush();
	frameSemaphore.reset();
	
	instance->Device().UnloadAssets();

	scene.reset();
	instance.reset();

	#if defined(WIN32) && defined(_DEBUG)
	_CrtMemState s1;
	_CrtMemCheckpoint(&s1);
	_CrtMemState ds;
	_CrtMemDifference(&ds, &s0, &s1);
	if (ds.lTotalCount) _CrtMemDumpStatistics(&ds);
	#endif
	return EXIT_SUCCESS;
}