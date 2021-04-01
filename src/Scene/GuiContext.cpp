#include "GuiContext.hpp"

#include "../Core/SpirvModule.hpp"
#include "../Core/Mesh.hpp"
#include "imgui.h"

using namespace stm;

static inline unordered_map<string, shared_ptr<SpirvModule>> LoadShaders(Device& device, const fs::path& spvm) {
	unordered_map<string, shared_ptr<SpirvModule>> spirvModules;
	unordered_map<string, SpirvModule> tmp;
	byte_stream<ifstream>(spvm, ios::binary) >> tmp;
	ranges::for_each(tmp | views::values, [&](const auto& m){ spirvModules.emplace(m.mEntryPoint, make_shared<SpirvModule>(m)); });
	return spirvModules;
}

GuiContext::GuiContext(CommandBuffer& commandBuffer) {
	Device& device = commandBuffer.mDevice;
	unordered_map<string, shared_ptr<SpirvModule>> moduleGroup = LoadShaders(device, "Assets/core_shaders.stmb");
	
	shared_ptr<Mesh> mMesh = make_shared<Mesh>("UIMesh");

	const stm::RenderPass& renderPass, uint32_t subpassIndex, const GeometryData& geometry,
		shared_ptr<SpirvModule> vs, shared_ptr<SpirvModule> fs,
	mPipeline = make_shared<GraphicsPipeline>("ui", commandBuffer.moduleGroup.at("ui"));

	ImGuiIO& io = ImGui::GetIO();

    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	const vk::Extent3D fontsTexExtent = vk::Extent3D(width, height, 0);
	const size_t uploadSize = width * height * 4 * sizeof(char);
	const byte_blob fontsBlob = byte_blob(span(pixels, uploadSize)); // TODO: no copy
	mFontsTexture = make_shared<Texture>("FontsTexture", device, fontsTexExtent, vk::Format::eR8G8B8A8Unorm, fontsBlob);
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
	mFontsSampler = make_shared<Sampler>(device, "FontsSampler", samplerCreateInfo);

	mMesh->Geometry()[VertexAttributeType::ePosition][0] = GeometryData::Attribute(0, vk::Format::eR32G32B32Sfloat, 0);

	mDescriptorSet = make_shared<DescriptorSet>(mPipeline->DescriptorSetLayouts()[0], string("UIDescriptorSet"));
}

void GuiContext::OnDraw(CommandBuffer& commandBuffer, Camera& camera) {
	const Vector2f screenSize = Vector2f((float)commandBuffer.CurrentFramebuffer()->Extent().width, (float)commandBuffer.CurrentFramebuffer()->Extent().height);

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

		mMesh->Indices() = commandBuffer.CopyToDevice("UIInds", indices, vk::BufferUsageFlagBits::eIndexBuffer);

		mMesh->Geometry().mBindings.resize(1);
		mMesh->Geometry().mBindings[0] = { commandBuffer.CopyToDevice("UIVerts", vertices, vk::BufferUsageFlagBits::eVertexBuffer), vk::VertexInputRate::eVertex };
	}

	// TODO: Combined Image Sampler?
	ui->SetSampledTexture("FontsTexture", mFontsTextureView, 0, vk::ImageLayout::eShaderReadOnlyOptimal);
	ui->SetSampler("FontsSampler", mFontsSampler, 0);
	ui->Bind(commandBuffer);

	DescriptorSet::TextureEntry fontsTextureEntry = DescriptorSet::TextureEntry(nullptr, TextureView(mFontsTexture), vk::ImageLayout::eShaderReadOnlyOptimal);
	DescriptorSet::TextureEntry fontsSamplerEntry = DescriptorSet::TextureEntry(mFontsSampler, TextureView(), vk::ImageLayout::eShaderReadOnlyOptimal);

	commandBuffer.BindDescriptorSet(make_shared<DescriptorSet>(commandBuffer.BoundPipeline()->DescriptorSetLayouts()[0], "tmp", unordered_map<uint32_t, DescriptorSet::Entry> {
		{ pipeline->binding("gFontsSampler").mBinding, fontsTextureEntry },
		{ pipeline->binding("gFontsTexture").mBinding, fontsSamplerEntry }), 0);

	//camera.SetViewportScissor(commandBuffer, StereoEye::eLeft);

	if(drawData->TotalVtxCount > 0) {
		commandBuffer.BindVertexBuffer(0, get<0>(mMesh->Geometry().mBindings[0]));
		commandBuffer.BindIndexBuffer(mMesh->Indices());
	}

	{
		const Vector2f scale = Vector2f(2.f / drawData->DisplaySize.x, 2.f / drawData->DisplaySize.y);
		const Vector2f translate = Vector2f(-1.f - drawData->DisplayPos.x * scale.x(), -1.f - drawData->DisplayPos.y * scale.y());

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

				if (clipRect.x < fbWidth && clipRect.y < fbHeight && clipRect.z >= 0.0f && clipRect.w >= 0.0f) {
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
