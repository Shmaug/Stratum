#include "ImGuiNode.hpp"

#include <imgui/imgui.cpp>
#include <imgui/imgui_demo.cpp>
#include <imgui/imgui_draw.cpp>
#include <imgui/imgui_tables.cpp>
#include <imgui/imgui_widgets.cpp>

#include "../Core/Window.hpp"

using namespace stm;

PFN_vkCmdSetDepthTestEnableEXT fpCmdSetDepthTestEnableEXT_g;
PFN_vkCmdSetFrontFaceEXT fpCmdSetFrontFaceEXT_g;

void vkCmdSetDepthTestEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthTestEnable)
{
	fpCmdSetDepthTestEnableEXT_g(commandBuffer, depthTestEnable);
}

void vkCmdSetFrontFaceEXT(
    VkCommandBuffer                             commandBuffer,
    VkFrontFace                                 frontFace)
{
	fpCmdSetFrontFaceEXT_g(commandBuffer, frontFace);
}

ImGuiNode::ImGuiNode(const string& name, NodeGraph& nodeGraph, Device& device, shared_ptr<SpirvModule> vs_texture, shared_ptr<SpirvModule> fs_texture) {
	vk::PipelineColorBlendAttachmentState blendOpaque;
	blendOpaque.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	mRenderNode = &nodeGraph.emplace("mainpass").emplace<RenderNode>();
	(*mRenderNode)[""].bind_point(vk::PipelineBindPoint::eGraphics);
	(*mRenderNode)[""]["primaryColor"] = RenderPass::SubpassDescription::AttachmentInfo(
		RenderPass::AttachmentTypeFlags::eColor, blendOpaque, vk::AttachmentDescription({},
			device.mInstance.window().surface_format().format, vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal
		)
	);
	
	mMaterial = make_shared<Material>("ImGui", vs_texture, fs_texture);

	// TODO: run NewFrame before any imgui rendering calls are done, 
	// then PreRender before any actual rendering is done to upload vertex buffer, then Render
	mRenderNode->PreRender.emplace(mRenderNode->node(), bind_front(&ImGuiNode::PreRender, this));
	mRenderNode->OnRender.emplace(mRenderNode->node(), bind_front(&ImGuiNode::Render, this));

	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.BackendRendererName = "imgui_impl_vulkan";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.

	ImGui::StyleColorsDark();

    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)
    io.BackendPlatformName = "imgui_impl_win32";

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

	ImVec4* colors = ImGui::GetStyle().Colors;
	/*ImGui::GetStyle().FramePadding = ImVec2(4.0f, 2.0f);
	ImGui::GetStyle().ItemSpacing = ImVec2(8.0f, 2.0f);
	ImGui::GetStyle().WindowRounding = 2.0f;
	ImGui::GetStyle().ChildRounding = 2.0f;
	ImGui::GetStyle().FrameRounding = 2.f;
	ImGui::GetStyle().ScrollbarRounding = 0.0f;
	ImGui::GetStyle().GrabRounding = 1.0f;
	ImGui::GetStyle().WindowBorderSize = 1.0f;
	ImGui::GetStyle().FrameBorderSize = 1.0f;*/
	ImGui::GetStyle().ItemSpacing = ImVec2(8.0f, 4.0f);
	ImGui::GetStyle().FramePadding = ImVec2(4.0f, 4.0f);
	ImGui::GetStyle().WindowRounding = 10.0f;
	ImGui::GetStyle().WindowMenuButtonPosition = ImGuiDir_Right;
	//ImGui::GetStyle().FrameRounding = 8.f;
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

	io.Fonts->AddFontFromFileTTF("extern/src/imgui/misc/fonts/Karla-Regular.ttf", 18.f);

	// TODO: remove dynamic state stuff
	fpCmdSetDepthTestEnableEXT_g = (PFN_vkCmdSetDepthTestEnableEXT)device.mInstance->getProcAddr("vkCmdSetDepthTestEnableEXT");
	fpCmdSetFrontFaceEXT_g = (PFN_vkCmdSetFrontFaceEXT)device.mInstance->getProcAddr("vkCmdSetFrontFaceEXT");

}

void ImGuiNode::LoadFonts(CommandBuffer& commandBuffer)
{
	ImGuiIO& io = ImGui::GetIO();

	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	// Fonts staging
	vk::Format fontsFormat = vk::Format::eR8G8B8A8Unorm;
	Buffer::View<byte> fontsStaging(
		make_shared<Buffer>(commandBuffer.mDevice, "UIFonts/Staging", width * height * texel_size(fontsFormat),
			vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eUniformTexelBuffer | vk::BufferUsageFlagBits::eStorageTexelBuffer,
			VMA_MEMORY_USAGE_CPU_TO_GPU
			)
	);
	memcpy(fontsStaging.data(), pixels, fontsStaging.size());
	Buffer::TexelView fontsStagingView(fontsStaging, fontsFormat);

	auto fonts = Texture::View(
		make_shared<Texture>(commandBuffer.mDevice, "FontsTexture", vk::Extent3D(width, height, 1), fontsFormat, 1, 1, vk::SampleCountFlagBits::e1,
			vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage
			)
	);
	fonts.texture().transition_barrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	commandBuffer->copyBufferToImage(*commandBuffer.hold_resource(fontsStagingView.buffer_ptr()), *fonts.texture(), vk::ImageLayout::eTransferDstOptimal,
		//{ vk::BufferImageCopy() }
		{ vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, fonts.texture().array_layers()), {0, 0, 0}, vk::Extent3D(width, height, 1)) }
	);

	vk::SamplerCreateInfo samplerInfo;
	samplerInfo.magFilter = vk::Filter::eLinear;
	samplerInfo.minFilter = vk::Filter::eLinear;
	samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
	samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
	samplerInfo.minLod = -1000;
	samplerInfo.maxLod = 1000;
	samplerInfo.maxAnisotropy = 1.0f;
	auto basicSampler = make_shared<Sampler>(commandBuffer.mDevice, "gFontsSampler", samplerInfo);

	mMaterial->set_immutable_sampler("gFontsSampler", basicSampler);

	mMaterial->descriptor("gFontsTexture") = sampled_texture_descriptor(move(fonts));
}

void ImGuiNode::NewFrame(const Window& window)
{
	const float width = float(window.swapchain_extent().width), height = float(window.swapchain_extent().height);
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(1.f, 1.f);

    // Setup time step
    //double current_time = glfwGetTime();
    io.DeltaTime = 1.f / 60.f; //g_Time > 0.0 ? (float)(current_time - g_Time) : (float)(1.0f / 60.0f);
    //g_Time = current_time;

	io.ImeWindowHandle = window.handle();
	io.MousePos = ImVec2(window.cursor_pos().x(), window.cursor_pos().y());
	io.MouseDown[0] = window.input_state().pressed(eMouse1);
	io.MouseDown[1] = window.input_state().pressed(eMouse2);
	io.MouseDown[2] = window.input_state().pressed(eMouse3);
	io.MouseWheel = window.input_state().scroll_delta();

	memset(io.KeysDown, 0, sizeof(io.KeysDown));
	for (KeyCode key : window.input_state().buttons()) {
		io.KeysDown[size_t(key)] = 1;
	}

	ImGui::NewFrame();
}

void ImGuiNode::PreRender(CommandBuffer& commandBuffer) {
	ImGui::Render();

	const ImDrawData* drawData = ImGui::GetDrawData();
	if(!drawData)
	{
		cout << "bad drawdata\n";
		return;
	}

	if (drawData->TotalVtxCount > 0) {
		const size_t vertexSize = drawData->TotalVtxCount;
		const size_t indexSize = drawData->TotalIdxCount;

		buffer_vector<ImDrawVert> vertices(commandBuffer.mDevice, 0, vk::BufferUsageFlagBits::eTransferSrc);
		vertices.resize(vertexSize);
		buffer_vector<ImDrawIdx> indices(commandBuffer.mDevice, 0, vk::BufferUsageFlagBits::eTransferSrc);
		indices.resize(indexSize);

		vk::DeviceSize vertexOffset = 0;
		vk::DeviceSize indexOffset = 0;
		for (std::size_t drawListIdx = 0; drawListIdx < drawData->CmdListsCount; drawListIdx++) {
			const ImDrawList* cmdList = drawData->CmdLists[drawListIdx];
			ranges::copy_n(cmdList->VtxBuffer.Data, cmdList->VtxBuffer.size(), &vertices[vertexOffset]);
			ranges::copy_n(cmdList->IdxBuffer.Data, cmdList->IdxBuffer.size(), &indices[indexOffset]);

			vertexOffset += cmdList->VtxBuffer.size();
			indexOffset += cmdList->IdxBuffer.size();
		}

		mIndexBuffer = commandBuffer.copy_buffer<ImDrawIdx>(indices, vk::BufferUsageFlagBits::eIndexBuffer);
		mVertexBuffer = commandBuffer.copy_buffer<ImDrawVert>(vertices, vk::BufferUsageFlagBits::eVertexBuffer);

		Geometry::attribute_map_t attributeMap = {
			{ Geometry::AttributeType::ePosition, { Geometry::Attribute(mVertexBuffer, vk::Format::eR32G32B32Sfloat, offsetof(ImDrawVert, pos)) } },
			{ Geometry::AttributeType::eTexcoord, { Geometry::Attribute(mVertexBuffer, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, uv)) } },
			{ Geometry::AttributeType::eColor, { Geometry::Attribute(mVertexBuffer, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col)) } }
		};

		mGeometry = Geometry(vk::PrimitiveTopology::eTriangleList, attributeMap);
		//mGeometry[Geometry::AttributeType::ePosition][0] = Geometry::Attribute(vertexBuffer, vk::Format::eR32G32B32Sfloat, offsetof(ImDrawVert, pos));
		//mGeometry[Geometry::AttributeType::eTexcoord][0] = Geometry::Attribute(0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, uv));
		//mGeometry[Geometry::AttributeType::eColor][0] = Geometry::Attribute(0, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col));
		//mGeometry.mBindings.resize(1);
		//mGeometry.mBindings[0] = { commandBuffer.copy_buffer<ImDrawVert>(vertices, vk::BufferUsageFlagBits::eVertexBuffer), vk::VertexInputRate::eVertex };
	}

	mMaterial->transition_images(commandBuffer);
}

void ImGuiNode::Render(CommandBuffer& commandBuffer) {
	const ImDrawData* drawData = ImGui::GetDrawData();
	if(!drawData)
	{
		cout << "bad drawdata\n";
		return;
	}

	const bool isMinimized = (drawData->DisplaySize.x <= 0.f || drawData->DisplaySize.y <= 0.f);
	if (isMinimized) { // TODO: really needed? shouldn't even call this method
		return;
	}

	commandBuffer->setDepthTestEnableEXT(false);
	commandBuffer->setFrontFaceEXT(vk::FrontFace::eClockwise);
	//commandBuffer->setDepthWriteEnableEXT(false);

	mMaterial->bind(commandBuffer, mGeometry);

	commandBuffer.bind_vertex_buffer(0, mVertexBuffer);
	if (mIndexBuffer) commandBuffer.bind_index_buffer(mIndexBuffer);

	commandBuffer.push_constant<ImVec2>("Scale", ImVec2(2.f, -2.f) / drawData->DisplaySize);
	commandBuffer.push_constant<ImVec2>("Translate", ImVec2(-1.f, 1.f)*ImVec2(1.f, 1.f) - drawData->DisplayPos * drawData->DisplaySize);

	const ImVec2 clipOffset = drawData->DisplayPos; // (0,0) unless using multi-viewports
	const ImVec2 clipScale = drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

	vk::DeviceSize globalVtxOffset = 0;
	vk::DeviceSize globalIdxOffset = 0;
	for (int drawListIdx = 0; drawListIdx < drawData->CmdListsCount; drawListIdx++) {
		const ImDrawList* cmdList = drawData->CmdLists[drawListIdx];
		for (int cmdIdx = 0; cmdIdx < cmdList->CmdBuffer.Size; cmdIdx++) {
			const ImDrawCmd* cmd = &cmdList->CmdBuffer[cmdIdx];
			if (cmd->UserCallback != nullptr) {
				// TODO: reset render state callback
				// if (cmd->UserCallback == ResetRenderState)
				cmd->UserCallback(cmdList, cmd);
			} else {
				ImVec4 clipRect;
				clipRect.x = (cmd->ClipRect.x - clipOffset.x) * clipScale.x;
				clipRect.y = (cmd->ClipRect.y - clipOffset.y) * clipScale.y;
				clipRect.z = (cmd->ClipRect.z - clipOffset.x) * clipScale.x;
				clipRect.w = (cmd->ClipRect.w - clipOffset.y) * clipScale.y;
				//commandBuffer.push_constant<ImVec4>("ClipRect", clipRect);
				commandBuffer->drawIndexed(cmd->ElemCount, 1, cmd->IdxOffset + globalIdxOffset, cmd->VtxOffset + globalVtxOffset, 0);
			}
		}

		globalVtxOffset += cmdList->VtxBuffer.Size;
		globalIdxOffset += cmdList->IdxBuffer.Size;
	}

	commandBuffer->setDepthTestEnableEXT(true);
	commandBuffer->setFrontFaceEXT(vk::FrontFace::eCounterClockwise);
}