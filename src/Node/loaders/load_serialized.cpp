#include "../Scene.hpp"
#include <miniz.h>

#define MTS_FILEFORMAT_VERSION_V3 0x0003
#define MTS_FILEFORMAT_VERSION_V4 0x0004

namespace stm {

#define ZSTREAM_BUFSIZE 32768

class z_iftream {
public:
	inline z_iftream(std::fstream& fs) : fs(fs) {
		std::streampos pos = fs.tellg();
		fs.seekg(0, fs.end);
		fsize = (size_t)fs.tellg();
		fs.seekg(pos, fs.beg);

		int windowBits = 15;
		m_inflateStream.zalloc = Z_NULL;
		m_inflateStream.zfree = Z_NULL;
		m_inflateStream.opaque = Z_NULL;
		m_inflateStream.avail_in = 0;
		m_inflateStream.next_in = Z_NULL;

		int retval = inflateInit2(&m_inflateStream, windowBits);
		if (retval != Z_OK) {
			throw runtime_error("Could not initialize ZLIB");
		}
	}
	inline virtual ~z_iftream() {
		inflateEnd(&m_inflateStream);
	}

	inline void read(void* ptr, size_t size) {
		uint8_t* targetPtr = (uint8_t*)ptr;
		while (size > 0) {
			if (m_inflateStream.avail_in == 0) {
				size_t remaining = fsize - fs.tellg();
				m_inflateStream.next_in = m_inflateBuffer;
				m_inflateStream.avail_in = (uint32_t)min(remaining, sizeof(m_inflateBuffer));
				if (m_inflateStream.avail_in == 0) {
					throw runtime_error("Read less data than expected");
				}

				fs.read((char*)m_inflateBuffer, m_inflateStream.avail_in);
			}

			m_inflateStream.avail_out = (uint32_t)size;
			m_inflateStream.next_out = targetPtr;

			int retval = inflate(&m_inflateStream, Z_NO_FLUSH);
			switch (retval) {
			case Z_STREAM_ERROR: {
				throw runtime_error("inflate(): stream error!");
			}
			case Z_NEED_DICT: {
				throw runtime_error("inflate(): need dictionary!");
			}
			case Z_DATA_ERROR: {
				throw runtime_error("inflate(): data error!");
			}
			case Z_MEM_ERROR: {
				throw runtime_error("inflate(): memory error!");
			}
			};

			size_t outputSize = size - (size_t)m_inflateStream.avail_out;
			targetPtr += outputSize;
			size -= outputSize;

			if (size > 0 && retval == Z_STREAM_END) {
				throw runtime_error("inflate(): attempting to read past the end of the stream!");
			}
		}
	}

private:
	std::fstream& fs;
	size_t fsize;
	z_stream m_inflateStream;
	uint8_t m_inflateBuffer[ZSTREAM_BUFSIZE];
};

Mesh load_serialized(CommandBuffer& commandBuffer, const fs::path& filename, int shape_index) {
	std::fstream fs(filename.c_str(), std::fstream::in | std::fstream::binary);
	// Format magic number, ignore it
	fs.ignore(sizeof(short));
	// Version number
	short version = 0;
	fs.read((char*)&version, sizeof(short));
	if (shape_index > 0) {
		// Go to the end of the file to see how many components are there
		fs.seekg(-sizeof(uint32_t), fs.end);
		uint32_t count = 0;
		fs.read((char*)&count, sizeof(uint32_t));
		size_t offset = 0;
		if (version == MTS_FILEFORMAT_VERSION_V4) {
			fs.seekg(-sizeof(uint64_t) * (count - shape_index) - sizeof(uint32_t), fs.end);
			fs.read((char*)&offset, sizeof(size_t));
		} else {  // V3
			fs.seekg(-sizeof(uint32_t) * (count - shape_index + 1), fs.end);
			uint32_t upos = 0;
			fs.read((char*)&upos, sizeof(unsigned int));
			offset = upos;
		}
		fs.seekg(offset, fs.beg);
		// Skip the header
		fs.ignore(sizeof(short) * 2);
	}
	z_iftream zs(fs);

	enum ETriMeshFlags {
		EHasNormals = 0x0001,
		EHasTexcoords = 0x0002,
		EHasTangents = 0x0004,  // unused
		EHasColors = 0x0008,
		EFaceNormals = 0x0010,
		ESinglePrecision = 0x1000,
		EDoublePrecision = 0x2000
	};

	uint32_t flags;
	zs.read((char*)&flags, sizeof(uint32_t));
	std::string name;
	if (version == MTS_FILEFORMAT_VERSION_V4) {
		char c;
		while (true) {
			zs.read((char*)&c, sizeof(char));
			if (c == '\0')
				break;
			name.push_back(c);
		}
	}
	size_t vertex_count = 0;
	zs.read((char*)&vertex_count, sizeof(size_t));
	size_t triangle_count = 0;
	zs.read((char*)&triangle_count, sizeof(size_t));

	bool file_double_precision = flags & EDoublePrecision;
	// bool face_normals = flags & EFaceNormals;

	vk::BufferUsageFlags bufferUsage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer;
#ifdef VK_KHR_buffer_device_address
	bufferUsage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
	bufferUsage |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
#endif

	unordered_map<VertexArrayObject::AttributeType, vector<VertexArrayObject::Attribute>> attributes;
	Buffer::View<float3> positions_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp positions", sizeof(float3) * vertex_count, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
	{
		if (file_double_precision) {
			for (uint32_t i = 0; i < vertex_count; i++) {
				double3 tmp;
				zs.read(tmp.data(), sizeof(double) * 3);
				positions_tmp[i] = tmp.cast<float>();
			}
		} else {
			for (uint32_t i = 0; i < vertex_count; i++)
				zs.read(positions_tmp[i].data(), sizeof(float) * 3);
		}
		Buffer::View<float3> positions = make_shared<Buffer>(commandBuffer.mDevice, filename.stem().string() + " positions", positions_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
		commandBuffer.copy_buffer(positions_tmp, positions);
		attributes[VertexArrayObject::AttributeType::ePosition].emplace_back(VertexArrayObject::AttributeDescription{ (uint32_t)sizeof(float3), vk::Format::eR32G32B32Sfloat, 0, vk::VertexInputRate::eVertex }, positions);
	}

	if (flags & EHasNormals) {
		Buffer::View<float3> normals_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp normals", sizeof(float3) * vertex_count, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
		if (file_double_precision) {
			for (uint32_t i = 0; i < vertex_count; i++) {
				double3 tmp;
				zs.read(tmp.data(), sizeof(double) * 3);
				normals_tmp[i] = tmp.cast<float>();
			}
		} else {
			for (uint32_t i = 0; i < vertex_count; i++)
				zs.read(normals_tmp[i].data(), sizeof(float) * 3);
		}
		Buffer::View<float3> normals = make_shared<Buffer>(commandBuffer.mDevice, filename.stem().string() + " normals", normals_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
		commandBuffer.copy_buffer(normals_tmp, normals);
		attributes[VertexArrayObject::AttributeType::eNormal].emplace_back(VertexArrayObject::AttributeDescription{ (uint32_t)sizeof(float3), vk::Format::eR32G32B32Sfloat, 0, vk::VertexInputRate::eVertex }, normals);
	}

	if (flags & EHasTexcoords) {
		Buffer::View<float2> uvs_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp uvs", sizeof(float2) * vertex_count, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
		if (file_double_precision) {
			for (uint32_t i = 0; i < vertex_count; i++) {
				double2 tmp;
				zs.read(tmp.data(), sizeof(double) * 2);
				uvs_tmp[i] = tmp.cast<float>();
			}
		} else {
			for (uint32_t i = 0; i < vertex_count; i++)
				zs.read(uvs_tmp[i].data(), sizeof(float) * 2);
		}
		Buffer::View<float2> uvs = make_shared<Buffer>(commandBuffer.mDevice, filename.stem().string() + " uvs", uvs_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
		commandBuffer.copy_buffer(uvs_tmp, uvs);
		attributes[VertexArrayObject::AttributeType::eTexcoord].emplace_back(VertexArrayObject::AttributeDescription{ (uint32_t)sizeof(float2), vk::Format::eR32G32Sfloat, 0, vk::VertexInputRate::eVertex }, uvs);
	}

	if (flags & EHasColors) {
		Buffer::View<float3> colors_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp colors", sizeof(float3) * vertex_count, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
		if (file_double_precision) {
			for (uint32_t i = 0; i < vertex_count; i++) {
				double3 tmp;
				zs.read(tmp.data(), sizeof(double) * 3);
				colors_tmp[i] = tmp.cast<float>();
			}
		} else {
			for (uint32_t i = 0; i < vertex_count; i++)
				zs.read(colors_tmp[i].data(), sizeof(float) * 3);
		}
		Buffer::View<float2> colors = make_shared<Buffer>(commandBuffer.mDevice, filename.stem().string() + " colors", colors_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
		commandBuffer.copy_buffer(colors_tmp, colors);
		attributes[VertexArrayObject::AttributeType::eColor].emplace_back(VertexArrayObject::AttributeDescription{ (uint32_t)sizeof(float3), vk::Format::eR32G32B32Sfloat, 0, vk::VertexInputRate::eVertex }, colors);
	}

	Buffer::View<uint32_t> indices_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp inds", 3 * triangle_count * sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
	zs.read(indices_tmp.data(), sizeof(uint32_t) * 3 * triangle_count);

	Buffer::View<uint32_t> indexBuffer = make_shared<Buffer>(commandBuffer.mDevice, filename.stem().string() + " indices", indices_tmp.size_bytes(), bufferUsage | vk::BufferUsageFlagBits::eIndexBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	commandBuffer.copy_buffer(indices_tmp, indexBuffer);

	float area = 0;
	for (int ii = 0; ii < indices_tmp.size(); ii+=3) {
		const float3 v0 = positions_tmp[indices_tmp[ii]];
		const float3 v1 = positions_tmp[indices_tmp[ii + 1]];
		const float3 v2 = positions_tmp[indices_tmp[ii + 2]];
		area += (v2 - v0).matrix().cross((v1 - v0).matrix()).norm();
	}
	return Mesh(make_shared<VertexArrayObject>(attributes), indexBuffer, vk::PrimitiveTopology::eTriangleList, area);
}

}