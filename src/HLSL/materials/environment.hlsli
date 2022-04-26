#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "../scene.hlsli"

#ifdef __HLSL__
inline uint upper_bound(StructuredBuffer<float> data, uint first, uint last, float value) {
	uint it;
	int count = last - first;
	while (count > 0) {
		uint it = first;
		int step = count / 2;
		it += step;
		if (value >= data[it]) {
			first = ++it;
			count -= step + 1;
		} else
			count = step;
	}
	return first;
}
inline float dist2d_pdf(StructuredBuffer<float> data, const uint pdf_marginals, const uint pdf_rows, const uint w, const uint h, const float2 uv) {
	const int x = clamp(uv.x * w, 0, w - 1);
	const int y = clamp(uv.y * h, 0, h - 1);
	// What's the PDF for sampling row y?
	const float pdf_y = data[pdf_marginals + y];
	// What's the PDF for sampling row x?
	const float pdf_x = data[pdf_rows + y * w + x];
	return pdf_y * pdf_x * w * h;
}
inline float2 sample_dist2d(StructuredBuffer<float> data, const uint cdf_marginals, const uint cdf_rows, const uint w, const uint h, const float2 rnd) {
	const uint y_ptr = upper_bound(data, cdf_marginals, cdf_marginals + h + 1, rnd.y) - cdf_marginals;
	const int y_offset = clamp(int(y_ptr - 1), 0, h - 1);
	// Uniformly remap u1 to find the continuous offset
	float dy = rnd.y - data[cdf_marginals + y_offset];
	if ((data[cdf_marginals + y_offset + 1] - data[cdf_marginals + y_offset]) > 0)
		dy /= (data[cdf_marginals + y_offset + 1] - data[cdf_marginals + y_offset]);

	// Sample a column at the row y_offset
	const int row_offset = y_offset * (w + 1);
	const uint x_ptr = upper_bound(data, cdf_rows + row_offset, cdf_rows + row_offset + w + 1, rnd.x) - cdf_rows;
	const int x_offset = clamp(int(x_ptr) - row_offset - 1, 0, w - 1);
	// Uniformly remap u2
	float dx = rnd.x - data[cdf_rows + row_offset + x_offset];
	if (data[cdf_rows + row_offset + x_offset + 1] - data[cdf_rows + row_offset + x_offset] > 0)
		dx /= (data[cdf_rows + row_offset + x_offset + 1] - data[cdf_rows + row_offset + x_offset]);

	return float2((x_offset + dx) / w, (y_offset + dy) / h);
}
#endif

struct Environment {
	ImageValue3 emission;

#ifdef __cplusplus
	Buffer::View<float> marginal_pdf;
	Buffer::View<float> row_pdf;
	Buffer::View<float> marginal_cdf;
	Buffer::View<float> row_cdf;

	inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
		emission.store(bytes, resources);
		bytes.Append(resources.get_index(marginal_pdf));
		bytes.Append(resources.get_index(row_pdf));
		bytes.Append(resources.get_index(marginal_cdf));
		bytes.Append(resources.get_index(row_cdf));
	}
	inline void inspector_gui() {
		image_value_field("Emission", emission);
	}
#endif
#ifdef __HLSL__
	uint marginal_pdf;
	uint row_pdf;
	uint marginal_cdf;
	uint row_cdf;

	inline void load(ByteAddressBuffer bytes, inout uint address) {
	}
	inline Real eval_pdf(const Vector3 dir_out) {
		const float2 uv = cartesian_to_spherical_uv(dir_out);
		uint w, h;
		emission.image().GetDimensions(w, h);
		return dist2d_pdf(gDistributions, marginal_pdf, row_pdf, w, h, uv) / (2 * M_PI * M_PI * sqrt(1 - dir_out.y*dir_out.y));
	}
	inline Spectrum sample(const float2 rnd, out Vector3 dir_out, out Real pdf) {
		uint w, h;
		emission.image().GetDimensions(w, h);
		const float2 uv = sample_dist2d(gDistributions, marginal_cdf, row_cdf, w, h, rnd);
		dir_out = spherical_uv_to_cartesian(uv);
		pdf = dist2d_pdf(gDistributions, marginal_pdf, row_pdf, w, h, uv) / (2 * M_PI * M_PI * sqrt(1 - dir_out.y*dir_out.y));
		if (emission.has_image())
			return emission.value*emission.image().SampleLevel(gSampler, uv, 0).rgb;
		else
			return emission.value;
	}
#endif
};

#ifdef __HLSL__
template<> inline Environment load_material<Environment>(uint address, const ShadingData shading_data) {
    Environment material;
	material.emission		= load_image_value3(address);
	material.marginal_pdf 	= gMaterialData.Load(address); address += 4;
	material.row_pdf 		= gMaterialData.Load(address); address += 4;
	material.marginal_cdf 	= gMaterialData.Load(address); address += 4;
	material.row_cdf 	  	= gMaterialData.Load(address); address += 4;
    return material;
}

template<> inline EmissionEvalRecord eval_material_emission(const Environment material, const Vector3 dir_out, const ShadingData shading_data) {
	uint w, h;
	material.emission.image().GetDimensions(w, h);
	const float2 uv = cartesian_to_spherical_uv(-dir_out);
	EmissionEvalRecord r;
	r.f = sample_image(material.emission.image(), uv, 0).rgb * material.emission.value;
	r.pdf = dist2d_pdf(gDistributions, material.marginal_pdf, material.row_pdf, w, h, uv) / (2 * M_PI * M_PI * sqrt(1 - dir_out.y*dir_out.y));
	return r;
}
#endif // __HLSL__

#ifdef __cplusplus

inline void build_distributions(const span<float4>& img, const vk::Extent2D& extent, span<float> pdf_marginals, span<float> pdf_rows, span<float> cdf_marginals, span<float> cdf_rows) {
	const float invHeight = 1 / (float)extent.height;
	auto f = [&](uint32_t x, uint32_t y) {
		return luminance(img[y*extent.width + x].head<3>()) * sin(M_PI * (y + 0.5f)*invHeight);
	};

	// Construct a 1D distribution for each row
	//vector<thread> threads;
	//const uint32_t block_size = (extent.height + thread::hardware_concurrency() - 1) / thread::hardware_concurrency();
	//for (uint32_t thread_idx = 0; thread_idx < thread::hardware_concurrency(); thread_idx++) {
	//	threads.emplace_back([&,thread_idx]{
	//		for (int y = thread_idx*block_size; y < min((thread_idx+1)*block_size, extent.height); y++) {
			for (int y = 0; y < extent.height; y++) {
				cdf_rows[y * (extent.width + 1)] = 0;
				for (int x = 0; x < extent.width; x++) {
					cdf_rows[y * (extent.width + 1) + (x + 1)] = cdf_rows[y * (extent.width + 1) + x] + f(x,y);
				}
				float integral = cdf_rows[y * (extent.width + 1) + extent.width];
				if (integral > 0) {
					// Normalize
					for (int x = 0; x < extent.width; x++) {
						cdf_rows[y * (extent.width + 1) + x] /= integral;
					}
					// Note that after normalization, the last entry of each row for
					// cdf_rows is still the "integral".
					// We need this later for constructing the marginal distribution.

					// Setup the pmf/pdf
					for (int x = 0; x < extent.width; x++) {
						pdf_rows[y * extent.width + x] = f(x,y) / integral;
					}
				} else {
					// We shouldn't sample this row, but just in case we
					// set up a uniform distribution.
					for (int x = 0; x < extent.width; x++) {
						pdf_rows[y * extent.width + x] = float(1) / float(extent.width);
						cdf_rows[y * (extent.width + 1) + x] = float(x) / float(extent.width);
					}
					cdf_rows[y * (extent.width + 1) + extent.width] = 1;
				}
			}
	//});
	//}
	//for (thread& t : threads) t.join();

	// Now construct the marginal CDF for each column.
	cdf_marginals[0] = 0;
	for (int y = 0; y < extent.height; y++) {
		float weight = cdf_rows[y * (extent.width + 1) + extent.width];
		cdf_marginals[y + 1] = cdf_marginals[y] + weight;
	}
	float total_values = cdf_marginals.back();
	if (total_values > 0) {
		// Normalize
		for (int y = 0; y < extent.height; y++) {
			cdf_marginals[y] /= total_values;
		}
		cdf_marginals[extent.height] = 1;
		// Setup pdf cols
		for (int y = 0; y < extent.height; y++) {
			float weight = cdf_rows[y * (extent.width + 1) + extent.width];
			pdf_marginals[y] = weight / total_values;
		}
	} else {
		// The whole thing is black...why are we even here?
		// Still set up a uniform distribution.
		for (int y = 0; y < extent.height; y++) {
			pdf_marginals[y] = float(1) / float(extent.height);
			cdf_marginals[y] = float(y) / float(extent.height);
		}
		cdf_marginals[extent.height] = 1;
	}
	// We finally normalize the last entry of each cdf row to 1
	for (int y = 0; y < extent.height; y++) {
		cdf_rows[y * (extent.width + 1) + extent.width] = 1;
	}
}

inline void build_distributions(CommandBuffer& commandBuffer, const ShaderDatabase& shaders) {
	auto build_row_dist = make_shared<ComputePipelineState>("build_row_dist", shaders.at("build_row_dist"));
	auto sum_row_cdf = make_shared<ComputePipelineState>("sum_row_cdf", shaders.at("sum_row_cdf"));
	auto build_marginal_dist = make_shared<ComputePipelineState>("build_marginal_dist", shaders.at("build_marginal_dist"));
	auto build_marginal_sample_dist = make_shared<ComputePipelineState>("build_marginal_sample_dist", shaders.at("build_marginal_sample_dist"));
	auto build_conditional_sample_dist = make_shared<ComputePipelineState>("build_conditional_sample_dist", shaders.at("build_conditional_sample_dist"));
}

inline Environment load_environment(CommandBuffer& commandBuffer, const fs::path& filename) {
	ImageData image = load_image_data(commandBuffer.mDevice, filename, false);
	Environment e;
	e.emission.value = float3::Ones();
	e.emission.image = make_shared<Image>(commandBuffer, filename.stem().string(), image, 1);

	Buffer::View<float> marginalPDF = make_shared<Buffer>(commandBuffer.mDevice, "marginal_pdf_tmp", image.extent.height*sizeof(float), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
	Buffer::View<float> rowPDF      = make_shared<Buffer>(commandBuffer.mDevice, "row_pdf_tmp"     , image.extent.width*image.extent.height*sizeof(float), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
	Buffer::View<float> marginalCDF = make_shared<Buffer>(commandBuffer.mDevice, "marginal_cdf_tmp", (image.extent.height+1)*sizeof(float), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
	Buffer::View<float> rowCDF      = make_shared<Buffer>(commandBuffer.mDevice, "row_cdf_tmp"     , (image.extent.width+1)*image.extent.height*sizeof(float), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);

	fs::path cacheFile = filename.string() + ".dists";
	if (fs::exists(cacheFile)) {
		ifstream file(cacheFile, ios::binary);
		file.read(reinterpret_cast<char*>(marginalPDF.data()), marginalPDF.size_bytes());
		file.read(reinterpret_cast<char*>(rowPDF.data()), rowPDF.size_bytes());
		file.read(reinterpret_cast<char*>(marginalCDF.data()), marginalCDF.size_bytes());
		file.read(reinterpret_cast<char*>(rowCDF.data()), rowCDF.size_bytes());
	} else {
		build_distributions(
			span<float4>(reinterpret_cast<float4*>(image.pixels.data()), image.pixels.size_bytes()/sizeof(float4)), vk::Extent2D(image.extent.width, image.extent.height),
			span(marginalPDF.data(), marginalPDF.size()),
			span(rowPDF.data(), rowPDF.size()),
			span(marginalCDF.data(), marginalCDF.size()),
			span(rowCDF.data(), rowCDF.size()) );

		ofstream file(cacheFile, ios::binary);
		file.write(reinterpret_cast<char*>(marginalPDF.data()), marginalPDF.size_bytes());
		file.write(reinterpret_cast<char*>(rowPDF.data()), rowPDF.size_bytes());
		file.write(reinterpret_cast<char*>(marginalCDF.data()), marginalCDF.size_bytes());
		file.write(reinterpret_cast<char*>(rowCDF.data()), rowCDF.size_bytes());
	}

	commandBuffer.barrier({ marginalPDF, rowPDF, marginalCDF, rowCDF }, vk::PipelineStageFlagBits::eHost, vk::AccessFlagBits::eHostWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);

	e.marginal_pdf = make_shared<Buffer>(commandBuffer.mDevice, "marginal_PDF", marginalPDF.size_bytes(), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	e.row_pdf = make_shared<Buffer>(commandBuffer.mDevice, "row_pdf", rowPDF.size_bytes(), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	e.marginal_cdf = make_shared<Buffer>(commandBuffer.mDevice, "marginal_cdf", marginalCDF.size_bytes(), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	e.row_cdf = make_shared<Buffer>(commandBuffer.mDevice, "row_cdf", rowCDF.size_bytes(), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);

	commandBuffer.copy_buffer(marginalPDF, e.marginal_pdf);
	commandBuffer.copy_buffer(rowPDF, e.row_pdf);
	commandBuffer.copy_buffer(marginalCDF, e.marginal_cdf);
	commandBuffer.copy_buffer(rowCDF, e.row_cdf);

	return e;
}
#endif

#endif