#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "scene.h"
#include "image_value.h"
#include "dist2.h"

struct Environment {
	ImageValue3 emission;

#ifdef __cplusplus
	Buffer::View<float> marginal_pdf;
	Buffer::View<float> row_pdf;
	Buffer::View<float> marginal_cdf;
	Buffer::View<float> row_cdf;

	inline void store(ByteAppendBuffer& bytes, MaterialResources& resources) const {
		emission.store(bytes, resources);
		if (emission.image) {
			bytes.Append(resources.get_index(marginal_pdf));
			bytes.Append(resources.get_index(row_pdf));
			bytes.Append(resources.get_index(marginal_cdf));
			bytes.Append(resources.get_index(row_cdf));
		}
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

	SLANG_MUTATING
	inline void load(uint address) {
		emission.load(address);
		if (emission.has_image()) {
			const uint4 data = gMaterialData.Load4(address);
			marginal_pdf = data[0];
			row_pdf 	 = data[1];
			marginal_cdf = data[2];
			row_cdf 	 = data[3];
		}
	}

	inline Real eval_pdf(const Vector3 dir_out) {
		if (!emission.has_image()) return uniform_sphere_pdfW();
		const float2 uv = cartesian_to_spherical_uv(dir_out);
		uint w, h;
		emission.image().GetDimensions(w, h);
		return dist2d_pdf(gDistributions, marginal_pdf, row_pdf, w, h, uv) / (2 * M_PI * M_PI * sqrt(1 - dir_out.y*dir_out.y));
	}

	inline Spectrum eval(const Vector3 dir_out) {
		if (!emission.has_image()) return emission.value;
		uint w, h;
		emission.image().GetDimensions(w, h);
		const float2 uv = cartesian_to_spherical_uv(dir_out);
		return sample_image(emission.image(), uv, 0).rgb * emission.value;
	}

	inline Spectrum sample(const float2 rnd, out Vector3 dir_out, out Real pdf) {
		if (!emission.has_image()) {
			const float z = 1 - 2 * rnd.x;
			const float r_ = sqrt(max(0, 1 - z * z));
			const float phi = 2 * M_PI * rnd.y;
			dir_out = spherical_uv_to_cartesian(sample_uniform_sphere(rnd.x, rnd.y));
			pdf = uniform_sphere_pdfW();
			return emission.value;
		}
		uint w, h;
		emission.image().GetDimensions(w, h);
		const float2 uv = dist2d_sample(gDistributions, marginal_cdf, row_cdf, w, h, rnd);
		dir_out = spherical_uv_to_cartesian(uv);
		pdf = dist2d_pdf(gDistributions, marginal_pdf, row_pdf, w, h, uv) / (2 * M_PI * M_PI * sqrt(1 - dir_out.y*dir_out.y));
		return emission.value*emission.image().SampleLevel(gSampler, uv, 0).rgb;
	}
#endif
};

#ifdef __cplusplus

inline Environment load_environment(CommandBuffer& commandBuffer, const fs::path& filename) {
	ImageData image = load_image_data(commandBuffer.mDevice, filename, false);
	Environment e;
	e.emission = make_image_value3(make_shared<Image>(commandBuffer, filename.stem().string(), image, 1), float3::Ones());

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