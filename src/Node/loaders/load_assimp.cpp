#include "../Scene.hpp"

#ifdef STRATUM_ENABLE_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#endif

#include <portable-file-dialogs.h>

namespace stm {

#ifdef STRATUM_ENABLE_ASSIMP
void Scene::load_assimp(Node& root, CommandBuffer& commandBuffer, const fs::path& filename) {
	ProfilerRegion ps("load_assimp", commandBuffer);

	Device& device = commandBuffer.mDevice;

	// Create an instance of the Importer class
	Assimp::Importer importer;
	// And have it read the given file with some example postprocessing
	// Usually - if speed is not the most important aspect for you - you'll
	// propably to request more postprocessing than we do in this example.
	uint32_t flags = aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_TransformUVCoords;
	flags &= ~(aiProcess_CalcTangentSpace); // Never use Assimp's tangent gen code
	flags &= ~(aiProcess_FindDegenerates); // Avoid converting degenerated triangles to lines
	//flags &= ~(aiProcess_RemoveRedundantMaterials);
	flags &= ~(aiProcess_SplitLargeMeshes);

	int removeFlags = aiComponent_COLORS;
	for (uint32_t uvLayer = 1; uvLayer < AI_MAX_NUMBER_OF_TEXTURECOORDS; uvLayer++) removeFlags |= aiComponent_TEXCOORDSn(uvLayer);
	importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, removeFlags);

	const aiScene* scene = importer.ReadFile(filename.string(), flags);

	// If the import failed, report it
	if (!scene) {
		cout << "Failed to load " << filename << ": " << importer.GetErrorString() << endl;
		return;
	}

	vector<component_ptr<Material>> materials;
	vector<component_ptr<Mesh>> meshes;
	unordered_map<string, Image::View> images;

	auto get_image = [&](fs::path path, bool srgb) -> Image::View {
		if (path.is_relative()) {
			fs::path cur = fs::current_path();
			fs::current_path(filename.parent_path());
			path = fs::absolute(path);
			fs::current_path(cur);
		}
		auto it = images.find(path.string());
		if (it != images.end()) return it->second;
		ImageData pixels = load_image_data(device, path, srgb);
		auto img = make_shared<Image>(commandBuffer, path.filename().string(), pixels, 1);
		commandBuffer.hold_resource(img);
		images.emplace(path.string(), img);
		return img;
	};

	if (scene->HasLights())
		printf_color(ConsoleColor::eYellow, "Warning: punctual lights are unsupported\n");

	if (scene->HasMaterials()) {
		bool interpret_as_pbr = false;
		if (filename.extension().string() == ".fbx") {
			pfd::message n("Interpret Fbx as PBR?", "Convert diffuse/specular to basecolor/roughness/metallic textures?", pfd::choice::yes_no);
			interpret_as_pbr = n.result() == pfd::button::yes;
		}

		Node& materials_node = root.make_child("materials");
		for (int i = 0; i < scene->mNumMaterials; i++) {
			aiMaterial* m = scene->mMaterials[i];
			Material& material = *materials.emplace_back(materials_node.make_child(m->GetName().C_Str()).make_component<Material>());

			ImageValue3 diffuse = make_image_value3({}, float3::Ones());
			ImageValue4 specular = make_image_value4({}, float4::Ones());
			ImageValue1 roughness = make_image_value1({}, 1);
			ImageValue3 transmittance = make_image_value3({}, float3::Zero());
			ImageValue3 emission = make_image_value3({}, float3::Zero());
			float eta = 1.45f;

            aiColor3D tmp_color;
            if (m->Get(AI_MATKEY_COLOR_EMISSIVE, tmp_color) == AI_SUCCESS) emission.value = float3(tmp_color.r, tmp_color.g, tmp_color.b);
            if (m->Get(AI_MATKEY_COLOR_DIFFUSE, tmp_color) == AI_SUCCESS) diffuse.value  = float3(tmp_color.r, tmp_color.g, tmp_color.b);
            if (m->Get(AI_MATKEY_COLOR_SPECULAR, tmp_color) == AI_SUCCESS) specular.value = float4(tmp_color.r, tmp_color.g, tmp_color.b, 1);
			if (m->Get(AI_MATKEY_SHININESS, roughness.value) == AI_SUCCESS) roughness.value = roughness.value;
            if (m->Get(AI_MATKEY_COLOR_TRANSPARENT, tmp_color) == AI_SUCCESS) transmittance.value = float3(tmp_color.r, tmp_color.g, tmp_color.b);
			m->Get(AI_MATKEY_REFRACTI, eta);

			if (m->GetTextureCount(aiTextureType_EMISSIVE) > 0) {
				aiString aiPath;
				m->GetTexture(aiTextureType_EMISSIVE, 0, &aiPath);
				material.emission = make_image_value3(get_image(aiPath.C_Str(), true), float3::Ones());
			}
			if (m->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
				aiString aiPath;
				m->GetTexture(aiTextureType_DIFFUSE, 0, &aiPath);
				diffuse.image = get_image(aiPath.C_Str(), true);
			}
			if (m->GetTextureCount(aiTextureType_SPECULAR) > 0) {
				aiString aiPath;
				m->GetTexture(aiTextureType_SPECULAR, 0, &aiPath);
				specular.image = get_image(aiPath.C_Str(), true);
			}
			if (m->GetTextureCount(aiTextureType_SHININESS) > 0) {
				aiString aiPath;
				m->GetTexture(aiTextureType_SHININESS, 0, &aiPath);
				roughness.image = get_image(aiPath.C_Str(), true);
			}

			if (interpret_as_pbr)
				material = root.find_in_ancestor<Scene>()->make_metallic_roughness_material(commandBuffer, diffuse, specular, transmittance, eta, emission);
			else
				material = root.find_in_ancestor<Scene>()->make_diffuse_specular_material(commandBuffer, diffuse, make_image_value3(specular.image,specular.value.head<3>()), make_image_value1({}), transmittance, eta, emission);
		}
	}

	if (scene->HasMeshes()) {
		vk::BufferUsageFlags bufferUsage = vk::BufferUsageFlagBits::eVertexBuffer|vk::BufferUsageFlagBits::eIndexBuffer|vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eTransferSrc;
		#ifdef VK_KHR_buffer_device_address
		bufferUsage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
		bufferUsage |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
		#endif

		Node& meshes_node = root.make_child("meshes");
		for (int i = 0; i < scene->mNumMeshes; i++) {
			aiMesh* m = scene->mMeshes[i];
			Node& mesh_node = meshes_node.make_child(m->mName.C_Str());

			if (!(m->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) || (m->mPrimitiveTypes & ~aiPrimitiveType_TRIANGLE) != 0)
				continue;

			Buffer::View<float3> positions_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp vertices", m->mNumVertices*sizeof(float3), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
			Buffer::View<float3> normals_tmp   = make_shared<Buffer>(commandBuffer.mDevice, "tmp normals" , m->mNumVertices*sizeof(float3), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
			Buffer::View<uint32_t> indices_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp indices" , m->mNumFaces*3*sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
			for (int vi = 0; vi < m->mNumVertices; vi++) {
				positions_tmp[vi] = float3(m->mVertices[vi].x, m->mVertices[vi].y, m->mVertices[vi].z);
				normals_tmp[vi] = float3(m->mNormals[vi].x, m->mNormals[vi].y, m->mNormals[vi].z);
			}
			for (int fi = 0; fi < m->mNumFaces; fi++)
				for (int j = 0; j < 3; j++)
					indices_tmp[fi*3 + j] = m->mFaces[fi].mIndices[j];

			vk::BufferUsageFlags bufferUsage = vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer;
			#ifdef VK_KHR_buffer_device_address
			bufferUsage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
			bufferUsage |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
			#endif

			auto vao = make_shared<VertexArrayObject>(unordered_map<VertexArrayObject::AttributeType, vector<VertexArrayObject::Attribute>>{
				{ VertexArrayObject::AttributeType::ePosition, { {
					VertexArrayObject::AttributeDescription{ (uint32_t)sizeof(float3), vk::Format::eR32G32B32Sfloat, 0, vk::VertexInputRate::eVertex },
					make_shared<Buffer>(commandBuffer.mDevice, filename.stem().string() + " vertices", positions_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY) } } },
				{ VertexArrayObject::AttributeType::eNormal, { {
					VertexArrayObject::AttributeDescription{ (uint32_t)sizeof(float3), vk::Format::eR32G32B32Sfloat, 0, vk::VertexInputRate::eVertex },
					make_shared<Buffer>(commandBuffer.mDevice, filename.stem().string() + " normals", normals_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY) } } },
			});

			Buffer::View<uint32_t> indexBuffer = make_shared<Buffer>(commandBuffer.mDevice, filename.stem().string() + " indices", indices_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eIndexBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
			commandBuffer.copy_buffer(positions_tmp, vao->at(VertexArrayObject::AttributeType::ePosition)[0].second);
			commandBuffer.copy_buffer(normals_tmp, vao->at(VertexArrayObject::AttributeType::eNormal)[0].second);
			commandBuffer.copy_buffer(indices_tmp, indexBuffer);

			if (m->GetNumUVChannels() >= 1) {
				Buffer::View<float2> uvs_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp uvs", m->mNumVertices*sizeof(float2), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
				for (int vi = 0; vi < m->mNumVertices; vi++)
					uvs_tmp[vi] = float2(m->mTextureCoords[0][vi].x, m->mTextureCoords[0][vi].y);
				(*vao)[VertexArrayObject::AttributeType::eTexcoord].emplace_back(
					VertexArrayObject::AttributeDescription{ (uint32_t)sizeof(float2), vk::Format::eR32G32Sfloat, 0, vk::VertexInputRate::eVertex },
					make_shared<Buffer>(commandBuffer.mDevice, filename.stem().string() + " uvs", uvs_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY));
				commandBuffer.copy_buffer(uvs_tmp, vao->at(VertexArrayObject::AttributeType::eTexcoord)[0].second);
			}

			float area = 0;
			for (int ii = 0; ii < indices_tmp.size(); ii+=3) {
				const float3 v0 = positions_tmp[indices_tmp[ii]];
				const float3 v1 = positions_tmp[indices_tmp[ii + 1]];
				const float3 v2 = positions_tmp[indices_tmp[ii + 2]];
				area += (v2 - v0).matrix().cross((v1 - v0).matrix()).norm();
			}
			meshes.emplace_back( mesh_node.make_component<Mesh>(vao, indexBuffer, vk::PrimitiveTopology::eTriangleList, area) );
		}
	}

	stack<pair<aiNode*, Node*>> nodes;
	nodes.push(make_pair(scene->mRootNode, &root.make_child(scene->mRootNode->mName.C_Str())));
	while (!nodes.empty()) {
		auto[an, n] = nodes.top();
		nodes.pop();

		n->make_component<TransformData>( from_float3x4(Eigen::Array<ai_real,4,4,Eigen::RowMajor>::Map(&an->mTransformation.a1).block<3,4>(0,0).cast<float>()) );

		if (an->mNumMeshes == 1)
			n->make_component<MeshPrimitive>(materials[scene->mMeshes[an->mMeshes[0]]->mMaterialIndex], meshes[an->mMeshes[0]]);
		else if (an->mNumMeshes > 1)
			for (int i = 0; i < an->mNumMeshes; i++)
				n->make_child(scene->mMeshes[an->mMeshes[i]]->mName.C_Str()).make_component<MeshPrimitive>(materials[scene->mMeshes[an->mMeshes[i]]->mMaterialIndex], meshes[an->mMeshes[i]]);

		for (int i = 0; i < an->mNumChildren; i++)
			nodes.push(make_pair(an->mChildren[i], &n->make_child(an->mChildren[i]->mName.C_Str())));
	}

	cout << "Loaded " << filename << endl;
}
#endif

}