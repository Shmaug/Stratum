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
	
	STRATUM_API void create_pipelines();

	inline ImFont* font() const { return mFont; } 
	inline ImFont* title_font() const { return mTitleFont; } 
	
	STRATUM_API void create_font_image(CommandBuffer& commandBuffer);

	STRATUM_API void new_frame(CommandBuffer& commandBuffer, float deltaTime);
	STRATUM_API void make_geometry(CommandBuffer& commandBuffer);
	STRATUM_API void draw(CommandBuffer& commandBuffer);

	inline void set_context() const {
		ImGui::SetCurrentContext(mContext);
		ImGui::SetAllocatorFunctions(
			[](size_t sz, void* user_data) { return ::operator new(sz); },
			[](void* ptr, void* user_data) { ::operator delete(ptr); });
	}
	
	template<typename T>
	inline void register_inspector_gui_fn(void(*fn_ptr)(T*)) {
		mInspectorGuiFns[typeid(T)] = reinterpret_cast<void(*)(void*)>(fn_ptr);
	}
	inline void unregister_inspector_gui_fn(type_index t) {
		mInspectorGuiFns.erase(t);
	}

private:
	Node& mNode;
	component_ptr<DynamicRenderPass> mRenderNode;
	shared_ptr<DynamicRenderPass::Subpass> mRenderPass;
	component_ptr<GraphicsPipelineState> mPipeline;
	unordered_map<Image::View, uint32_t> mImageMap;
	Mesh mMesh;
	bool mUploadFonts = true;
	ImGuiContext* mContext;
	const ImDrawData* mDrawData;
	ImFont* mFont;
	ImFont* mTitleFont;

	unordered_map<type_index, void(*)(void*)> mInspectorGuiFns;

};

} 