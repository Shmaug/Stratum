#pragma once

#include "Asset.hpp"
#include "../Core/Pipeline.hpp"
#include "Mesh.hpp"

namespace stm {

class Shader : public Asset {
public:
	class Variant {		
		std::set<std::string> mKeywords;

	public:
		STRATUM_API Variant(std::istream& stream);

		friend STRATUM_API std::ostream& operator<<(std::ostream& os, const Variant& v);
	};

private:
	std::unordered_map<uint64_t, Variant> mVariants;
	std::set<std::string> mKeywords;

	std::unordered_map<uint64_t, std::shared_ptr<Pipeline>> mPipelines;

public:
	STRATUM_API Shader();
	STRATUM_API Shader(const fs::path& filename, Device* device, const std::string& name);

	friend STRATUM_API std::ostream& operator<<(std::ostream& os, const Shader& v);

	// Find or create a pipeline for given parameters
	STRATUM_API std::shared_ptr<ComputePipeline> GetPipeline(const std::set<std::string>& keywords, const std::string& kernel) const;

	// Find or create a pipeline for given parameters
	STRATUM_API std::shared_ptr<GraphicsPipeline> GetPipeline(const std::set<std::string>& keywords, const Subpass& subpass, const std::string& shaderPass, 
		vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList,
		const vk::PipelineVertexInputStateCreateInfo& vertexInput = vk::PipelineVertexInputStateCreateInfo(),
		vk::Optional<const vk::CullModeFlags> cullModeOverride = nullptr,
		vk::Optional<const vk::PolygonMode> polyModeOverride = nullptr) const;

	// Find or create a pipeline for given parameters, attempt to get vertex input from mesh (if supplied)
	STRATUM_API std::shared_ptr<GraphicsPipeline> GetPipeline(const std::set<std::string>& keywords, CommandBuffer& commandBuffer, const std::string& shaderPass,
		Mesh* mesh = nullptr,
		vk::Optional<const vk::CullModeFlags> cullModeOverride = nullptr,
		vk::Optional<const vk::PolygonMode> polyModeOverride = nullptr) const;
		
	inline bool HasKeyword(const std::string& kw) const { return mKeywords.count(kw); }
};

}