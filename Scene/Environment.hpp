#pragma once

#include <Content/Material.hpp>
#include <Content/Texture.hpp>
#include <Scene/Object.hpp>
#include <Scene/Light.hpp>
#include <Util/Util.hpp>

class Scene;

class Environment {
public:
	ENGINE_EXPORT Environment(Scene* scene);

	inline float3 AmbientLight() const { return mAmbientLight; }
	inline void AmbientLight(const float3& t) { mAmbientLight = t; }

	inline Texture* EnvironmentTexture() const { return mEnvironmentTexture; }
	inline void EnvironmentTexture(Texture* t) { mEnvironmentTexture = t; }

	ENGINE_EXPORT void SetEnvironment(Camera* camera, Material* material);
	
private:
	friend class Scene;
	ENGINE_EXPORT void PreRender(CommandBuffer* commandBuffer, Camera* camera);

	Texture* mEnvironmentTexture;

	float3 mAmbientLight;

    Scene* mScene;

	Shader* mShader;
	std::shared_ptr<Material> mSkyboxMaterial;
};