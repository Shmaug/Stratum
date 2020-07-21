#pragma once

#include <Content/AssetManager.hpp>
#include <Content/Material.hpp>
#include <Content/Texture.hpp>
#include <Core/Instance.hpp>
#include <Core/DescriptorSet.hpp>
#include <Core/PluginManager.hpp>
#include <Input/InputManager.hpp>
#include <Scene/ObjectBvh2.hpp>
#include <Scene/Camera.hpp>
#include <Scene/GuiContext.hpp>
#include <Scene/Light.hpp>
#include <Scene/Object.hpp>
#include <Util/Util.hpp>

#include <assimp/scene.h>
#include <functional>

class Renderer;

// Holds scene Objects. In general, plugins will add objects during their lifetime, and remove objects during or at the end of their lifetime.
// This makes the shared_ptr destroy when the plugin removes the object, allowing the plugin's module to free the memory.
class Scene {
public:
	ENGINE_EXPORT ~Scene();

	ENGINE_EXPORT void AddObject(std::shared_ptr<Object> object);
	ENGINE_EXPORT void RemoveObject(Object* object);
	
	// Loads a 3d scene from a file, separating all meshes with different topologies/materials into separate MeshRenderers and 
	// replicating the heirarchy stored in the file, and creating new materials using the specified shader.
	// Calls materialSetupFunc for every aiMaterial in the file, to create a corresponding Material
	ENGINE_EXPORT Object* LoadModelScene(const std::string& filename,
		std::function<std::shared_ptr<Material>(Scene*, aiMaterial*)> materialSetupFunc,
		std::function<void(Scene*, Object*, aiMaterial*)> objectSetupFunc,
		float scale, float directionalLightIntensity, float spotLightIntensity, float pointLightIntensity);

	// Render to a camera. This is called automatically on all cameras added to the scene via Scene::AddObject()
	ENGINE_EXPORT void Render(CommandBuffer* commandBuffer, Camera* camera, PassType pass = PASS_MAIN, bool clear = true);
	inline Object* Raycast(const Ray& worldRay, float* t = nullptr, bool any = false, uint32_t mask = 0xFFFFFFFF) { return BVH()->Intersect(worldRay, t, any, mask); }

	ENGINE_EXPORT void SetEnvironmentParameters(Material* material);

	// Setters

	inline void FixedTimeStep(float step) { mFixedTimeStep = step; }
	inline void PhysicsTimeLimitPerFrame(float t) { mPhysicsTimeLimitPerFrame = t; }
	inline void AmbientLight(const float3& t) { mAmbientLight = t; }
	inline void EnvironmentTexture(Texture* t) { mEnvironmentTexture = t; }

	// Getters
	
	inline float FPS() const { return mFps; }
	inline float TotalTime() const { return mTotalTime; }
	inline float DeltaTime() const { return mDeltaTime; }
	inline float FixedTimeStep() const { return mFixedTimeStep; }
	inline float PhysicsTimeLimitPerFrame() const { return mPhysicsTimeLimitPerFrame; }
	inline const std::vector<Light*>& ActiveLights() const { return mActiveLights; }
	inline const std::vector<Camera*>& Cameras() const { return mCameras; }
	inline float3 AmbientLight() const { return mAmbientLight; }
	inline Texture* EnvironmentTexture() const { return mEnvironmentTexture; }
	// Buffer of GPULight structs (defined in shadercompat.h)
	inline Buffer* LightBuffer() const { return mLightBuffer; }
	// Buffer of ShadowData structs (defined in shadercompat.h)
	inline Buffer* ShadowBuffer() const { return mShadowBuffer; }
	// Shadow atlas of multiple shadowmaps
	inline Texture* ShadowAtlas() const { return mShadowAtlas->DepthBuffer(); }

	// All objects, in order of insertion
	ENGINE_EXPORT std::vector<Object*> Objects() const;

	inline ::InputManager* InputManager() const { return mInputManager; }
	inline ::PluginManager* PluginManager() const { return mPluginManager; }
	inline ::Instance* Instance() const { return mInstance; }

	ENGINE_EXPORT ObjectBvh2* BVH();
	// Frame id of the last bvh build
	inline uint64_t LastBvhBuild() { return mLastBvhBuild; }

	inline void BvhDirty(/* unused */ Object* reason) { mBvhDirty = true; }

private:
	friend class Stratum;
	ENGINE_EXPORT void Update(CommandBuffer* commandBuffer);
	ENGINE_EXPORT Scene(::Instance* instance, ::InputManager* inputManager, ::PluginManager* pluginManager);
	
	ENGINE_EXPORT void AddShadowCamera(uint32_t si, ShadowData* sd, bool ortho, float size, const float3& pos, const quaternion& rot, float near, float far);
	ENGINE_EXPORT void Render(CommandBuffer* commandBuffer, Camera* camera, PassType pass, bool clear, std::vector<Renderer*>& renderers);
	ENGINE_EXPORT void RenderShadows(CommandBuffer* commandBuffer, Camera* camera);

	::Instance* mInstance;
	::InputManager* mInputManager;
	::PluginManager* mPluginManager;
	GuiContext* mGuiContext;
	
	std::vector<std::shared_ptr<Object>> mObjects;
	std::vector<Light*> mLights;
	std::vector<Camera*> mCameras;
	std::vector<Renderer*> mRenderers;

	Buffer* mLightBuffer;
	Buffer* mShadowBuffer;
	Framebuffer* mShadowAtlas;

	uint32_t mShadowCount;
	std::vector<Camera*> mShadowCameras;
	std::vector<Light*> mActiveLights;

	Mesh* mSkyboxCube;
	
	Texture* mEnvironmentTexture;
	float3 mAmbientLight;
	Material* mSkyboxMaterial;

	ObjectBvh2* mBvh;
	uint64_t mLastBvhBuild;
	bool mBvhDirty;

	float mFixedAccumulator;
	float mFixedTimeStep;

	// fps calculation
	std::chrono::high_resolution_clock mClock;
	std::chrono::high_resolution_clock::time_point mStartTime;
	std::chrono::high_resolution_clock::time_point mLastFrame;
	float mTotalTime;
	float mDeltaTime;
	float mFrameTimeAccum;
	float mPhysicsTimeLimitPerFrame;
	uint32_t mFpsAccum;
	float mFps;
};