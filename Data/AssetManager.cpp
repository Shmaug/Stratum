#include <Data/AssetManager.hpp>

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