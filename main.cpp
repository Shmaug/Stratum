#include <Core/PluginManager.hpp>
#include <Core/Window.hpp>
#include <Scene/Scene.hpp>

using namespace std;

int main(int argc, char** argv) {
	#if defined(WINDOWS) && defined(_DEBUG)
	_CrtMemState s0;
	_CrtMemCheckpoint(&s0);
	#endif

	Instance* instance = new Instance(argc, argv);
	Scene* scene = new Scene(instance);

	Semaphore* commandSemaphore = new Semaphore(instance->Device());
	while (instance->BeginFrame()) {
		scene->MainRenderExtent(instance->Window()->SwapchainExtent());

		CommandBuffer* commandBuffer = instance->Device()->GetCommandBuffer();
		if (instance->Window()->SwapchainExtent().width > 0 && instance->Window()->SwapchainExtent().height > 0)
			commandBuffer->WaitOn(vk::PipelineStageFlagBits::eTransfer, instance->Window()->ImageAvailableSemaphore());
		commandBuffer->Signal(vk::PipelineStageFlagBits::eAllCommands, commandSemaphore);
		
		scene->Update(commandBuffer);
		scene->Render(commandBuffer);

		Texture* srcImage = scene->HasAttachment("stm_main_resolve") ? scene->GetAttachment("stm_main_resolve") : scene->GetAttachment("stm_main_render");
		commandBuffer->TransitionBarrier(srcImage, vk::ImageLayout::eTransferSrcOptimal);
		commandBuffer->TransitionBarrier(instance->Window()->BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal);
		vk::ImageCopy rgn = {};
		rgn.dstSubresource = rgn.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
		rgn.extent = { instance->Window()->SwapchainExtent().width, instance->Window()->SwapchainExtent().height, 1 };
		((vk::CommandBuffer)*commandBuffer).copyImage(
			*srcImage, vk::ImageLayout::eTransferSrcOptimal,
			instance->Window()->BackBuffer(), vk::ImageLayout::eTransferDstOptimal, { rgn });
		commandBuffer->TransitionBarrier(instance->Window()->BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR);
		
		instance->Device()->Execute(commandBuffer);
		instance->EndFrame({ *commandSemaphore });
	}
	instance->Device()->Flush();

	delete commandSemaphore;

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