#pragma once

#include <Core/CommandBuffer.hpp>
#include <Core/DescriptorSet.hpp>

namespace stm {

class EnginePlugin {
public:
	// Higher priority plugins get called first
	inline virtual int Priority() { return 50; }
	
protected:
	friend class Instance;
	friend class PluginManager;
	friend class Scene;

	inline virtual std::set<std::string> InstanceExtensionsRequired() { return {}; };
	inline virtual std::set<std::string> DeviceExtensionsRequired(vk::PhysicalDevice device) { return {}; };

	inline virtual bool OnSceneInit(Scene* scene) { return true; }
	
	inline virtual void OnPreUpdate(CommandBuffer& commandBuffer) {}
	inline virtual void OnFixedUpdate(CommandBuffer& commandBuffer) {}
	inline virtual void OnUpdate(CommandBuffer& commandBuffer) {}
	inline virtual void OnLateUpdate(CommandBuffer& commandBuffer) {}
	// Called before the Scene begins rendering a frame
	inline virtual void OnPreRender(CommandBuffer& commandBuffer) {}
	// Called before the Scene begins rendering a frame, used to easily queue GUI drawing operations
	inline virtual void OnGui(CommandBuffer& commandBuffer, Camera& camera, GuiContext& gui) {}
	// Called during a Subpass for each camera that renders to an attachment that the subpass outputs
	inline virtual void OnRenderCamera(CommandBuffer& commandBuffer, Camera& camera, std::shared_ptr<DescriptorSet> perCamera) {}
	// Called after the Scene ends a full RenderPass
	inline virtual void OnPostProcess(CommandBuffer& commandBuffer, std::shared_ptr<Framebuffer> framebuffer, const std::set<Camera*>& cameras) {}

	// Called before the window presents the next swapchain image, after the command buffer(s) are executed
	inline virtual void PrePresent() {}
};

}

#define CREATE_PLUGIN_FUNCTION Stratum_CreatePlugin
#define ENGINE_PLUGIN(plugin_t) extern "C" { PLUGIN_EXPORT stm::EnginePlugin* CREATE_PLUGIN_FUNCTION() { return new plugin_t(); } }