#include "Application.hpp"
#include "Scene.hpp"
#include "Gui.hpp"

namespace stm {

void Application::run() {
	auto instance = mNode.find_in_ancestor<Instance>();

	auto t0 = chrono::high_resolution_clock::now();
	while (true) {
		instance->poll_events();
		if (!mWindow.handle()) break;
		if (!mWindow.wants_repaint()) continue;
		if (!mWindow.acquire_image()) continue;

		Profiler::begin_frame();

		{
			ProfilerRegion ps("Application::PreFrame");
			PreFrame();
		}

		auto t1 = chrono::high_resolution_clock::now();
		float deltaTime = chrono::duration_cast<chrono::duration<float>>(t1 - t0).count();
		t0 = t1;

		auto commandBuffer = mWindow.mInstance.device().get_command_buffer("Frame");

		{
			ProfilerRegion ps("Application::OnUpdate");
			OnUpdate(*commandBuffer, deltaTime);
		}

		{
			ProfilerRegion ps("Application::OnRenderWindow");
			OnRenderWindow(*commandBuffer);
			mWindow.resolve(*commandBuffer);
		}

		commandBuffer->wait_for(mWindow.image_available_semaphore(), vk::PipelineStageFlagBits::eTransfer);

		auto cmd_semaphore = make_shared<Semaphore>(commandBuffer->mDevice, "cmd semaphore");
		commandBuffer->signal_when_done(cmd_semaphore);
		commandBuffer->mDevice.submit(commandBuffer);
		mWindow.present(**cmd_semaphore);

		{
			ProfilerRegion ps("Application::PostFrame");
			PostFrame();
		}
	}
}

}