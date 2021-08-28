#pragma once

#include "NodeGraph.hpp"
#include <Core/PipelineState.hpp>

#include <imgui.h>

namespace stm {

class Gui {
private:
	Node& mNode;
	component_ptr<PipelineState> mPipeline;
	Mesh mMesh;
	bool mUploadFonts = true;
	ImGuiContext* mContext;
	const ImDrawData* mDrawData;
	
public:
  NodeEvent<CommandBuffer&> OnGui;
	
	STRATUM_API Gui(Node& node);
	STRATUM_API ~Gui();

	inline Node& node() const { return mNode; }
	
	inline void set_context() const {
		ImGui::SetCurrentContext(mContext);
		ImGui::SetAllocatorFunctions(
			[](size_t sz, void* user_data) { return ::operator new(sz); },
			[](void* ptr, void* user_data) { ::operator delete(ptr); });
	}

	STRATUM_API void update(CommandBuffer& commandBuffer, float deltaTime);
	STRATUM_API void render_gui(CommandBuffer& commandBuffer);
	STRATUM_API void draw(CommandBuffer& commandBuffer) const;
};

} 