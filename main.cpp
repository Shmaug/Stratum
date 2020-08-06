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
		if (instance->Window()->SwapchainExtent() != VkExtent2D { 0, 0 })
			commandBuffer->WaitOn(VK_PIPELINE_STAGE_TRANSFER_BIT, instance->Window()->ImageAvailableSemaphore());
		commandBuffer->Signal(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, commandSemaphore);
		
		scene->Update(commandBuffer);
		scene->Render(commandBuffer);

		Texture* srcImage = scene->HasAttachment("stm_main_resolve") ? scene->GetAttachment("stm_main_resolve") : scene->GetAttachment("stm_main_render");
		commandBuffer->TransitionBarrier(srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		commandBuffer->TransitionBarrier(instance->Window()->BackBuffer(), { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		VkImageCopy rgn = {};
		rgn.dstSubresource = rgn.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		rgn.extent = { instance->Window()->SwapchainExtent().width, instance->Window()->SwapchainExtent().height, 1 };
		vkCmdCopyImage(*commandBuffer,
			*srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			instance->Window()->BackBuffer(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rgn);
		commandBuffer->TransitionBarrier(instance->Window()->BackBuffer(), { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		
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