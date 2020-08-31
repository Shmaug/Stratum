#pragma once

#include <Core/Pipeline.hpp>
#include <Core/Buffer.hpp>
#include <Core/DescriptorSet.hpp>

class Material {
public:
	const std::string mName;

	STRATUM_API Material(const std::string& name, ::Pipeline* pipeline);
	STRATUM_API ~Material();

	inline ::Pipeline* Pipeline() const { return mPipeline; };
	inline GraphicsPipeline* GetPassPipeline(const std::string& pass) const { return mPipeline->GetGraphics(pass, mShaderKeywords); }

	// Enable a keyword to be used to select a shader variant
	STRATUM_API void EnableKeyword(const std::string& kw);
	// Disable a keyword to be used to select a shader variant
	STRATUM_API void DisableKeyword(const std::string& kw);

	inline void CullMode(vk::Optional<const vk::CullModeFlags> c) { mCullMode = c; }
	inline vk::Optional<const vk::CullModeFlags> CullMode() const { return mCullMode; }

	inline void PolygonMode(vk::Optional<const vk::PolygonMode> m) { mPolygonMode = m; }
	inline vk::Optional<const vk::PolygonMode> PolygonMode() const { return mPolygonMode; }

	STRATUM_API void SetUniformBuffer(const std::string& name, variant_ptr<Buffer> param, vk::DeviceSize offset, vk::DeviceSize range, uint32_t arrayIndex = 0);
	STRATUM_API void SetStorageBuffer(const std::string& name, variant_ptr<Buffer> param, vk::DeviceSize offset, vk::DeviceSize range, uint32_t arrayIndex = 0);
	STRATUM_API void SetUniformBuffer(const std::string& name, variant_ptr<Buffer> param, uint32_t arrayIndex = 0) { SetUniformBuffer(name, param, 0, param->Size(), arrayIndex); }
	STRATUM_API void SetStorageBuffer(const std::string& name, variant_ptr<Buffer> param, uint32_t arrayIndex = 0) { SetStorageBuffer(name, param, 0, param->Size(), arrayIndex); }
	STRATUM_API void SetStorageTexture(const std::string& name, variant_ptr<Texture> param, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eGeneral);
	STRATUM_API void SetSampledTexture(const std::string& name, variant_ptr<Texture> param, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);
	STRATUM_API void SetSampler(const std::string& name, variant_ptr<Sampler> param, uint32_t arrayIndex = 0);
	STRATUM_API void SetPushParameter(const std::string& name, vk::DeviceSize dataSize, const void* data);
	template<typename T>
	inline void SetPushParameter(const std::string& name, const T& param) { SetPushParameter(name, sizeof(T), &param); }

	inline bool HasDescriptorParameter(const std::string& name) const { return mDescriptorParameters.count(name); }
	inline bool HasDescriptorParameter(const std::string& name, uint32_t index) const { return mDescriptorParameters.count(name) && mDescriptorParameters.at(name).size() > index; }
	inline DescriptorSetEntry GetDescriptorParameter(const std::string& name, uint32_t index = 0) const { return mDescriptorParameters.at(name)[index]; }

	inline bool HasPushParameter(const std::string& name) const { return mPushParameters.count(name); }
	inline bool HasPushParameter(const std::string& name, vk::DeviceSize size) const { return mPushParameters.count(name) && mPushParameters.at(name).first >= size; }
	inline bool GetPushParameter(const std::string& name, vk::DeviceSize dataSize, void* data) const;
	template<typename T>
	inline bool GetPushParameter(const std::string& name, T& ref) const { return GetPushParameter(name, sizeof(T), &ref); }

	STRATUM_API void OnLateUpdate(CommandBuffer* commandBuffer);

private:
	friend class Scene;
	friend class CommandBuffer;
	STRATUM_API void CopyInputSignature(GraphicsPipeline* shader);
	STRATUM_API void BindDescriptorParameters(CommandBuffer* commandBuffer);
	STRATUM_API void PushConstants(CommandBuffer* commandBuffer);

	::Pipeline* mPipeline;
	Device* mDevice;

	std::set<std::string> mShaderKeywords;
	std::unordered_map<std::string, PipelineVariant> mPassCache;

	vk::Optional<const vk::PolygonMode> mPolygonMode;
	vk::Optional<const vk::CullModeFlags> mCullMode;
	DescriptorSet* mCachedDescriptorSet;

	bool mDescriptorSetDirty;

	std::unordered_map<std::string, std::vector<DescriptorSetEntry>> mDescriptorParameters;
	std::unordered_map<std::string, std::pair<vk::DeviceSize, void*>> mPushParameters;
};