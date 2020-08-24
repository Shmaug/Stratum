#include <Data/AssetManager.hpp>
#include <Data/Font.hpp>
#include <Data/Mesh.hpp>
#include <Core/Pipeline.hpp>
#include <Data/Texture.hpp>

using namespace std;

AssetManager::AssetManager(Device* device) : mDevice(device) {
	uint8_t whitePixels[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
	uint8_t blackPixels[4] = { 0, 0, 0, 0xFF };
	uint8_t transparentBlackPixels[4] = { 0, 0, 0, 0 };
	uint8_t bumpPixels[4] = { 0x80, 0x80, 0xFF, 0xFF };
	uint8_t noisePixels[4 * 256*256];
	for (uint32_t i = 0; i < 4*256*256; i++) noisePixels[i] = rand() % 0xFF;
	mWhiteTexture = new Texture("White", device, whitePixels, 4, { 1, 1, 1 }, vk::Format::eR8G8B8A8Unorm, 1);
	mBlackTexture = new Texture("Black", device, blackPixels, 4, { 1, 1, 1 }, vk::Format::eR8G8B8A8Unorm, 1);
	mTransparentBlackTexture = new Texture("TransparentBlack", device, transparentBlackPixels, 4, { 1, 1, 1 }, vk::Format::eR8G8B8A8Unorm, 1);
	mBumpTexture = new Texture("Bump", device, bumpPixels, 4, { 1, 1, 1 }, vk::Format::eR8G8B8A8Unorm, 1);
	mNoiseTexture = new Texture("RGBA Noise", device, noisePixels, 4*256*256, { 256, 256, 1 }, vk::Format::eR8G8B8A8Unorm, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage);
}
AssetManager::~AssetManager() {
	delete mWhiteTexture;
	delete mBlackTexture;
	delete mTransparentBlackTexture;
	delete mBumpTexture;
	delete mNoiseTexture;
	for (auto& asset : mAssets)
		switch (asset.second.index()) {
			case 0:
				delete get<Pipeline*>(asset.second);
				break;
			case 1:
				delete get<Texture*>(asset.second);
				break;
			case 2:
				delete get<Mesh*>(asset.second);
				break;
			case 3:
				delete get<Font*>(asset.second);
				break;
		}
}

Pipeline* AssetManager::LoadPipeline(const string& filename) {
	mMutex.lock();
	Asset& asset = mAssets[filename];
	if (asset.index() != 0 || get<Pipeline*>(asset) == nullptr) asset = new Pipeline(filename, mDevice, filename);
	mMutex.unlock();
	return get<Pipeline*>(asset);
}
Texture* AssetManager::LoadTexture(const string& filename, TextureLoadFlags flags) {
	mMutex.lock();
	Asset& asset = mAssets[filename + to_string((uint32_t)flags)];
	if (asset.index() != 1 || get<Texture*>(asset) == nullptr) asset = new Texture(filename, mDevice, filename, flags);
	mMutex.unlock();
	return get<Texture*>(asset);
}
Texture* AssetManager::LoadCubemap(const string& posx, const string& negx, const string& posy, const string& negy, const string& posz, const string& negz, TextureLoadFlags flags) {
	mMutex.lock();
	Asset& asset = mAssets[negx + posx + negy + posy + negz + posz + to_string((uint32_t)flags)];
	if (asset.index() != 1 || get<Texture*>(asset) == nullptr) asset = new Texture(negx + " Cube", mDevice, posx, negx, posy, negy, posz, negz, flags);
	mMutex.unlock();
	return get<Texture*>(asset);
}
Mesh* AssetManager::LoadMesh(const string& filename, float scale) {
	mMutex.lock();
	Asset& asset = mAssets[filename];
	if (asset.index() != 2 || get<Mesh*>(asset) == nullptr) asset = new Mesh(filename, mDevice, filename, scale);
	mMutex.unlock();
	return get<Mesh*>(asset);
}
Font* AssetManager::LoadFont(const string& filename) {
	mMutex.lock();
	Asset& asset = mAssets[filename];
	if (asset.index() != 3 || get<Font*>(asset) == nullptr) asset = new Font(filename, mDevice, filename);
	mMutex.unlock();
	return get<Font*>(asset);
}