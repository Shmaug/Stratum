#include "Core/Material.hpp"
#include "Core/Window.hpp"

#include "NodeGraph/pbrRenderer.hpp"

using namespace stm;

unordered_map<string, shared_ptr<SpirvModule>> gSpirvModules;

int main(int argc, char** argv) {
	unique_ptr<Instance> instance = make_unique<Instance>(argc, argv);
	{
		stm::Window& window = instance->window();
		Device& device = instance->device();

		for (const fs::path& p : fs::directory_iterator("Assets/SPIR-V")) {
			auto m = make_shared<SpirvModule>(p);
			cout << gSpirvModules.emplace(p.stem().string()+"/"+m->entry_point(), m).first->first << endl;
		}
		
		NodeGraph nodeGraph;
		pbrRenderer pbr(nodeGraph, device, gSpirvModules.at("pbr/vs"), gSpirvModules.at("pbr/fs"));
		
		{
			auto commandBuffer = device.get_command_buffer("Init");
			for (const string& filepath : instance->find_arguments("load_gltf"))
				pbr.load_gltf(*commandBuffer, filepath);
			device.submit(commandBuffer);
			device.flush();
		}

		hlsl::TransformData gCameraToWorld = hlsl::make_transform(Vector3f(0,1,-3));
		float fovy = radians(60.f);

		while (true) {
			Profiler::begin_sample("Frame" + to_string(window.present_count()));
			instance->poll_events();
			if (!window.handle()) break; // Window was closed

			{
				ProfilerRegion ps("Camera Controls");
				
				static auto t0 = chrono::high_resolution_clock::now();
				auto t1 = chrono::high_resolution_clock::now();
				auto dt = t1 - t0;
				t0 = t1;

				float deltaTime = chrono::duration_cast<chrono::duration<float>>(dt).count();

				// count fps
				static float frameTimeAccum = 0;
				static uint32_t fpsAccum = 0;
				frameTimeAccum += deltaTime;
				fpsAccum++;
				if (frameTimeAccum > 1) {
					float totalTime = chrono::duration<float>(frameTimeAccum).count();
					printf("%.2f fps (%fms)\t\t\r", (float)fpsAccum/totalTime, totalTime/fpsAccum);
					frameTimeAccum -= 1;
					fpsAccum = 0;
				}

				if (window.pressed(KeyCode::eMouse2)) {
					static Vector2f euler = Vector2f::Zero();
					euler += window.cursor_delta().reverse() * .005f;
					euler.x() = clamp(euler.x(), -numbers::pi_v<float>/2, numbers::pi_v<float>/2);
					gCameraToWorld.Rotation = Quaternionf(AngleAxisf(euler.y(), Vector3f(0,1,0))) * Quaternionf(AngleAxisf(euler.x(), Vector3f(1,0,0)));
				}
				Vector3f mv = Vector3f::Zero();
				if (window.pressed(KeyCode::eKeyD)) mv += Vector3f( 1,0,0);
				if (window.pressed(KeyCode::eKeyA)) mv += Vector3f(-1,0,0);
				if (window.pressed(KeyCode::eKeyW)) mv += Vector3f(0,0, 1);
				if (window.pressed(KeyCode::eKeyS)) mv += Vector3f(0,0,-1);
				gCameraToWorld.Translation += (gCameraToWorld.Rotation*mv*deltaTime).array();

				fovy = clamp(fovy - window.scroll_delta(), radians(20.f), radians(90.f));
			}

			auto commandBuffer = device.get_command_buffer("Frame");
			if (auto backBuffer = window.acquire_image(*commandBuffer)) {
				pbr.render(*commandBuffer, backBuffer, gCameraToWorld, hlsl::make_perspective(fovy, (float)backBuffer.texture().extent().height/(float)backBuffer.texture().extent().width, 0, 64));
				Semaphore& semaphore = commandBuffer->hold_resource<Semaphore>(device, "RenderSemaphore");
				device.submit(commandBuffer, { *window.image_available_semaphore() }, { vk::PipelineStageFlagBits::eColorAttachmentOutput }, { *semaphore });
				window.present({ *semaphore });
			}
		}
		
		device.flush();
		gSpirvModules.clear();
	}
	instance.reset();
	Profiler::clear();
	return EXIT_SUCCESS;
}