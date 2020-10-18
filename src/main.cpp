#include "Core/PluginManager.hpp"
#include "Core/Window.hpp"
#include "Scene/Scene.hpp"

using namespace std;
using namespace stm;

int main(int argc, char** argv) {
	#if defined(WINDOWS) && defined(_DEBUG)
	_CrtMemState s0;
	_CrtMemCheckpoint(&s0);
	#endif

	Instance* instance = new Instance(argc, argv);
	Scene* scene = new Scene(instance);

	Window* window = instance->Window();
	auto frameSemaphore = make_shared<Semaphore>("Frame Semaphore", instance->Device());
	while (instance->AdvanceSwapchain()) {

		if (window->Swapchain()) scene->MainRenderExtent(window->SwapchainExtent());

		CommandBuffer* commandBuffer = instance->Device()->GetCommandBuffer("Frame CommandBuffer");
		scene->Update(*commandBuffer);

		if (window->Swapchain()) {
			scene->Render(*commandBuffer);
			shared_ptr<Texture> srcImage = scene->HasAttachment("stm_main_resolve") ? scene->GetAttachment("stm_main_resolve") : scene->GetAttachment("stm_main_render");

			commandBuffer->TransitionBarrier(*srcImage, vk::ImageLayout::eTransferSrcOptimal);
			commandBuffer->TransitionBarrier(window->BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal);

			vk::ImageCopy rgn = {};
			rgn.dstSubresource = rgn.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
			rgn.extent = vk::Extent3D(window->SwapchainExtent(), 1);
			(*commandBuffer)->copyImage(
				**srcImage, vk::ImageLayout::eTransferSrcOptimal,
				window->BackBuffer(), vk::ImageLayout::eTransferDstOptimal, { rgn });

			commandBuffer->TransitionBarrier(window->BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR);
			commandBuffer->SignalOnComplete(vk::PipelineStageFlagBits::eTransfer, frameSemaphore);
		}
		
		instance->Device()->Execute(commandBuffer);
		instance->Present({ **frameSemaphore, **window->ImageAvailableSemaphore() });
	}
	instance->Device()->Flush();
	frameSemaphore.reset();
	
	instance->Device()->UnloadAssets();

	safe_delete(scene);
	safe_delete(instance);

	#if defined(WINDOWS) && defined(_DEBUG)
	_CrtMemState s1;
	_CrtMemCheckpoint(&s1);
	_CrtMemState ds;
	_CrtMemDifference(&ds, &s0, &s1);
	if (ds.lTotalCount) _CrtMemDumpStatistics(&ds);
	#endif
	return EXIT_SUCCESS;
}