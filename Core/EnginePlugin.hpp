#pragma once

#include <Core/CommandBuffer.hpp>
#include <Core/DescriptorSet.hpp>

class EnginePlugin {
public:
	inline virtual ~EnginePlugin() {}
	// Higher priority plugins get called first
	inline virtual int Priority() { return 50; }
	
protected:
	friend class Instance;
	friend class PluginManager;
	friend class Scene;

	inline virtual std::set<std::string> InstanceExtensionsRequired() { return {}; };
	inline virtual std::set<std::string> DeviceExtensionsRequired(vk::PhysicalDevice device) { return {}; };

	inline virtual bool OnSceneInit(Scene* scene) { return true; }
	
	inline virtual void OnPreUpdate(stm_ptr<CommandBuffer> commandBuffer) {}
	inline virtual void OnFixedUpdate(stm_ptr<CommandBuffer> commandBuffer) {}
	inline virtual void OnUpdate(stm_ptr<CommandBuffer> commandBuffer) {}
	inline virtual void OnLateUpdate(stm_ptr<CommandBuffer> commandBuffer) {}
	// Called before the Scene begins rendering a frame
	inline virtual void OnPreRender(stm_ptr<CommandBuffer> commandBuffer) {}
	// Called before the Scene begins rendering a frame, used to easily queue GUI drawing operations
	inline virtual void OnGui(stm_ptr<CommandBuffer> commandBuffer, Camera* camera, GuiContext* gui) {}
	// Called during a Subpass for each camera that renders to an attachment that the subpass outputs
	inline virtual void OnRenderCamera(stm_ptr<CommandBuffer> commandBuffer, Camera* camera, stm_ptr<DescriptorSet> perCamera) {}
	// Called after the Scene ends a full RenderPass
	inline virtual void OnPostProcess(stm_ptr<CommandBuffer> commandBuffer, Framebuffer* framebuffer, const std::set<Camera*>& cameras) {}

	// Called before the window presents the next swapchain image, after the command buffer(s) are executed
	inline virtual void PrePresent() {}
};

#define ENGINE_PLUGIN(plugin) extern "C" { PLUGIN_EXPORT EnginePlugin* CreatePlugin() { return new plugin(); } }