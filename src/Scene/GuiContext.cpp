#include "GuiContext.hpp"

#include "Camera.hpp"
#include "imgui.h"

using namespace stm;

GuiContext::GuiContext(stm::Scene& scene) : mScene(scene) {
	mIconsTexture = mScene.mInstance.Device().FindOrLoadAsset<Texture>("Assets/Textures/icons.png");

	SpirvModuleGroup uiSpirv(scene.mInstance.Device(), "Assets/Shaders/ui.stmb");
	
	mMaterials["ui"] = make_shared<Material>("GuiContext/UI", uiSpirv);
	//mMaterials["font_ss"] = make_shared<MaterialDerivative>("GuiContext/UI", mMaterials["ui"], { "gScreenSpace", true });
	//mMaterials["ui_ss"]->SetSpecialization("gScreenSpace", true);
	//mMaterials["lines_ss"]->SetSpecialization("gScreenSpace", true);

	ImGuiIO& io = ImGui::GetIO();

    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	const vk::Extent3D fontsTexExtent = vk::Extent3D(width, height, 0);
	const size_t uploadSize = width * height * 4 * sizeof(char);
	const byte_blob fontsBlob = byte_blob(pixels | ranges::views::take(uploadSize)); // TODO: no copy
	mFontsTexture = make_shared<Texture>("FontsTexture", scene.mInstance.Device(), fontsTexExtent, vk::Format::eR8G8B8A8Unorm, fontsBlob);
	mFontsTextureView = TextureView(mFontsTexture);

	vk::SamplerCreateInfo samplerCreateInfo = vk::SamplerCreateInfo()
    	.setMagFilter(vk::Filter::eLinear)
    	.setMinFilter(vk::Filter::eLinear)
    	.setMipmapMode(vk::SamplerMipmapMode::eLinear)
    	.setAddressModeU(vk::SamplerAddressMode::eRepeat)
    	.setAddressModeV(vk::SamplerAddressMode::eRepeat)
    	.setAddressModeW(vk::SamplerAddressMode::eRepeat)
    	.setMinLod(-1000)
    	.setMaxLod(1000)
    	.setMaxAnisotropy(1.0f);
	mFontsSampler = make_shared<Sampler>(scene.mInstance.Device(), "FontsSampler", samplerCreateInfo);
}

void GuiContext::OnDraw(CommandBuffer& commandBuffer, Camera& camera) {
	const Vector2f screenSize = Vector2f((float)commandBuffer.CurrentFramebuffer()->Extent().width, (float)commandBuffer.CurrentFramebuffer()->Extent().height);

	auto ui = mMaterials["ui"];

	ImGui::Render();
	const ImDrawData* drawData = ImGui::GetDrawData();
	const bool isMinimized = (drawData->DisplaySize.x <= 0.f || drawData->DisplaySize.y <= 0.f);
	if (isMinimized) { // TODO: really needed? shouldn't even call this method
		return;
	}

	const float fbWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
    const float fbHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;
    if (fbWidth <= 0.f || fbHeight <= 0.f) {
        return;
	}

	if (drawData->TotalVtxCount > 0) {
		const size_t vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
		const size_t indexSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

		// TODO: staging buffer?
		auto deviceVertexBuffer = commandBuffer.GetBuffer("GUIVerts", vertexSize, vk::BufferUsageFlagBits::eVertexBuffer, 
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		auto deviceIndexBuffer = commandBuffer.GetBuffer("GUIInds", indexSize, vk::BufferUsageFlagBits::eIndexBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		vk::DeviceSize vertexOffset = 0;
		vk::DeviceSize indexOffset = 0;
		for (std::size_t drawListIdx = 0; drawListIdx < drawData->CmdListsCount; drawListIdx++) {
			const ImDrawList* cmdList = drawData->CmdLists[drawListIdx];
			const size_t subVtxBufSz = cmdList->VtxBuffer.Size * sizeof(ImDrawVert);
			const size_t subIdxBufSz = cmdList->IdxBuffer.Size * sizeof(ImDrawIdx);

			auto dVtxBufView = Buffer::ArrayView(deviceVertexBuffer, vertexOffset);
			auto dIdxBufView = Buffer::ArrayView(deviceVertexBuffer, indexOffset);

			ranges::copy(cmdList->VtxBuffer.Data | ranges::views::take(subVtxBufSz), dVtxBufView);
			ranges::copy(cmdList->IdxBuffer.Data | ranges::views::take(subIdxBufSz), dIdxBufView);
		}
	}

	// TODO: Combined Image Sampler?
	ui->SetSampledTexture("FontsTexture", mFontsTextureView, 0, vk::ImageLayout::eShaderReadOnlyOptimal);
	ui->SetSampler("FontsSampler", mFontsSampler, 0);
	ui->Bind(commandBuffer);

	if(drawData->TotalVtxCount > 0) {
		auto dVtxBufView = Buffer::ArrayView(deviceVertexBuffer);
		auto dIdxBufView = Buffer::ArrayView(deviceVertexBuffer);
		commandBuffer.BindVertexBuffer(dVtxBufView);
		commandBuffer.BindIndexBuffer(dIdxBufView);
	}

	camera.SetViewportScissor(commandBuffer, StereoEye::eLeft);

	{
		const vec2_t scale = vec2_t(2.f / drawData->DisplaySize.x, 2.f / drawData->DisplaySize.y);
		const vec2_t translate = vec2-t(-1.f - drawData->DisplayPos.x * scale.x, -1.f - drawData->DisplayPos.y * scale.y);

		commandBuffer.PushConstantRef("Scale", scale);
		commandBuffer.PushConstantRef("Translate", translate);
	}

	const ImVec2 clipOffset = drawData->DisplayPos; // (0,0) unless using multi-viewports
	const ImVec2 clipScale = drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

	vk::DeviceSize globalVtxOffset = 0;
	vk::DeviceSize globalIdxOffset = 0;
	for (size_t drawListIdx = 0; drawListIdx < drawData->CmdListsCount; drawListIdx++) {
		const ImDrawList* cmdList = drawData->CmdLists[drawListIdx];
		for (size_t cmdIdx = 0; cmdIdx < cmdList->CmdBuffer.Size; cmdIdx++) {
			const ImDrawCmd* cmd = &cmdList->CmdBuffer[cmdIdx];
			if (cmd->UserCallback != nullptr) {
				// TODO: reset render state callback
				// if (cmd->UserCallback == ResetRenderState)
				cmd->UserCallback(cmdList, cmd);
			}
			else {
				ImVec4 clipRect;
                clipRect.x = (cmd->ClipRect.x - clipOffset.x) * clipScale.x;
                clipRect.y = (cmd->ClipRect.y - clipOffset.y) * clipScale.y;
                clipRect.z = (cmd->ClipRect.z - clipOffset.x) * clipScale.x;
                clipRect.w = (cmd->ClipRect.w - clipOffset.y) * clipScale.y;

				if (clipRect.x < fbWidth && clipRect.y < fbWeight && clipRect.z >= 0.0f && clipRect.w >= 0.0f) {
                    // Negative offsets are illegal for vkCmdSetScissor
                    if (clipRect.x < 0.0f) {
                        clipRect.x = 0.0f;
					}
                    if (clipRect.y < 0.0f) {
                        clipRect.y = 0.0f;
					}

                    // Apply scissor/clipping rectangle
					// TODO: does pipeline have VK_DYNAMIC_STATE_SCISSOR?
                    vk::Rect2D scissor;
                    scissor.offset.x = (int32_t)(clipRect.x);
                    scissor.offset.y = (int32_t)(clipRect.y);
                    scissor.extent.width = (uint32_t)(clipRect.z - clipRect.x);
                    scissor.extent.height = (uint32_t)(clipRect.w - clipRect.y);
                    commandBuffer->setScissor(0, 1, &scissor);

                    // Draw
                    commandBuffer->drawIndexed(cmd->ElemCount, 1, cmd->IdxOffset + globalIdxOffset, 
						cmd->VtxOffset + globalVtxOffset, 0);
                }
			}
		}

		globalVtxOffset += cmdList->VtxBuffer.Size;
		globalIdxOffset += cmdList->IdxBuffer.Size;
	}
}
