#include "Core/Material.hpp"
#include "Core/Window.hpp"

#include "NodeGraph/PbrRenderer.hpp"
#include "NodeGraph/ImGuiRenderer.hpp"

#include <json.hpp>

using namespace stm;

unordered_map<string, shared_ptr<SpirvModule>> gSpirvModules;

int main(int argc, char** argv) {
	unique_ptr<Instance> instance = make_unique<Instance>(argc, argv);
	{
		stm::Window& window = instance->window();
		Device& device = instance->device();

		for (const fs::path& p : fs::directory_iterator("Assets/SPIR-V"))
			if (p.extension() == ".spv")
				gSpirvModules.emplace(p.stem().string(), make_shared<SpirvModule>(device, p)).first->first;
		
		NodeGraph nodeGraph;
		
		vk::PipelineColorBlendAttachmentState blendOpaque;
		blendOpaque.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

		auto mainPass = &nodeGraph.emplace("mainpass").emplace<RenderNode>();
		(*mainPass)[""].bind_point(vk::PipelineBindPoint::eGraphics);
		(*mainPass)[""]["primaryColor"] =  RenderPass::SubpassDescription::AttachmentInfo(
			RenderPass::AttachmentTypeFlags::eColor, blendOpaque, vk::AttachmentDescription({},
				device.mInstance.window().surface_format().format, vk::SampleCountFlagBits::e4,
				vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal));
		(*mainPass)[""]["primaryDepth"] =  RenderPass::SubpassDescription::AttachmentInfo(
			RenderPass::AttachmentTypeFlags::eDepthStencil, blendOpaque, vk::AttachmentDescription({},
				vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e4,
				vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal));
		(*mainPass)[""]["primaryResolve"] =  RenderPass::SubpassDescription::AttachmentInfo(
			RenderPass::AttachmentTypeFlags::eResolve, blendOpaque, vk::AttachmentDescription({},
				device.mInstance.window().surface_format().format, vk::SampleCountFlagBits::e1,
				vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR));

		PbrRenderer pbr(nodeGraph, gSpirvModules.at("pbr_vs"), gSpirvModules.at("pbr_fs"));
		ImGuiRenderer imgui(nodeGraph, gSpirvModules.at("basic_texture_vs"), gSpirvModules.at("basic_texture_fs"));
			
		mainPass->PreRender.emplace(mainPass->node(), bind_front(&PbrRenderer::pre_render, &pbr));
		mainPass->OnDraw.emplace(mainPass->node(),  bind(&PbrRenderer::draw, &pbr, std::placeholders::_1, ref(pbr.material())));

		mainPass->PreRender.emplace(mainPass->node(), bind_front(&ImGuiRenderer::pre_render, &imgui));
		mainPass->OnDraw.emplace(mainPass->node(), bind_front(&ImGuiRenderer::draw, &imgui));
		
		{
			auto commandBuffer = device.get_command_buffer("Init");
			imgui.create_textures(*commandBuffer);
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

				// move camera
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

				imgui.new_frame(window, deltaTime);
			}

			Profiler::draw_imgui();

			auto commandBuffer = device.get_command_buffer("Frame");
			if (auto backBuffer = window.acquire_image(*commandBuffer)) {
				pbr.material().push_constant("gWorldToCamera", inverse(gCameraToWorld));
				pbr.material().push_constant("gProjection", hlsl::make_perspective(fovy, (float)backBuffer.texture().extent().height/(float)backBuffer.texture().extent().width, 0, 64));
				mainPass->render(*commandBuffer, { { "primaryResolve", backBuffer } });
				
				Semaphore& semaphore = commandBuffer->hold_resource<Semaphore>(device, "RenderSemaphore");
				device.submit(commandBuffer, { *window.image_available_semaphore() }, { vk::PipelineStageFlagBits::eColorAttachmentOutput }, { *semaphore });
				window.present({ *semaphore });
			}
		}
		
		device.flush();

		nodeGraph.clear();
		gSpirvModules.clear();
	}
	instance.reset();
	Profiler::clear();
	return EXIT_SUCCESS;
}