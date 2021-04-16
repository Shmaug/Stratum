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

	device_allocator<aiVector3D> alloc(device, device.MemoryTypeIndex(host_visible_coherent));
	device_vector<aiVector3D> vertices(alloc);
	device_vector<aiVector3D> normals(alloc);
	device_vector<aiVector3D> tangents(alloc);
	device_vector<aiVector3D> texcoords(alloc);
	device_vector<uint32_t> indices(alloc);

	for (const aiMesh* m : span(scene->mMeshes, scene->mNumMeshes)) {
		size_t vertsPerFace;
		if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_POINT) vertsPerFace = 1;
		else if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_LINE) vertsPerFace = 2;
		else if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_TRIANGLE) vertsPerFace = 3;
		else continue;

		mSubmeshes.emplace_back(m->mNumFaces, (uint32_t)indices.size(), (uint32_t)vertices.size());

		size_t tmp = vertices.size();
		vertices.resize(tmp + m->mNumVertices);
		normals.resize(tmp + m->mNumVertices);
		tangents.resize(tmp + m->mNumVertices);
		texcoords.resize(tmp + m->mNumVertices);
		ranges::copy_n(m->mVertices, m->mNumVertices, &vertices[tmp]);
		ranges::copy_n(m->mNormals, m->mNumVertices, &normals[tmp]);
		ranges::copy_n(m->mTangents, m->mNumVertices, &tangents[tmp]);
		ranges::copy_n(m->mTextureCoords[0], m->mNumVertices, &texcoords[tmp]);

		tmp = indices.size();
		indices.resize(indices.size() + m->mNumFaces*vertsPerFace);
		for (const aiFace& f : span(m->mFaces, m->mNumFaces)) {
			ranges::copy_n(f.mIndices, f.mNumIndices, &indices[tmp]);
			tmp += f.mNumIndices;
		}
	}

	mIndices = make_buffer(indices, "Indices", vk::BufferUsageFlagBits::eIndexBuffer);

	mGeometry.mBindings.resize(4);
	mGeometry.mBindings[0] = { make_buffer(vertices, "Vertices", vk::BufferUsageFlagBits::eVertexBuffer), vk::VertexInputRate::eVertex };
	mGeometry.mBindings[1] = { make_buffer(normals, "Normals", vk::BufferUsageFlagBits::eVertexBuffer), vk::VertexInputRate::eVertex };
	mGeometry.mBindings[2] = { make_buffer(tangents, "Tangents", vk::BufferUsageFlagBits::eVertexBuffer), vk::VertexInputRate::eVertex };
	mGeometry.mBindings[3] = { make_buffer(texcoords, "Texcoords", vk::BufferUsageFlagBits::eVertexBuffer), vk::VertexInputRate::eVertex };
	mGeometry[VertexAttributeType::ePosition][0] = GeometryData::Attribute(0, vk::Format::eR32G32B32Sfloat, 0);
	mGeometry[VertexAttributeType::eNormal][0] = GeometryData::Attribute(1, vk::Format::eR32G32B32Sfloat, 0);
	mGeometry[VertexAttributeType::eTangent][0] = GeometryData::Attribute(2, vk::Format::eR32G32B32Sfloat, 0);
	mGeometry[VertexAttributeType::eTexcoord][0] = GeometryData::Attribute(3, vk::Format::eR32G32B32Sfloat, 0);
}
