#pragma once
#include <Core/PipelineState.hpp>
#include "DynamicRenderPass.hpp"

#include <imgui.h>

namespace stm {

class Gui {
	
public:	
	STRATUM_API Gui(Node& node);
	STRATUM_API ~Gui();

	inline Node& node() const { return mNode; }
	inline const auto& render_pass() const { return mRenderPass; }
	
	inline void set_context() const {
		ImGui::SetCurrentContext(mContext);
		ImGui::SetAllocatorFunctions(
			[](size_t sz, void* user_data) { return ::operator new(sz); },
			[](void* ptr, void* user_data) { ::operator delete(ptr); });
	}
	
private:
	Node& mNode;
	component_ptr<DynamicRenderPass> mRenderNode;
	shared_ptr<DynamicRenderPass::Subpass> mRenderPass;
	component_ptr<GraphicsPipelineState> mPipeline;
	Mesh mMesh;
	bool mUploadFonts = true;
	ImGuiContext* mContext;
	const ImDrawData* mDrawData;
};

} 