#include "ImGuiNode.hpp"

#include <imgui/imgui.cpp>
#include <imgui/imgui_demo.cpp>
#include <imgui/imgui_draw.cpp>
#include <imgui/imgui_tables.cpp>
#include <imgui/imgui_widgets.cpp>

using namespace stm;

ImGuiNode::ImGuiNode(Scene& scene, const string& name, CommandBuffer& commandBuffer, shared_ptr<SpirvModule> fs_texture) : Scene::Node(scene, name) {
	mMaterial = make_shared<Material>("ImGui", fs_texture);

	ImGuiIO& io = ImGui::GetIO();

	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	mFonts = TextureView(make_shared<Texture>(commandBuffer.mDevice, "FontsTexture", vk::Extent3D(width, height, 0), vk::Format::eR8G8B8A8Unorm));
	mFonts.texture().TransitionBarrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	commandBuffer->copyBufferToImage(commandBuffer.CreateStagingBuffer(span(pixels, width*height*4)), *mFonts.texture(), vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy() });
}

void ImGuiNode::Render(CommandBuffer& commandBuffer) {

	ImGui::Render();
	const ImDrawData* drawData = ImGui::GetDrawData();

	GeometryData geometry;
	Buffer::RangeView indexBuffer;

	if (drawData->TotalVtxCount > 0) {
		const size_t vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
		const size_t indexSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

		vector<ImDrawVert> vertices;
		vertices.reserve(vertexSize);
		vector<ImDrawIdx> indices;
		indices.reserve(indexSize);

		vk::DeviceSize vertexOffset = 0;
		vk::DeviceSize indexOffset = 0;
		for (std::size_t drawListIdx = 0; drawListIdx < drawData->CmdListsCount; drawListIdx++) {
			const ImDrawList* cmdList = drawData->CmdLists[drawListIdx];
			ranges::copy(cmdList->VtxBuffer, end(vertices));
			ranges::copy(cmdList->IdxBuffer, end(indices));
		}

		indexBuffer = commandBuffer.CopyToDevice("UIInds", indices, vk::BufferUsageFlagBits::eIndexBuffer);

		geometry[VertexAttributeType::ePosition][0] = GeometryData::Attribute(0, vk::Format::eR32G32B32Sfloat, 0);
		geometry.mBindings.resize(1);
		geometry.mBindings[0] = { commandBuffer.CopyToDevice("UIVerts", vertices, vk::BufferUsageFlagBits::eVertexBuffer), vk::VertexInputRate::eVertex };
	}

	mMaterial->Bind(commandBuffer, geometry);
	if (indexBuffer) commandBuffer.BindIndexBuffer(indexBuffer);

	commandBuffer.PushConstant<Vector2f>("Scale", Vector2f::Constant(2)/Map<Vector2f>((float*)&drawData->DisplaySize));
	commandBuffer.PushConstant<Vector2f>("Translate", -Vector2f::Ones() - Map<Vector2f>(&drawData->DisplayPos) * Map<Vector2f>((float*)&drawData->DisplaySize));

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
				Vector4f clipRect;
				clipRect.x() = (cmd->ClipRect.x - clipOffset.x) * clipScale.x;
				clipRect.y() = (cmd->ClipRect.y - clipOffset.y) * clipScale.y;
				clipRect.z() = (cmd->ClipRect.z - clipOffset.x) * clipScale.x;
				clipRect.w() = (cmd->ClipRect.w - clipOffset.y) * clipScale.y;
				commandBuffer.PushConstant<Vector4f>("ClipRect", clipRect)
				commandBuffer->drawIndexed(cmd->ElemCount, 1, cmd->IdxOffset + globalIdxOffset, cmd->VtxOffset + globalVtxOffset, 0);
			}
		}

		globalVtxOffset += cmdList->VtxBuffer.Size;
		globalIdxOffset += cmdList->IdxBuffer.Size;
	}
}