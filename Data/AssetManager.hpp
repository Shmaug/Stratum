#pragma once

#include <Core/Device.hpp>

class AssetManager {
public:
	STRATUM_API ~AssetManager();

	STRATUM_API Pipeline*	LoadPipeline(const std::string& filename);
	STRATUM_API Texture*	LoadTexture(const std::string& filename, TextureLoadFlags flags = TextureLoadFlags::eSrgb);
	STRATUM_API Texture*  LoadCubemap(const std::string& posx, const std::string& negx, const std::string& posy, const std::string& negy, const std::string& posz, const std::string& negz, TextureLoadFlags flags = TextureLoadFlags::eSrgb);
	STRATUM_API Font*		  LoadFont(const std::string& filename);

	inline Texture* WhiteTexture() const { return mWhiteTexture; }
	inline Texture* TransparentBlackTexture() const { return mTransparentBlackTexture; }
	inline Texture* BlackTexture() const { return mBlackTexture; }
	inline Texture* BumpTexture() const { return mBumpTexture; }
	inline Texture* NoiseTexture() const { return mNoiseTexture; }

private:
	friend class Device;
	STRATUM_API AssetManager(Device* device);

	Texture* mWhiteTexture;
	Texture* mBlackTexture;
	Texture* mTransparentBlackTexture;
	Texture* mBumpTexture;
	Texture* mNoiseTexture;

	Device* mDevice;
	std::unordered_map<std::string, std::variant<Pipeline*, Texture*, Font*>> mAssets;
	mutable std::mutex mMutex;
};