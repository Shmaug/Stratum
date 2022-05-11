#include "Application.hpp"
#include "Scene.hpp"
#include "Gui.hpp"

namespace stm {

void Application::run() {
	auto instance = mNode.find_in_ancestor<Instance>();

	size_t frameCount = 0;
	auto t0 = chrono::high_resolution_clock::now();
	while (true) {
		ProfilerRegion ps("Frame " + to_string(frameCount++));

		instance->poll_events();
		if (!mWindow.handle()) break;
		if (!mWindow.acquire_image()) continue;

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

		commandBuffer->wait_for(mWindow.image_available_semaphore(), vk::PipelineStageFlagBits::eComputeShader);

		{
			ProfilerRegion ps("Application::OnRenderWindow");
			OnRenderWindow(*commandBuffer);
			mWindow.resolve(*commandBuffer);
		}

		auto present_semaphore = make_shared<Semaphore>(commandBuffer->mDevice, "present semaphore");
		commandBuffer->signal_when_done(present_semaphore);
		commandBuffer->mDevice.submit(commandBuffer);
		mWindow.present(**present_semaphore);

		{
			ProfilerRegion ps("Application::PostFrame");
			PostFrame();
		}
	}
}

}