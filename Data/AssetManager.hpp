#pragma once

#include <Data/Texture.hpp>

class AssetManager {
public:
	template<typename T, typename... Targs>
	inline stm_ptr<T> Load(const fs::path& filename, Targs&&... args) {
		uint64_t key = hash<string>()(filename.string());
		mMutex.lock();
		if (mLoadedAssets.count(key) == 0)
			mLoadedAssets.emplace(key, new T(filename, mDevice, args...));
		stm_ptr<T> asset = stm_ptr_cast<T>(mLoadedAssets.at(key));
		mMutex.unlock();
		return asset;
	}

	inline stm_ptr<Texture> WhiteTexture() const { return mWhiteTexture; }
	inline stm_ptr<Texture> TransparentBlackTexture() const { return mTransparentBlackTexture; }
	inline stm_ptr<Texture> BlackTexture() const { return mBlackTexture; }
	inline stm_ptr<Texture> BumpTexture() const { return mBumpTexture; }
	inline stm_ptr<Texture> NoiseTexture() const { return mNoiseTexture; }

private:
	friend class Device;
	STRATUM_API AssetManager(Device* device);

	stm_ptr<Texture> mWhiteTexture;
	stm_ptr<Texture> mBlackTexture;
	stm_ptr<Texture> mTransparentBlackTexture;
	stm_ptr<Texture> mBumpTexture;
	stm_ptr<Texture> mNoiseTexture;

	Device* mDevice;
	std::unordered_map<uint64_t, stm_ptr<Asset>> mLoadedAssets;
	mutable std::mutex mMutex;
};