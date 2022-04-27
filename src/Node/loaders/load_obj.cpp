#include "../Scene.hpp"
#include <regex>

namespace stm {

// https://stackoverflow.com/questions/216823/how-to-trim-a-stdstring
// trim from start
static inline string& ltrim(string &s) {
    s.erase(s.begin(), find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !isspace(ch);
    }));
    return s;
}
// trim from end
static inline string& rtrim(string &s) {
    s.erase(find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !isspace(ch);
    }).base(), s.end());
    return s;
}
// trim from both ends
static inline string &trim(string &s) {
    return ltrim(rtrim(s));
}

static vector<int> split_face_str(const string &s) {
    regex rgx("/");
    sregex_token_iterator first{begin(s), end(s), rgx, -1}, last;
    vector<string> list{first, last};
    vector<int> result;
    for (auto &i : list) {
        if (i != "")
            result.push_back(stoi(i));
        else
            result.push_back(0);
    }
    while (result.size() < 3)
        result.push_back(0);

    return result;
}


// Numerical robust computation of angle between unit vectors
inline float unit_angle(const float3 &u, const float3 &v) {
    if (dot(u, v) < 0)
        return (M_PI - 2) * asin(0.5f * (v + u).matrix().norm());
    else
        return 2 * asin(0.5f * (v - u).matrix().norm());
}

inline vector<float3> compute_normal(const vector<float3> &vertices, const vector<uint32_t> &indices) {
	vector<float3> normals(vertices.size(), float3{0, 0, 0});

    // Nelson Max, "Computing Vertex Normals from Facet Normals", 1999
    for (uint32_t j = 0; j < indices.size(); j+=3) {
        float3 n = float3{0, 0, 0};
        for (int i = 0; i < 3; ++i) {
            const float3 &v0 = vertices[indices[j + i]];
            const float3 &v1 = vertices[indices[j + (i + 1) % 3]];
            const float3 &v2 = vertices[indices[j + (i + 2) % 3]];
            const float3 side1 = v1 - v0, side2 = v2 - v0;
            if (i == 0) {
                n = cross(side1, side2);
                float l = length(n);
                if (l == 0) {
                    break;
                }
                n = n / l;
            }
            const float angle = unit_angle(normalize(side1), normalize(side2));
            normals[indices[j + i]] += n * angle;
        }
    }

    for (auto &n : normals) {
        float l = length(n);
        if (l != 0) {
            n = n / l;
        } else {
        	// degenerate normals, set it to 0
            n = float3{0, 0, 0};
        }
    }
    return normals;
}

struct ObjVertex {
    ObjVertex(const vector<int> &id) : v(id[0] - 1), vt(id[1] - 1), vn(id[2] - 1) {}

    bool operator<(const ObjVertex &vertex) const {
        if (v != vertex.v) {
            return v < vertex.v;
        }
        if (vt != vertex.vt) {
            return vt < vertex.vt;
        }
        if (vn != vertex.vn) {
            return vn < vertex.vn;
        }
        return false;
    }

    int v, vt, vn;
};

size_t get_vertex_id(const ObjVertex &vertex,
                     const vector<float3> &pos_pool,
                     const vector<float2> &st_pool,
                     const vector<float3> &nor_pool,
                     vector<float3> &pos,
                     vector<float2> &st,
                     vector<float3> &nor,
                     map<ObjVertex, size_t> &vertex_map) {
    auto it = vertex_map.find(vertex);
    if (it != vertex_map.end())
        return it->second;
    size_t id = pos.size();
    pos.push_back(pos_pool[vertex.v]);
    if (vertex.vt != -1)
        st.push_back(st_pool[vertex.vt]);
    if (vertex.vn != -1)
        nor.push_back(nor_pool[vertex.vn]);
    vertex_map[vertex] = id;
    return id;
}

Mesh load_obj(CommandBuffer& commandBuffer, const fs::path &filename) {
    vector<float3> pos_pool;
    vector<float3> nor_pool;
    vector<float2> st_pool;
    map<ObjVertex, size_t> vertex_map;

    vector<float3> positions;
    vector<float3> normals;
    vector<float2> uvs;
    vector<uint32_t> indices;

    ifstream ifs(filename.c_str(), ifstream::in);
    if (!ifs.is_open()) {
        throw runtime_error("Unable to open the obj file");
    }
    while (ifs.good()) {
        string line;
        getline(ifs, line);
        line = trim(line);
        if (line.size() == 0 || line[0] == '#') { // comment
            continue;
        }

        stringstream ss(line);
        string token;
        ss >> token;
        if (token == "v") {  // vertices
            float x, y, z, w = 1;
            ss >> x >> y >> z >> w;
            pos_pool.push_back(float3{x, y, z} / w);
        } else if (token == "vt") {
            float s, t, w;
            ss >> s >> t >> w;
            st_pool.push_back(float2{s, 1 - t});
        } else if (token == "vn") {
            float x, y, z;
            ss >> x >> y >> z;
            nor_pool.push_back(normalize(float3{x, y, z}));
        } else if (token == "f") {
            string i0, i1, i2;
            ss >> i0 >> i1 >> i2;
            vector<int> i0f = split_face_str(i0);
            vector<int> i1f = split_face_str(i1);
            vector<int> i2f = split_face_str(i2);

            ObjVertex v0(i0f), v1(i1f), v2(i2f);
            size_t v0id = get_vertex_id(v0,
                                        pos_pool,
                                        st_pool,
                                        nor_pool,
                                        positions,
                                        uvs,
                                        normals,
                                        vertex_map);
            size_t v1id = get_vertex_id(v1,
                                        pos_pool,
                                        st_pool,
                                        nor_pool,
                                        positions,
                                        uvs,
                                        normals,
                                        vertex_map);
            size_t v2id = get_vertex_id(v2,
                                        pos_pool,
                                        st_pool,
                                        nor_pool,
                                        positions,
                                        uvs,
                                        normals,
                                        vertex_map);
            indices.push_back((uint32_t)v0id);
            indices.push_back((uint32_t)v1id);
            indices.push_back((uint32_t)v2id);

            string i3;
            if (ss >> i3) {
                vector<int> i3f = split_face_str(i3);
                ObjVertex v3(i3f);
                size_t v3id = get_vertex_id(v3,
                                            pos_pool,
                                            st_pool,
                                            nor_pool,
	                                        positions,
	                                        uvs,
	                                        normals,
	                                        vertex_map);
                indices.push_back((uint32_t)v0id);
                indices.push_back((uint32_t)v2id);
                indices.push_back((uint32_t)v3id);
            }
            string i4;
            if (ss >> i4) {
                throw runtime_error("The object file contains n-gon (n>4) that we do not support.");
            }
        }  // Currently ignore other tokens
    }
    if (normals.empty()) {
        normals = compute_normal(positions, indices);
    }

	Buffer::View<float3> positions_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp vertices", positions.size()*sizeof(float3), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
	Buffer::View<float3> normals_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp normals", normals.size()*sizeof(float3), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
	Buffer::View<uint32_t> indices_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp indices", indices.size()*sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
    memcpy(positions_tmp.data(), positions.data(), positions_tmp.size_bytes());
    memcpy(normals_tmp.data(), normals.data(), normals_tmp.size_bytes());
    memcpy(indices_tmp.data(), indices.data(), indices_tmp.size_bytes());

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

    if (!uvs.empty()) {
        Buffer::View<float2> uvs_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp uvs", uvs.size()*sizeof(float2), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
        (*vao)[VertexArrayObject::AttributeType::eTexcoord].emplace_back(
            VertexArrayObject::AttributeDescription{ (uint32_t)sizeof(float2), vk::Format::eR32G32Sfloat, 0, vk::VertexInputRate::eVertex },
            make_shared<Buffer>(commandBuffer.mDevice, filename.stem().string() + " uvs", uvs_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY));
        memcpy(uvs_tmp.data(), uvs.data(), uvs_tmp.size_bytes());
        commandBuffer.copy_buffer(uvs_tmp, vao->at(VertexArrayObject::AttributeType::eTexcoord)[0].second);
    }

	return Mesh(vao, indexBuffer, vk::PrimitiveTopology::eTriangleList);
}

}