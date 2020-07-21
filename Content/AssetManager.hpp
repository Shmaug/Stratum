#pragma once

#include <Core/Device.hpp>
#include <Util/Util.hpp>
#include <Content/Asset.hpp>

class Font;
class Mesh;
class Shader;
class Texture;
class Instance;

class AssetManager {
public:
	ENGINE_EXPORT ~AssetManager();

	ENGINE_EXPORT Shader*	LoadShader(const std::string& filename);
	ENGINE_EXPORT Texture*	LoadTexture(const std::string& filename, bool srgb = true);
	ENGINE_EXPORT Texture*  LoadCubemap(const std::string& posx, const std::string& negx, const std::string& posy, const std::string& negy, const std::string& posz, const std::string& negz, bool srgb = true);
	ENGINE_EXPORT Mesh*		LoadMesh(const std::string& filename, float scale = 1.f);
	ENGINE_EXPORT Font*		LoadFont(const std::string& filename);

	inline Texture* WhiteTexture() const { return mWhiteTexture; }
	inline Texture* TransparentBlackTexture() const { return mTransparentBlackTexture; }
	inline Texture* BlackTexture() const { return mBlackTexture; }
	inline Texture* BumpTexture() const { return mBumpTexture; }
	inline Texture* NoiseTexture() const { return mNoiseTexture; }

private:
	friend class Device;
	ENGINE_EXPORT AssetManager(Device* device);

	Texture* mWhiteTexture;
	Texture* mBlackTexture;
	Texture* mTransparentBlackTexture;
	Texture* mBumpTexture;
	Texture* mNoiseTexture;

	Device* mDevice;
	std::unordered_map<std::string, Asset*> mAssets;
	mutable std::mutex mMutex;
};