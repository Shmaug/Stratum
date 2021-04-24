#include "Mesh.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace stm;

Mesh::Mesh(CommandBuffer& commandBuffer, const fs::path& filename) : Mesh(filename.stem().string()) {
	Device& device = commandBuffer.mDevice;

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(filename.string().c_str(), 
		aiPostProcessSteps::aiProcess_CalcTangentSpace |
		aiPostProcessSteps::aiProcess_JoinIdenticalVertices |
		aiPostProcessSteps::aiProcess_MakeLeftHanded |
		aiPostProcessSteps::aiProcess_Triangulate |
		aiPostProcessSteps::aiProcess_SortByPType);

	static_assert(is_same_v<float, ai_real>);
	static_assert(sizeof(aiVector3D) == sizeof(float)*3);
	static_assert(sizeof(std::array<aiVector3D,4>) == sizeof(float)*12);

	buffer_vector<aiVector3D> vertices(device, 0);
	buffer_vector<aiVector3D> normals(device, 0);
	buffer_vector<aiVector3D> tangents(device, 0);
	buffer_vector<aiVector3D> texcoords(device, 0);
	buffer_vector<uint32_t> indices(device, 0);

	for (const aiMesh* m : span(scene->mMeshes, scene->mNumMeshes)) {
		size_t vertsPerFace;
		if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_POINT) vertsPerFace = 1;
		else if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_LINE) vertsPerFace = 2;
		else if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_TRIANGLE) vertsPerFace = 3;
		else continue;

		mSubmeshes.emplace_back(m->mNumFaces, (uint32_t)indices.size(), (uint32_t)vertices.size());

		size_t start = vertices.size();
		size_t sz = start + m->mNumVertices;
		vertices.resize(sz);
		normals.resize(sz);
		tangents.resize(sz);
		texcoords.resize(sz);
		ranges::copy_n(m->mVertices, m->mNumVertices, &vertices[start]);
		ranges::copy_n(m->mNormals, m->mNumVertices, &normals[start]);
		ranges::copy_n(m->mTangents, m->mNumVertices, &tangents[start]);
		ranges::copy_n(m->mTextureCoords[0], m->mNumVertices, &texcoords[start]);

		start = indices.size();
		indices.resize(start + m->mNumFaces*vertsPerFace);
		for (const aiFace& f : span(m->mFaces, m->mNumFaces)) {
			ranges::copy_n(f.mIndices, f.mNumIndices, &indices[start]);
			start += f.mNumIndices;
		}
	}

	ProfilerRegion ps("load " + filename.stem().string(), commandBuffer);
	mIndices = commandBuffer.copy_buffer<uint32_t>(indices, vk::BufferUsageFlagBits::eIndexBuffer);

	mGeometry.mBindings.resize(4);
	mGeometry.mBindings[0].first = commandBuffer.copy_buffer<aiVector3D>(vertices, vk::BufferUsageFlagBits::eVertexBuffer);
	mGeometry.mBindings[1].first = commandBuffer.copy_buffer<aiVector3D>(normals, vk::BufferUsageFlagBits::eVertexBuffer);
	mGeometry.mBindings[2].first = commandBuffer.copy_buffer<aiVector3D>(tangents, vk::BufferUsageFlagBits::eVertexBuffer);
	mGeometry.mBindings[3].first = commandBuffer.copy_buffer<aiVector3D>(texcoords, vk::BufferUsageFlagBits::eVertexBuffer);

	mGeometry[VertexAttributeType::ePosition][0] = GeometryData::Attribute(0, vk::Format::eR32G32B32Sfloat, 0);
	mGeometry[VertexAttributeType::eNormal][0]   = GeometryData::Attribute(1, vk::Format::eR32G32B32Sfloat, 0);
	mGeometry[VertexAttributeType::eTangent][0]  = GeometryData::Attribute(2, vk::Format::eR32G32B32Sfloat, 0);
	mGeometry[VertexAttributeType::eTexcoord][0] = GeometryData::Attribute(3, vk::Format::eR32G32B32Sfloat, 0);
}
