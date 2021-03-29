#include "Core/Window.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/Mesh.hpp"
#include "Core/Profiler.hpp"
#include "Scene/GuiContext.hpp"

using namespace stm;

int main(int argc, char** argv) {
	unique_ptr<Instance> instance = make_unique<Instance>(argc, argv);
	Window& window = instance->Window();
	
	vector<shared_ptr<Semaphore>> frameSemaphores(window.BackBufferCount());
	ranges::generate(frameSemaphores, [&](){ return make_shared<Semaphore>(instance->Device(), "FrameSemaphore"); });

	shared_ptr<Mesh> testMesh = make_shared<Mesh>("test mesh");

	shared_ptr<RenderPass> renderPass; // TODO
	shared_ptr<GraphicsPipeline> pipeline; // TODO
	shared_ptr<Framebuffer> framebuffer; // TODO

	auto frameTimeAccum = chrono::nanoseconds::zero();
	uint32_t fpsAccum = 0;
	while (true) {
		Profiler::BeginSample("Frame" + to_string(instance->Device().FrameCount()));

		{
			ProfilerRegion ps("PollEvents+AcquireNextImage");
			if (!instance->PollEvents()) break; // Window was closed
			window.AcquireNextImage();
		}
		
		auto commandBuffer = instance->Device().GetCommandBuffer("Render");
		commandBuffer->WaitOn(vk::PipelineStageFlagBits::eTransfer, window.ImageAvailableSemaphore());
		commandBuffer->SignalOnComplete(vk::PipelineStageFlagBits::eColorAttachmentOutput, frameSemaphores[window.BackBufferIndex()]);

		if (window.Swapchain()) {
			
			//commandBuffer->BeginRenderPass(renderPass, framebuffer);
			//commandBuffer->BindPipeline(pipeline);
			//testMesh->Draw(*commandBuffer);
			//commandBuffer->EndRenderPass();


			shared_ptr<Texture> srcImage = make_shared<Texture>(instance->Device(), "src img", vk::Extent3D(window.SwapchainExtent(), 1), window.SurfaceFormat().format, byte_blob{}, vk::ImageUsageFlagBits::eTransferSrc);
			commandBuffer->HoldResource(srcImage);
			srcImage->TransitionBarrier(*commandBuffer, vk::ImageLayout::eTransferSrcOptimal);

			commandBuffer->TransitionBarrier(window.BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal);
			
			vk::ImageCopy rgn = {};
			rgn.dstSubresource = rgn.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
			rgn.extent = vk::Extent3D(window.SwapchainExtent(), 1);
			(*commandBuffer)->copyImage(**srcImage, vk::ImageLayout::eTransferSrcOptimal, window.BackBuffer(), vk::ImageLayout::eTransferDstOptimal, { rgn });

			commandBuffer->TransitionBarrier(window.BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR);
		}

		instance->Device().Execute(commandBuffer);

		if (window.Swapchain()) {
			ProfilerRegion ps("Present");
			window.Present({ **frameSemaphores[window.BackBufferIndex()] });
		}

		Profiler::EndSample();
		
		// count fps
		frameTimeAccum += Profiler::FrameHistory().front().mDuration;
		fpsAccum++;
		if (frameTimeAccum > 1s) {
			float fps = (float)fpsAccum/chrono::duration<float>(frameTimeAccum).count();
			frameTimeAccum -= 1s;
			fpsAccum = 0;

			auto sum = transform_reduce(Profiler::FrameHistory().begin(), Profiler::FrameHistory().end(), chrono::nanoseconds::zero(), plus<chrono::nanoseconds>(), [](const auto& s) { return s.mDuration; });
			auto avgTime = chrono::duration<float>(sum).count()/Profiler::FrameHistory().size();
			printf("\r%.2f fps (%.fms)     ", fps, avgTime);
		}
	}
	
	testMesh.reset();

	instance->Device().Flush();
	frameSemaphores.clear();
	instance.reset();
	
	Profiler::ClearHistory();

	return EXIT_SUCCESS;
}