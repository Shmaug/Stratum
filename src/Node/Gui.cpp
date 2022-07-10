#include "Gui.hpp"
#include "Application.hpp"

#include <imgui/imgui_internal.h>
#include <stb_image_write.h>

#include <Core/Window.hpp>

namespace stm {
#pragma pack(push)
#pragma pack(1)
#include <Shaders/scene.h>
#pragma pack(pop)
}

using namespace stm;

Gui::Gui(Node& node) : mNode(node) {
	auto app = mNode.find_in_ancestor<Application>();
	app->OnUpdate.add_listener(mNode, bind_front(&Gui::new_frame, this), Node::EventPriority::eFirst);
	app->OnUpdate.add_listener(mNode, bind(&Gui::make_geometry, this, std::placeholders::_1), Node::EventPriority::eAlmostLast);
	app->OnRenderWindow.add_listener(mNode, [&,app](CommandBuffer& commandBuffer) { render(commandBuffer, app->window().back_buffer()); }, Node::EventPriority::eLast);

	mContext = ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	io.KeyMap[ImGuiKey_Tab] = (int)KeyCode::eKeyTab;
	io.KeyMap[ImGuiKey_LeftArrow] = (int)KeyCode::eKeyLeft;
	io.KeyMap[ImGuiKey_RightArrow] = (int)KeyCode::eKeyRight;
	io.KeyMap[ImGuiKey_UpArrow] = (int)KeyCode::eKeyUp;
	io.KeyMap[ImGuiKey_DownArrow] = (int)KeyCode::eKeyDown;
	io.KeyMap[ImGuiKey_PageUp] = (int)KeyCode::eKeyPageUp;
	io.KeyMap[ImGuiKey_PageDown] = (int)KeyCode::eKeyPageDown;
	io.KeyMap[ImGuiKey_Home] = (int)KeyCode::eKeyHome;
	io.KeyMap[ImGuiKey_End] = (int)KeyCode::eKeyEnd;
	io.KeyMap[ImGuiKey_Insert] = (int)KeyCode::eKeyInsert;
	io.KeyMap[ImGuiKey_Delete] = (int)KeyCode::eKeyDelete;
	io.KeyMap[ImGuiKey_Backspace] = (int)KeyCode::eKeyBackspace;
	io.KeyMap[ImGuiKey_Space] = (int)KeyCode::eKeySpace;
	io.KeyMap[ImGuiKey_Enter] = (int)KeyCode::eKeyEnter;
	io.KeyMap[ImGuiKey_Escape] = (int)KeyCode::eKeyEscape;
	io.KeyMap[ImGuiKey_KeyPadEnter] = (int)KeyCode::eKeyEnter;
	io.KeyMap[ImGuiKey_A] = (int)KeyCode::eKeyA;
	io.KeyMap[ImGuiKey_C] = (int)KeyCode::eKeyC;
	io.KeyMap[ImGuiKey_V] = (int)KeyCode::eKeyV;
	io.KeyMap[ImGuiKey_X] = (int)KeyCode::eKeyX;
	io.KeyMap[ImGuiKey_Y] = (int)KeyCode::eKeyY;
	io.KeyMap[ImGuiKey_Z] = (int)KeyCode::eKeyZ;

	io.Fonts->AddFontFromFileTTF("DroidSans.ttf", 16.f);

	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 5;
	style.ScrollbarSize *= 0.75f;

	mMesh.topology() = vk::PrimitiveTopology::eTriangleList;

	unordered_map<VertexArrayObject::AttributeType, vector<VertexArrayObject::Attribute>> attributes;
	attributes[VertexArrayObject::AttributeType::ePosition].emplace_back(VertexArrayObject::AttributeDescription(sizeof(ImDrawVert), vk::Format::eR32G32Sfloat,  (uint32_t)offsetof(ImDrawVert, pos), vk::VertexInputRate::eVertex), Buffer::View<byte>{});
	attributes[VertexArrayObject::AttributeType::eTexcoord].emplace_back(VertexArrayObject::AttributeDescription(sizeof(ImDrawVert), vk::Format::eR32G32Sfloat,  (uint32_t)offsetof(ImDrawVert, uv ), vk::VertexInputRate::eVertex), Buffer::View<byte>{});
	attributes[VertexArrayObject::AttributeType::eColor   ].emplace_back(VertexArrayObject::AttributeDescription(sizeof(ImDrawVert), vk::Format::eR8G8B8A8Unorm, (uint32_t)offsetof(ImDrawVert, col), vk::VertexInputRate::eVertex), Buffer::View<byte>{});
	mMesh.vertices() = make_shared<VertexArrayObject>(attributes);

	const auto& color_image_fs = make_shared<Shader>(app->window().mInstance.device(), "Shaders/raster_color_image_fs.spv");
	mPipeline = make_shared<GraphicsPipelineState>("Gui", make_shared<Shader>(app->window().mInstance.device(), "Shaders/raster_color_image_vs.spv"), color_image_fs);
	mPipeline->raster_state().setFrontFace(vk::FrontFace::eClockwise);
	mPipeline->depth_stencil().setDepthTestEnable(false);
	mPipeline->depth_stencil().setDepthWriteEnable(false);
	mPipeline->blend_states() = { vk::PipelineColorBlendAttachmentState(true,
		vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
		vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
		vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA) };
	mPipeline->set_immutable_sampler("gSampler", make_shared<Sampler>(color_image_fs->mDevice, "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE)));

	mPipeline->descriptor_binding_flag("gImages", vk::DescriptorBindingFlagBits::ePartiallyBound);
}
Gui::~Gui() {
	ImGui::DestroyContext(mContext);
}

void Gui::create_font_image(CommandBuffer& commandBuffer) {
	unsigned char* pixels;
	int width, height;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	Buffer::View<byte> staging = make_shared<Buffer>(commandBuffer.mDevice, "ImGuiNode::CreateImages/Staging", width*height*texel_size(vk::Format::eR8G8B8A8Unorm), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
	memcpy(staging.data(), pixels, staging.size_bytes());
	Image::View img = commandBuffer.copy_buffer_to_image(staging, make_shared<Image>(commandBuffer.mDevice, "Gui/Image", vk::Extent3D(width, height, 1), vk::Format::eR8G8B8A8Unorm, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage));
	mPipeline->descriptor("gImages", 0) = image_descriptor(img, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
}

void Gui::new_frame(CommandBuffer& commandBuffer, float deltaTime) {
	ProfilerRegion ps("Gui::new_frame");

	set_context();
	ImGuiIO& io = ImGui::GetIO();
	mImageMap.clear();

	Descriptor& imagesDescriptor = mPipeline->descriptor("gImages", 0);
	if (imagesDescriptor.index() != 0 || !get<Image::View>(imagesDescriptor))
		create_font_image(commandBuffer);

	Window& window = commandBuffer.mDevice.mInstance.window();
	io.DisplaySize = ImVec2((float)window.swapchain_extent().width, (float)window.swapchain_extent().height);
	io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
	io.DeltaTime = deltaTime;

	const MouseKeyboardState& input = window.input_state();
	io.MousePos = ImVec2(input.cursor_pos().x(), input.cursor_pos().y());
	io.MouseWheel = input.scroll_delta();
	io.MouseDown[0] = input.pressed(KeyCode::eMouse1);
	io.MouseDown[1] = input.pressed(KeyCode::eMouse2);
	io.MouseDown[2] = input.pressed(KeyCode::eMouse3);
	io.MouseDown[3] = input.pressed(KeyCode::eMouse4);
	io.MouseDown[4] = input.pressed(KeyCode::eMouse5);
	io.KeyCtrl = input.pressed(KeyCode::eKeyControl);
	io.KeyShift = input.pressed(KeyCode::eKeyShift);
	io.KeyAlt = input.pressed(KeyCode::eKeyAlt);
	ranges::uninitialized_fill(io.KeysDown, 0);
	for (KeyCode key : input.buttons())
		io.KeysDown[size_t(key)] = 1;
	io.AddInputCharactersUTF8(input.input_characters().c_str());

	ImGui::NewFrame();
}

void Gui::make_geometry(CommandBuffer& commandBuffer) {
	ProfilerRegion ps("Gui::make_geometry");
	set_context();
	ImGui::Render();

	mDrawData = ImGui::GetDrawData();
	if (mDrawData && mDrawData->TotalVtxCount) {
		Buffer::View<ImDrawVert> vertices = make_shared<Buffer>(commandBuffer.mDevice, "ImGui Vertices", mDrawData->TotalVtxCount*sizeof(ImDrawVert), vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
		Buffer::View<ImDrawIdx>  indices  = make_shared<Buffer>(commandBuffer.mDevice, "ImGui Indices" , mDrawData->TotalIdxCount*sizeof(ImDrawIdx), vk::BufferUsageFlagBits::eIndexBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
		auto dstVertex = vertices.begin();
		auto dstIndex  = indices.begin();
		for (const ImDrawList* cmdList : span(mDrawData->CmdLists, mDrawData->CmdListsCount)) {
			ranges::copy(cmdList->VtxBuffer, dstVertex);
			ranges::copy(cmdList->IdxBuffer, dstIndex);
			dstVertex += cmdList->VtxBuffer.size();
			dstIndex  += cmdList->IdxBuffer.size();
			for (const ImDrawCmd& cmd : cmdList->CmdBuffer) {
				if (cmd.TextureId != nullptr) {
					Image::View& view = *reinterpret_cast<Image::View*>(cmd.TextureId);
					if (!mImageMap.contains(view)) {
						uint32_t idx = 1 + (uint32_t)mImageMap.size();
						mPipeline->descriptor("gImages", idx) = image_descriptor(view, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
						mImageMap.emplace(view, idx);
					}
				}
			}
		}

		mMesh[VertexArrayObject::AttributeType::ePosition][0].second = vertices;
		mMesh[VertexArrayObject::AttributeType::eTexcoord][0].second = vertices;
		mMesh[VertexArrayObject::AttributeType::eColor   ][0].second = vertices;
		mMesh.indices() = indices;

		commandBuffer.hold_resource(vertices);
		commandBuffer.hold_resource(indices);

		Descriptor& imagesDescriptor = mPipeline->descriptor("gImages", 0);
		if (imagesDescriptor.index() != 0 || !get<Image::View>(imagesDescriptor))
			create_font_image(commandBuffer);

		mPipeline->transition_images(commandBuffer);
	}
}
void Gui::render(CommandBuffer& commandBuffer, const Image::View& dst) {
	if (!mDrawData || mDrawData->CmdListsCount <= 0 || mDrawData->DisplaySize.x == 0 || mDrawData->DisplaySize.y == 0) return;

	ProfilerRegion ps("Gui::render", commandBuffer);

	RenderPass::SubpassDescription subpass {
		{ "colorBuffer", {
			AttachmentType::eColor, blend_mode_state(), vk::AttachmentDescription{ {},
				dst.image()->format(), dst.image()->sample_count(),
				vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal } }
		}
	};
	auto renderPass = make_shared<RenderPass>(dst.image()->mDevice, "Gui RenderPass", ranges::single_view { subpass });
	auto framebuffer = make_shared<Framebuffer>(*renderPass, "Gui Framebuffer", ranges::single_view { dst });
	commandBuffer.begin_render_pass(renderPass, framebuffer, vk::Rect2D{ {}, framebuffer->extent() }, { {} });

	float2 scale = float2::Map(&mDrawData->DisplaySize.x);
	float2 offset = float2::Map(&mDrawData->DisplayPos.x);

	Buffer::View<TransformData> identity_transform = make_shared<Buffer>(commandBuffer.mDevice, "gCameraTransform", sizeof(TransformData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	identity_transform[0] = make_transform(float3(0,0,1), quatf_identity(), float3::Ones());
	Buffer::View<ViewData> views = make_shared<Buffer>(commandBuffer.mDevice, "gCameraData", sizeof(ViewData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	views[0].projection = make_orthographic(scale, -1 - offset.array()*2/scale.array(), 0, 1);
	views[0].image_min = { 0, 0 };
	views[0].image_max = { framebuffer->extent().width, framebuffer->extent().height };

	mPipeline->descriptor("gViews") = commandBuffer.hold_resource(views);
	mPipeline->descriptor("gInverseViewTransforms") = commandBuffer.hold_resource(identity_transform);
	mPipeline->push_constant<uint32_t>("gViewIndex") = 0;
	mPipeline->push_constant<float4>("gImageST") = float4(1,1,0,0);
	mPipeline->push_constant<float4>("gColor") = float4::Ones();

	commandBuffer.bind_pipeline(mPipeline->get_pipeline(commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(), mMesh.vertex_layout(*mPipeline->stage(vk::ShaderStageFlagBits::eVertex))));
	mPipeline->bind_descriptor_sets(commandBuffer);
	mPipeline->push_constants(commandBuffer);
	mMesh.bind(commandBuffer);

	commandBuffer->setViewport(0, vk::Viewport(mDrawData->DisplayPos.x, mDrawData->DisplayPos.y, mDrawData->DisplaySize.x, mDrawData->DisplaySize.y, 0, 1));

	uint32_t voff = 0, ioff = 0;
	for (const ImDrawList* cmdList : span(mDrawData->CmdLists, mDrawData->CmdListsCount)) {
		for (const ImDrawCmd& cmd : cmdList->CmdBuffer)
			if (cmd.UserCallback) {
				// TODO: reset render state callback
				// if (cmd->UserCallback == ResetRenderState)
				cmd.UserCallback(cmdList, &cmd);
			} else {
				commandBuffer.push_constant("gImageIndex", cmd.TextureId ? mImageMap.at(*reinterpret_cast<Image::View*>(cmd.TextureId)) : 0);
				vk::Offset2D offset((int32_t)cmd.ClipRect.x, (int32_t)cmd.ClipRect.y);
				vk::Extent2D extent((uint32_t)(cmd.ClipRect.z - cmd.ClipRect.x), (uint32_t)(cmd.ClipRect.w - cmd.ClipRect.y));
				commandBuffer->setScissor(0, vk::Rect2D(offset, extent));
				commandBuffer->drawIndexed(cmd.ElemCount, 1, ioff + cmd.IdxOffset, voff + cmd.VtxOffset, 0);
			}
		voff += cmdList->VtxBuffer.size();
		ioff += cmdList->IdxBuffer.size();
	}

	commandBuffer.end_render_pass();
}