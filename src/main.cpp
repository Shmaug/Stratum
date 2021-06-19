#include "Core/Window.hpp"

#include "NodeGraph/PbrRenderer.hpp"
#include "NodeGraph/ImGuiRenderer.hpp"

#include <json.hpp>

using namespace stm;

class Application {
private:
	Instance& mInstance;

	RenderGraph& mMainPass;
	ImGuiRenderer& mImGuiRenderer;
	PbrRenderer& mPbrRenderer;

public:
	inline Application(NodeGraph::Node& node, Instance& instance) :
		mInstance(instance), mMainPass(node.make_component<RenderGraph>()), mPbrRenderer(node.make_component<PbrRenderer>()), mImGuiRenderer(node.make_component<ImGuiRenderer>()) {
		
		vk::PipelineColorBlendAttachmentState blendOpaque;
		blendOpaque.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		mMainPass[""].bind_point(vk::PipelineBindPoint::eGraphics);
		mMainPass[""]["primaryColor"] =  RenderPass::SubpassDescription::AttachmentInfo(
			RenderPass::AttachmentTypeFlags::eColor, blendOpaque, vk::AttachmentDescription({},
				mInstance.window().surface_format().format, vk::SampleCountFlagBits::e4,
				vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal));
		mMainPass[""]["primaryDepth"] =  RenderPass::SubpassDescription::AttachmentInfo(
			RenderPass::AttachmentTypeFlags::eDepthStencil, blendOpaque, vk::AttachmentDescription({},
				vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e4,
				vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal));
		mMainPass[""]["primaryResolve"] =  RenderPass::SubpassDescription::AttachmentInfo(
			RenderPass::AttachmentTypeFlags::eResolve, blendOpaque, vk::AttachmentDescription({},
				mInstance.window().surface_format().format, vk::SampleCountFlagBits::e1,
				vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR));

		mMainPass.PreRender.emplace(node, bind_front(&PbrRenderer::pre_render, &mPbrRenderer));
		mMainPass.PreRender.emplace(node, bind_front(&ImGuiRenderer::pre_render, &mImGuiRenderer));
		mMainPass.OnDraw.emplace(node, bind_front(&PbrRenderer::draw, &mPbrRenderer, ref(mPbrRenderer.material())));
		mMainPass.OnDraw.emplace(node, bind_front(&ImGuiRenderer::draw, &mImGuiRenderer)); // TODO: ensure this is always last in the draw queue

		{
			auto commandBuffer = mInstance.device().get_command_buffer("Init");
			mImGuiRenderer.create_textures(*commandBuffer);
			for (const string& filepath : mInstance.find_arguments("load_gltf"))
				mPbrRenderer.load_gltf(*commandBuffer, filepath);
			mInstance.device().submit(commandBuffer);
			mInstance.device().flush();
		}
	}

	inline void run() {
		stm::Window& window = mInstance.window();
		Device& device = mInstance.device();

		float fovy = radians(60.f);
		Vector2f euler = Vector2f::Zero();
		hlsl::TransformData cameraToWorld = hlsl::make_transform(Vector3f(0,1,-3));
		auto t0 = chrono::high_resolution_clock::now();

		while (true) {
			Profiler::begin_sample("Frame" + to_string(window.present_count()));
			mInstance.poll_events();
			if (!window.handle()) break; // Window was closed

			auto t1 = chrono::high_resolution_clock::now();
			float deltaTime = chrono::duration_cast<chrono::duration<float>>(t1 - t0).count();
			t0 = t1;

			{
				ProfilerRegion ps("Camera Controls");
				if (!ImGui::GetIO().WantCaptureMouse) {
					if (window.pressed(KeyCode::eMouse2)) {
						euler += window.cursor_delta().reverse() * .005f;
						euler.x() = clamp(euler.x(), -numbers::pi_v<float>/2, numbers::pi_v<float>/2);
						cameraToWorld.Rotation = Quaternionf(AngleAxisf(euler.y(), Vector3f(0,1,0))) * Quaternionf(AngleAxisf(euler.x(), Vector3f(1,0,0)));
					}
					fovy = clamp(fovy - window.scroll_delta(), radians(20.f), radians(90.f));
				}
				if (!ImGui::GetIO().WantCaptureKeyboard) {
					Vector3f mv = Vector3f::Zero();
					if (window.pressed(KeyCode::eKeyD)) mv += Vector3f( 1,0,0);
					if (window.pressed(KeyCode::eKeyA)) mv += Vector3f(-1,0,0);
					if (window.pressed(KeyCode::eKeyW)) mv += Vector3f(0,0, 1);
					if (window.pressed(KeyCode::eKeyS)) mv += Vector3f(0,0,-1);
					cameraToWorld.Translation += (cameraToWorld.Rotation*mv*deltaTime).array();
				}
				mPbrRenderer.material().push_constant("gWorldToCamera", inverse(cameraToWorld));
			}

			mImGuiRenderer.new_frame(window, deltaTime);

			Profiler::imgui();

			ImGui::Render();
			
			auto commandBuffer = device.get_command_buffer("Frame");
			if (auto backBuffer = window.acquire_image(*commandBuffer)) {
				mPbrRenderer.material().push_constant("gProjection", hlsl::make_perspective(fovy, (float)backBuffer.texture().extent().height/(float)backBuffer.texture().extent().width, 0, 64));
				mMainPass.render(*commandBuffer, { { "primaryResolve", backBuffer } });
				
				Semaphore& semaphore = commandBuffer->hold_resource<Semaphore>(device, "RenderSemaphore");
				device.submit(commandBuffer, { *window.image_available_semaphore() }, { vk::PipelineStageFlagBits::eColorAttachmentOutput }, { *semaphore });
				window.present({ *semaphore });
			}
			Profiler::end_sample();
		}
		Profiler::clear_history();
		mInstance.device().flush();
	}
};

int main(int argc, char** argv) {
	unique_ptr<Instance> instance = make_unique<Instance>(argc, argv);
	{
		NodeGraph nodeGraph;
		NodeGraph::Node& root = nodeGraph.emplace("Application");
		auto& spirvModules = root.make_component<spirv_module_map>();
		for (const fs::path& p : fs::directory_iterator("Assets/SPIR-V"))
			if (p.extension() == ".spv")
				spirvModules.emplace(p.stem().string(), make_shared<SpirvModule>(instance->device(), p)).first->first;
		
		Application app(root, *instance);
		app.run();
		nodeGraph.clear();
	}
	Profiler::clear_history();
	instance.reset();
	return EXIT_SUCCESS;
}