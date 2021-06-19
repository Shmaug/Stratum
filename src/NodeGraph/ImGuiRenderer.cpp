#include "ImGuiRenderer.hpp"
#include <imgui_internal.h>

#include "../Core/Window.hpp"

using namespace stm;

ImGuiRenderer::ImGuiRenderer(NodeGraph::Node& node) : mNode(node) {
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;       // We can honor GetMouseCursor() values (optional)
	io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;        // We can honor io.WantSetMousePos requests (optional, rarely used)

	// Keyboard mapping. Dear ImGui will use those indices to peek into the io.KeysDown[] array that we will update during the application lifetime.
	io.KeyMap[ImGuiKey_Tab] = eKeyTab;
	io.KeyMap[ImGuiKey_LeftArrow] = eKeyLeft;
	io.KeyMap[ImGuiKey_RightArrow] = eKeyRight;
	io.KeyMap[ImGuiKey_UpArrow] = eKeyUp;
	io.KeyMap[ImGuiKey_DownArrow] = eKeyDown;
	io.KeyMap[ImGuiKey_PageUp] = eKeyPageUp;
	io.KeyMap[ImGuiKey_PageDown] = eKeyPageDown;
	io.KeyMap[ImGuiKey_Home] = eKeyHome;
	io.KeyMap[ImGuiKey_End] = eKeyEnd;
	io.KeyMap[ImGuiKey_Insert] = eKeyInsert;
	io.KeyMap[ImGuiKey_Delete] = eKeyDelete;
	io.KeyMap[ImGuiKey_Backspace] = eKeyBackspace;
	io.KeyMap[ImGuiKey_Space] = eKeySpace;
	io.KeyMap[ImGuiKey_Enter] = eKeyEnter;
	io.KeyMap[ImGuiKey_Escape] = eKeyEscape;
	io.KeyMap[ImGuiKey_KeyPadEnter] = eKeyEnter;
	io.KeyMap[ImGuiKey_A] = 'A';
	io.KeyMap[ImGuiKey_C] = 'C';
	io.KeyMap[ImGuiKey_V] = 'V';
	io.KeyMap[ImGuiKey_X] = 'X';
	io.KeyMap[ImGuiKey_Y] = 'Y';
	io.KeyMap[ImGuiKey_Z] = 'Z';

	ImGui::StyleColorsDark();

	//ImGui::GetStyle().FramePadding = ImVec2(4.0f, 2.0f);
	//ImGui::GetStyle().ItemSpacing = ImVec2(8.0f, 2.0f);
	//ImGui::GetStyle().WindowRounding = 2.0f;
	//ImGui::GetStyle().ChildRounding = 2.0f;
	//ImGui::GetStyle().FrameRounding = 2.f;
	//ImGui::GetStyle().ScrollbarRounding = 0.0f;
	//ImGui::GetStyle().GrabRounding = 1.0f;
	//ImGui::GetStyle().WindowBorderSize = 1.0f;
	//ImGui::GetStyle().FrameBorderSize = 1.0f;
	ImGui::GetStyle().ItemSpacing = ImVec2(8.0f, 4.0f);
	ImGui::GetStyle().FramePadding = ImVec2(4.0f, 4.0f);
	ImGui::GetStyle().WindowRounding = 10.0f;
	ImGui::GetStyle().WindowMenuButtonPosition = ImGuiDir_Right;
	//ImGui::GetStyle().FrameRounding = 8.f;

	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text] = ImVec4(0.8f, 0.8f, 0.8f, 1.f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.6f, 0.6f, 0.6f, 1.f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.f, 0.f, 0.f, 1.f);
	colors[ImGuiCol_Border] = ImVec4(0.f, 0.f, 0.f, 1.f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.f, 0.f, 0.f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.f, 0.f, 0.f, 1.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(.0f, .0f, .0f, 1.f);
	colors[ImGuiCol_Header] = ImVec4(.15f, .15f, .15f, 1.f);
	colors[ImGuiCol_PlotLines] = ImVec4(1.f, 1.f, 1.f, 1.f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.56f, 0.56f, 0.56f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);

	mMaterial = make_shared<Material>("ImGui", mNode.component<spirv_module_map>().at("basic_texture_vs"), mNode.component<spirv_module_map>().at("basic_texture_fs"));
	mMaterial->raster_state().setFrontFace(vk::FrontFace::eClockwise);
	mMaterial->depth_stencil().setDepthTestEnable(false);
	mMaterial->depth_stencil().setDepthWriteEnable(false);
	vk::PipelineColorBlendAttachmentState alphaBlend(true,
		vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, 
		vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, 
		vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	mMaterial->blend_states() = { alphaBlend };

	io.Fonts->AddFontFromFileTTF("Assets/Fonts/OpenSans/OpenSans-Regular.ttf", 18.f);
}
ImGuiRenderer::~ImGuiRenderer() {
	ImGui::DestroyContext();
}

void ImGuiRenderer::create_textures(CommandBuffer& commandBuffer) {
	ImGuiIO& io = ImGui::GetIO();

	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	auto staging = make_shared<Buffer>(commandBuffer.mDevice, "ImGuiNode::CreateTextures/Staging", width*height*texel_size(vk::Format::eR8G8B8A8Unorm), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
	memcpy(staging->data(), pixels, staging->size());
	
	auto tex = make_shared<Texture>(commandBuffer.mDevice, "ImGuiRenderer/Texture", vk::Extent3D(width, height, 1), vk::Format::eR8G8B8A8Unorm, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage);
	tex->transition_barrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	commandBuffer->copyBufferToImage(*commandBuffer.hold_resource(staging), **tex, vk::ImageLayout::eTransferDstOptimal,
		{ vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, tex->array_layers()), { 0, 0, 0 }, vk::Extent3D(width, height, 1)) }
	);
	mMaterial->descriptor("gTexture") = sampled_texture_descriptor(move(tex));

	if (!mMaterial->immutable_samplers().count("gSampler"))
		mMaterial->set_immutable_sampler("gSampler", make_shared<Sampler>(commandBuffer.mDevice, "gSampler", vk::SamplerCreateInfo({},
			vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
			0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE)));
}

void ImGuiRenderer::new_frame(const Window& window, float deltaTime) {
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)window.swapchain_extent().width, (float)window.swapchain_extent().height);
	io.DisplayFramebufferScale = ImVec2(1.f, 1.f);

	io.DeltaTime = deltaTime;
	io.MousePos = ImVec2(window.cursor_pos().x(), window.cursor_pos().y());
	io.MouseDown[0] = window.input_state().pressed(eMouse1);
	io.MouseDown[1] = window.input_state().pressed(eMouse2);
	io.MouseDown[2] = window.input_state().pressed(eMouse3);
	io.MouseWheel = window.input_state().scroll_delta();
	io.KeyCtrl = window.input_state().pressed(eKeyControl);
	io.KeyShift = window.input_state().pressed(eKeyShift);
	io.KeyAlt = window.input_state().pressed(eKeyAlt);
	ranges::uninitialized_fill(io.KeysDown, 0);
	for (KeyCode key : window.input_state().buttons())
		io.KeysDown[size_t(key)] = 1;

	ImGui::NewFrame();
}

void ImGuiRenderer::pre_render(CommandBuffer& commandBuffer) {
	mDrawData = ImGui::GetDrawData();

	if (!mDrawData || mDrawData->TotalVtxCount <= 0) return;

	mMaterial->push_constant("gOffset", hlsl::float2(mDrawData->DisplayPos.x, mDrawData->DisplayPos.y));
	mMaterial->push_constant("gTexelSize", hlsl::float2(1/mDrawData->DisplaySize.x, 1/mDrawData->DisplaySize.y));
	mMaterial->transition_images(commandBuffer);

	buffer_vector<ImDrawVert> vertices(commandBuffer.mDevice, mDrawData->TotalVtxCount, vk::BufferUsageFlagBits::eTransferSrc);
	buffer_vector<ImDrawIdx>   indices(commandBuffer.mDevice, mDrawData->TotalIdxCount, vk::BufferUsageFlagBits::eTransferSrc);

	auto dstVertex = vertices.data();
	auto dstIndex  = indices.data();
	for (const ImDrawList* cmdList : span(mDrawData->CmdLists, mDrawData->CmdListsCount)) {
		memcpy(dstVertex, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.size()*sizeof(ImDrawVert));
		memcpy(dstIndex,  cmdList->IdxBuffer.Data, cmdList->IdxBuffer.size()*sizeof(ImDrawIdx));
		dstVertex += cmdList->VtxBuffer.size();
		dstIndex  += cmdList->IdxBuffer.size();
	}

	mVertices = commandBuffer.copy_buffer<ImDrawVert>(vertices, vk::BufferUsageFlagBits::eVertexBuffer);
	mIndices  = commandBuffer.copy_buffer<ImDrawIdx >(indices,  vk::BufferUsageFlagBits::eIndexBuffer);
}

void ImGuiRenderer::draw(CommandBuffer& commandBuffer) {
	if (!mDrawData || mDrawData->CmdListsCount <= 0) return;

	Geometry geom(vk::PrimitiveTopology::eTriangleList, {
		{ Geometry::AttributeType::ePosition, { Geometry::Attribute(mVertices, vk::Format::eR32G32Sfloat,  offsetof(ImDrawVert, pos)) } },
		{ Geometry::AttributeType::eTexcoord, { Geometry::Attribute(mVertices, vk::Format::eR32G32Sfloat,  offsetof(ImDrawVert, uv)) } },
		{ Geometry::AttributeType::eColor,    { Geometry::Attribute(mVertices, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col)) } }
	});

	mMaterial->bind(commandBuffer, geom);
	commandBuffer->setViewport(0, vk::Viewport(mDrawData->DisplayPos.x, mDrawData->DisplayPos.y, mDrawData->DisplaySize.x, mDrawData->DisplaySize.y, 0, 1));
	mMaterial->bind_descriptor_sets(commandBuffer);
	mMaterial->push_constants(commandBuffer);
	
	geom.bind(commandBuffer);
	commandBuffer.bind_index_buffer(mIndices);

	uint32_t voff = 0, ioff = 0;
	for (const ImDrawList* cmdList : span(mDrawData->CmdLists, mDrawData->CmdListsCount)) {
		for (const ImDrawCmd& cmd : cmdList->CmdBuffer)
			if (cmd.UserCallback) {
				// TODO: reset render state callback
				// if (cmd->UserCallback == ResetRenderState)
				cmd.UserCallback(cmdList, &cmd);
			} else {
				vk::Offset2D offset((int32_t)cmd.ClipRect.x, (int32_t)cmd.ClipRect.y);
				vk::Extent2D extent((uint32_t)(cmd.ClipRect.z - cmd.ClipRect.x), (uint32_t)(cmd.ClipRect.w - cmd.ClipRect.y));
				commandBuffer->setScissor(0, vk::Rect2D(offset, extent));
				commandBuffer->drawIndexed(cmd.ElemCount, 1, ioff + cmd.IdxOffset, voff + cmd.VtxOffset, 0);
			}
		voff += cmdList->VtxBuffer.size();
		ioff += cmdList->IdxBuffer.size();
	}
}