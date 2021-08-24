#include "Gui.hpp"
#include <imgui_internal.h>

#include <Core/Window.hpp>

namespace stm {
namespace hlsl {
#include <HLSL/transform.hlsli>
}
}

using namespace stm;
using namespace stm::hlsl;

void setup_imgui() {
	ImGuiIO& io = ImGui::GetIO();
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;       // We can honor GetMouseCursor() values (optional)
	io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;        // We can honor io.WantSetMousePos requests (optional, rarely used)

	io.Fonts->AddFontDefault();
	
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
	io.KeyMap[ImGuiKey_A] = eKeyA;
	io.KeyMap[ImGuiKey_C] = eKeyC;
	io.KeyMap[ImGuiKey_V] = eKeyV;
	io.KeyMap[ImGuiKey_X] = eKeyX;
	io.KeyMap[ImGuiKey_Y] = eKeyY;
	io.KeyMap[ImGuiKey_Z] = eKeyZ;
}

Gui::Gui(Node& node) : mNode(node) {
	mContext = ImGui::CreateContext();

	setup_imgui();

	const spirv_module_map& spirv = *mNode.node_graph().find_components<spirv_module_map>().front();
	const auto& basic_color_texture_fs = spirv.at("basic_color_texture_fs");
	mPipeline = make_shared<PipelineState>("Gui", spirv.at("basic_color_texture_vs"), basic_color_texture_fs);
	mPipeline->raster_state().setFrontFace(vk::FrontFace::eClockwise);
	mPipeline->depth_stencil().setDepthTestEnable(false);
	mPipeline->depth_stencil().setDepthWriteEnable(false);
	mPipeline->blend_states() = { vk::PipelineColorBlendAttachmentState(true,
		vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, 
		vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, 
		vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA) };
	mPipeline->set_immutable_sampler("gSampler", make_shared<Sampler>(basic_color_texture_fs->mDevice, "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE)));

	mGeometry = Geometry(vk::PrimitiveTopology::eTriangleList, {
		{ Geometry::AttributeType::ePosition, { Geometry::Attribute({}, vk::Format::eR32G32Sfloat,  offsetof(ImDrawVert, pos)) } },
		{ Geometry::AttributeType::eTexcoord, { Geometry::Attribute({}, vk::Format::eR32G32Sfloat,  offsetof(ImDrawVert, uv)) } },
		{ Geometry::AttributeType::eColor,    { Geometry::Attribute({}, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col)) } } });
}
Gui::~Gui() {
	ImGui::DestroyContext(mContext);
}

void Gui::create_textures(CommandBuffer& commandBuffer) {
	set_context();
	ImGuiIO& io = ImGui::GetIO();

	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	auto staging = make_shared<Buffer>(commandBuffer.mDevice, "ImGuiNode::CreateTextures/Staging", width*height*texel_size(vk::Format::eR8G8B8A8Unorm), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
	memcpy(staging->data(), pixels, staging->size());
	
	auto tex = make_shared<Texture>(commandBuffer.mDevice, "Gui/Texture", vk::Extent3D(width, height, 1), vk::Format::eR8G8B8A8Unorm, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage);
	tex->transition_barrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	commandBuffer->copyBufferToImage(*commandBuffer.hold_resource(staging), **tex, vk::ImageLayout::eTransferDstOptimal,
		{ vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, tex->array_layers()), { 0, 0, 0 }, vk::Extent3D(width, height, 1)) }
	);
	mPipeline->descriptor("gTextures") = sampled_texture_descriptor(move(tex));
	mPipeline->specialization_constant("gTextureCount", 1);
}

void Gui::new_frame(const Window& window, float deltaTime) const {
	ProfilerRegion ps("ImGui::new_frame");
	
	set_context();

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)window.swapchain_extent().width, (float)window.swapchain_extent().height);
	io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
	io.DeltaTime = deltaTime;
	io.MousePos = ImVec2(window.cursor_pos().x(), window.cursor_pos().y());
	io.MouseWheel = window.input_state().scroll_delta();
	io.MouseDown[0] = window.input_state().pressed(KeyCode::eMouse1);
	io.MouseDown[1] = window.input_state().pressed(KeyCode::eMouse2);
	io.MouseDown[2] = window.input_state().pressed(KeyCode::eMouse3);
	io.MouseDown[3] = window.input_state().pressed(KeyCode::eMouse4);
	io.MouseDown[4] = window.input_state().pressed(KeyCode::eMouse5);
	io.KeyCtrl = window.input_state().pressed(KeyCode::eKeyControl);
	io.KeyShift = window.input_state().pressed(KeyCode::eKeyShift);
	io.KeyAlt = window.input_state().pressed(KeyCode::eKeyAlt);
	ranges::uninitialized_fill(io.KeysDown, 0);
	for (KeyCode key : window.input_state().buttons())
		io.KeysDown[size_t(key)] = 1;
	ImGui::NewFrame();
}

void Gui::render_gui(CommandBuffer& commandBuffer) {
	ProfilerRegion ps("Gui::render_gui");

	OnGui(commandBuffer);

	set_context();
	ImGui::Render();
	mDrawData = ImGui::GetDrawData();
	if (mDrawData && mDrawData->TotalVtxCount) {
		mPipeline->transition_images(commandBuffer);

		buffer_vector<ImDrawVert> vertices(commandBuffer.mDevice, mDrawData->TotalVtxCount, vk::BufferUsageFlagBits::eTransferSrc);
		buffer_vector<ImDrawIdx>  indices (commandBuffer.mDevice, mDrawData->TotalIdxCount, vk::BufferUsageFlagBits::eTransferSrc);
		auto dstVertex = vertices.data();
		auto dstIndex  = indices.data();
		for (const ImDrawList* cmdList : span(mDrawData->CmdLists, mDrawData->CmdListsCount)) {
			memcpy(dstVertex, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.size()*sizeof(ImDrawVert));
			memcpy(dstIndex,  cmdList->IdxBuffer.Data, cmdList->IdxBuffer.size()*sizeof(ImDrawIdx));
			dstVertex += cmdList->VtxBuffer.size();
			dstIndex  += cmdList->IdxBuffer.size();
		}
		auto vertexBuffer = commandBuffer.copy_buffer(vertices, vk::BufferUsageFlagBits::eVertexBuffer);
		mGeometry[Geometry::AttributeType::ePosition][0] = vertexBuffer;
		mGeometry[Geometry::AttributeType::eTexcoord][0] = vertexBuffer;
		mGeometry[Geometry::AttributeType::eColor][0] = vertexBuffer;
		mIndices = commandBuffer.copy_buffer(indices,  vk::BufferUsageFlagBits::eIndexBuffer);
	}
}

void Gui::draw(CommandBuffer& commandBuffer) const {
	if (!mDrawData || mDrawData->CmdListsCount <= 0) return;

	ProfilerRegion ps("Gui::draw", commandBuffer);

	mPipeline->push_constant("gWorldToCamera", TransformData(float3(mDrawData->DisplayPos.x, mDrawData->DisplayPos.y, 0), 1, make_quatf(0,0,0,1)));
	mPipeline->push_constant("gProjection", ProjectionData(float3(2/mDrawData->DisplaySize.x, 2/mDrawData->DisplaySize.y, 1), 0, float3(-1,-1, 1)));
	mPipeline->push_constant<float4>("gTextureST", float4(1,1,0,0));
	mPipeline->push_constant<float4>("gColor", Array4f::Ones());

	mPipeline->bind_pipeline(commandBuffer, mGeometry);
	mPipeline->bind_descriptor_sets(commandBuffer);
	mPipeline->push_constants(commandBuffer);
	
	commandBuffer->setViewport(0, vk::Viewport(mDrawData->DisplayPos.x, mDrawData->DisplayPos.y, mDrawData->DisplaySize.x, mDrawData->DisplaySize.y, 0, 1));
	
	mGeometry.bind(commandBuffer);
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