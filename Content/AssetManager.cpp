#include <Content/AssetManager.hpp>
#include <Content/Font.hpp>
#include <Content/Mesh.hpp>
#include <Content/Texture.hpp>
#include <Content/Shader.hpp>

using namespace std;

AssetManager::AssetManager(Device* device) : mDevice(device) {
	uint8_t whitePixels[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
	uint8_t blackPixels[4] = { 0, 0, 0, 0xFF };
	uint8_t transparentBlackPixels[4] = { 0, 0, 0, 0 };
	uint8_t bumpPixels[4] = { 0x80, 0x80, 0xFF, 0xFF };
	uint8_t noisePixels[4 * 256*256];
	for (uint32_t i = 0; i < 4*256*256; i++) noisePixels[i] = rand() % 0xFF;
	mWhiteTexture = new Texture("White", device, whitePixels, 4, { 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, 1);
	mBlackTexture = new Texture("Black", device, blackPixels, 4, { 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, 1);
	mTransparentBlackTexture = new Texture("TransparentBlack", device, transparentBlackPixels, 4, { 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, 1);
	mBumpTexture = new Texture("Bump", device, bumpPixels, 4, { 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, 1);
	mNoiseTexture = new Texture("RGBA Noise", device, noisePixels, 4, { 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, 1);
}
AssetManager::~AssetManager() {
	delete mWhiteTexture;
	delete mBlackTexture;
	delete mTransparentBlackTexture;
	delete mBumpTexture;
	delete mNoiseTexture;
	for (auto& asset : mAssets)
		delete asset.second;
}

Shader* AssetManager::LoadShader(const string& filename) {
	mMutex.lock();
	Asset*& asset = mAssets[filename];
	mMutex.unlock();
	if (!asset) asset = new Shader(filename, mDevice, filename);
	return (Shader*)asset;
}
Texture* AssetManager::LoadTexture(const string& filename, bool srgb) {
	mMutex.lock();
	Asset*& asset = mAssets[filename];
	mMutex.unlock();
	if (!asset) asset = new Texture(filename, mDevice, filename, srgb);
	return (Texture*)asset;
}
Texture* AssetManager::LoadCubemap(const string& posx, const string& negx, const string& posy, const string& negy, const string& posz, const string& negz, bool srgb) {
	mMutex.lock();
	Asset*& asset = mAssets[negx + posx + negy + posy + negz + posz];
	mMutex.unlock();
	if (!asset) asset = new Texture(negx + " Cube", mDevice, posx, negx, posy, negy, posz, negz, srgb);
	return (Texture*)asset;
}
Mesh* AssetManager::LoadMesh(const string& filename, float scale) {
	mMutex.lock();
	Asset*& asset = mAssets[filename];
	mMutex.unlock();
	if (!asset) asset = new Mesh(filename, mDevice, filename, scale);
	return (Mesh*)asset;
}
Font* AssetManager::LoadFont(const string& filename, uint32_t pixelHeight) {
	mMutex.lock();
	Asset*& asset = mAssets[filename + to_string(pixelHeight)];
	mMutex.unlock();
	if (!asset) asset = new Font(filename, mDevice, filename, (float)pixelHeight);
	return (Font*)asset;
}