#include "Application.hpp"
#include "Scene.hpp"
#include "Gui.hpp"

namespace stm {

void Application::run() {
	auto instance = mNode.find_in_ancestor<Instance>();

	vector<chrono::steady_clock::time_point> submitTimes;

	auto t0 = chrono::high_resolution_clock::now();
	while (true) {
		instance->poll_events();
		if (!mWindow.handle()) break;
		if (!mWindow.wants_repaint()) continue;
		if (!mWindow.acquire_image()) continue;

		submitTimes.resize(mWindow.back_buffer_count());

		auto& [qp,queryCount,labels] = instance->device().query_pool();
		vector<pair<string,chrono::nanoseconds>> timestamps(labels.size());
		if (!labels.empty()) {
			const uint32_t n = min(queryCount, (uint32_t)labels.size());
			const vector<uint64_t> times = instance->device()->getQueryPoolResults<uint64_t>(qp, 0, n, 2*n*sizeof(uint64_t), sizeof(uint64_t), vk::QueryResultFlagBits::e64|vk::QueryResultFlagBits::eWithAvailability);
			for (uint32_t i = 0; i < n; i++)
				timestamps[i] = { labels[i], chrono::nanoseconds(times[i]) };
			Profiler::set_timestamps(submitTimes[mWindow.back_buffer_index()], timestamps);

			if (labels.size() > queryCount) {
				queryCount = (uint32_t)labels.size();
				instance->device()->destroyQueryPool(qp);
				qp = instance->device()->createQueryPool(vk::QueryPoolCreateInfo({}, vk::QueryType::eTimestamp, queryCount));
			}
		}

		Profiler::begin_frame();

		{
			ProfilerRegion ps("Application::PreFrame");
			PreFrame();
		}

		auto t1 = chrono::high_resolution_clock::now();
		float deltaTime = chrono::duration_cast<chrono::duration<float>>(t1 - t0).count();
		t0 = t1;

		auto commandBuffer = instance->device().get_command_buffer("Frame");

		// reset timestamp query pool
		{
			labels.clear();
			(*commandBuffer)->resetQueryPool(qp, 0, queryCount);
			commandBuffer->write_timestamp(vk::PipelineStageFlagBits::eTopOfPipe, "");
		}

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

		commandBuffer->write_timestamp(vk::PipelineStageFlagBits::eBottomOfPipe, "");

		auto cmd_semaphore = make_shared<Semaphore>(commandBuffer->mDevice, "cmd semaphore");
		commandBuffer->signal_when_done(cmd_semaphore);
		commandBuffer->mDevice.submit(commandBuffer);
		submitTimes[mWindow.back_buffer_index()] = chrono::high_resolution_clock::now();
		mWindow.present(**cmd_semaphore);

		{
			ProfilerRegion ps("Application::PostFrame");
			PostFrame();
		}
	}
}

}