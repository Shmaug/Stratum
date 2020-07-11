#pragma once

#include <Content/Shader.hpp>
#include <Core/Buffer.hpp>

class Material {
public:
	const std::string mName;

	ENGINE_EXPORT Material(const std::string& name, ::Shader* shader);
	ENGINE_EXPORT ~Material();

	inline ::Shader* Shader() const { return mShader; };
	ENGINE_EXPORT GraphicsShader* GetShader(PassType pass);

	ENGINE_EXPORT void PreBeginRenderPass(CommandBuffer* commandBuffer, PassType pass);

	// Default to PASS_MASK_MAX_ENUM, which uses the shader's pass mask
	inline void PassMask(PassType p) { mPassMask = p; }
	inline PassType PassMask() { return mPassMask == PASS_MASK_MAX_ENUM ? Shader()->PassMask() : mPassMask; }

	// Default to ~0, which uses the shader's render queue
	inline void RenderQueue(uint32_t q) { mRenderQueue = q; }
	inline uint32_t RenderQueue() const { return mRenderQueue == ~0 ? Shader()->RenderQueue() : mRenderQueue; }

	// Default to VK_CULL_MODE_FLAG_BITS_MAX_ENUM, which uses the shader's cull mode
	inline void CullMode(VkCullModeFlags c) { mCullMode = c; }
	inline VkCullModeFlags CullMode() const { return mCullMode; }

	// Default to BLEND_MODE_MAX_ENUM, which uses the shader's blend mode
	inline void BlendMode(::BlendMode c) { mBlendMode = c; }
	inline ::BlendMode BlendMode() const { return mBlendMode; }


	ENGINE_EXPORT void SetUniformBuffer(const std::string& name, variant_ptr<Buffer> param, VkDeviceSize offset, VkDeviceSize range, uint32_t arrayIndex = 0);
	ENGINE_EXPORT void SetStorageBuffer(const std::string& name, variant_ptr<Buffer> param, VkDeviceSize offset, VkDeviceSize range, uint32_t arrayIndex = 0);
	ENGINE_EXPORT void SetUniformBuffer(const std::string& name, variant_ptr<Buffer> param, uint32_t arrayIndex = 0) { SetUniformBuffer(name, param, 0, param->Size(), arrayIndex); }
	ENGINE_EXPORT void SetStorageBuffer(const std::string& name, variant_ptr<Buffer> param, uint32_t arrayIndex = 0) { SetStorageBuffer(name, param, 0, param->Size(), arrayIndex); }
	ENGINE_EXPORT void SetStorageTexture(const std::string& name, variant_ptr<Texture> param, uint32_t arrayIndex = 0, VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL);
	ENGINE_EXPORT void SetSampledTexture(const std::string& name, variant_ptr<Texture> param, uint32_t arrayIndex = 0, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	ENGINE_EXPORT void SetSampler(const std::string& name, variant_ptr<Sampler> param, uint32_t arrayIndex = 0);
	ENGINE_EXPORT void SetPushParameter(const std::string& name, VkDeviceSize dataSize, const void* data);
	template<typename T>
	inline void SetPushParameter(const std::string& name, const T& param) { SetPushParameter(name, sizeof(T), &param); }


	// Enable a keyword to be used to select a shader variant
	ENGINE_EXPORT void EnableKeyword(const std::string& kw);
	// Disable a keyword to be used to select a shader variant
	ENGINE_EXPORT void DisableKeyword(const std::string& kw);


	inline bool HasDescriptorParameter(const std::string& name) const { return mDescriptorParameters.count(name); }
	inline bool HasDescriptorParameter(const std::string& name, uint32_t index) const { return mDescriptorParameters.count(name) && mDescriptorParameters.at(name).size() > index; }
	inline DescriptorSetEntry GetDescriptorParameter(const std::string& name, uint32_t index = 0) const { return mDescriptorParameters.at(name)[index]; }

	inline bool HasPushParameter(const std::string& name) const { return mPushParameters.count(name); }
	inline bool HasPushParameter(const std::string& name, VkDeviceSize size) const { return mPushParameters.count(name) && mPushParameters.at(name).first >= size; }
	inline bool GetPushParameter(const std::string& name, VkDeviceSize dataSize, void* data) const;
	template<typename T>
	inline bool GetPushParameter(const std::string& name, T& ref) const { return GetPushParameter(name, sizeof(T), &ref); }

private:
	friend class Scene;
	friend class CommandBuffer;
	ENGINE_EXPORT void CopyInputSignature(GraphicsShader* shader);
	ENGINE_EXPORT void BindDescriptorParameters(CommandBuffer* commandBuffer, PassType pass, Camera* camera);
	ENGINE_EXPORT void BindPushParameters(CommandBuffer* commandBuffer, PassType pass, Camera* camera);

	::Shader* mShader;
	Device* mDevice;

	std::set<std::string> mShaderKeywords;

	VkCullModeFlags mCullMode;
	::BlendMode mBlendMode;
	PassType mPassMask;
	uint32_t mRenderQueue;
	DescriptorSet* mCachedDescriptorSet;

	bool mDescriptorSetDirty;

	std::unordered_map<std::string, std::vector<DescriptorSetEntry>> mDescriptorParameters;
	std::unordered_map<std::string, std::pair<VkDeviceSize, void*>> mPushParameters;
};