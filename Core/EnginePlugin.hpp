#pragma once

#include <Core/CommandBuffer.hpp>
#include <Util/Util.hpp>

class Scene;
class Camera;
class Instance;

class EnginePlugin {
public:
	bool mEnabled;

	inline virtual ~EnginePlugin() {}

	// Callbacks
	
	
	// Called before vkCreateInstance
	// Use to request any Vulkan instance extensions
	inline virtual void PreInstanceInit(Instance* instance) {};

	// Called before vkCreateDevice
	// Use to request any Vulkan device extensions
	inline virtual void PreDeviceInit(Instance* instance, VkPhysicalDevice physicalDevice) {};

	
	inline virtual bool Init(Scene* scene) { return true; }

	inline virtual void PreUpdate(CommandBuffer* commandBuffer) {}
	inline virtual void FixedUpdate(CommandBuffer* commandBuffer) {}
	inline virtual void Update(CommandBuffer* commandBuffer) {}
	inline virtual void PostUpdate(CommandBuffer* commandBuffer) {}
	
	inline virtual void PreBeginRenderPass(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {}
	inline virtual void PostEndRenderPass(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {}

	inline virtual void PreRender(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {}
	inline virtual void PostRender(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {}

	inline virtual void DrawGUI(CommandBuffer* commandBuffer, Camera* camera) {}

	// Called before the window presents the next swapchain image, after the command buffer(s) are executed
	inline virtual void PrePresent() {}
	
	// Higher priority plugins get called first
	inline virtual int Priority() { return 50; }
};

#define ENGINE_PLUGIN(plugin) extern "C" { PLUGIN_EXPORT EnginePlugin* CreatePlugin() { return new plugin(); } }