#include "../Scene.hpp"

#ifdef STRATUM_ENABLE_OPENVDB
#include <openvdb/openvdb.h>
#endif

#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/IO.h>
#ifdef STRATUM_ENABLE_OPENVDB
#include <nanovdb/util/OpenToNanoVDB.h>
#endif

using namespace stm::hlsl;
namespace stm {

inline void create_volume(Node& dst, CommandBuffer& commandBuffer, const string& name, const component_ptr<nanovdb::GridHandle<nanovdb::HostBuffer>>& density = {}, const component_ptr<nanovdb::GridHandle<nanovdb::HostBuffer>>& albedo = {}) {
  HeterogeneousVolume& h = *dst.make_component<HeterogeneousVolume>();
  h.density_scale = float3::Ones();
  h.anisotropy = 0.f;
  h.albedo_scale = float3::Ones();
  h.attenuation_unit = 0.1f;
  h.density_grid = density;
  if (density) {
    Buffer::View<byte> staging = make_shared<Buffer>(commandBuffer.mDevice, name + "/staging", density->size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    memcpy(staging.data(), density->data(), density->size());
    h.density_buffer = make_shared<Buffer>(commandBuffer.mDevice, name+"/density", density->size(), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
    commandBuffer.copy_buffer(staging, h.density_buffer);
  }
  h.albedo_grid = albedo;
  if (albedo) {
    Buffer::View<byte> staging = make_shared<Buffer>(commandBuffer.mDevice, name + "/staging", albedo->size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    memcpy(staging.data(), albedo->data(), albedo->size());
    h.albedo_buffer = make_shared<Buffer>(commandBuffer.mDevice, name+"/albedo", albedo->size(), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
    commandBuffer.copy_buffer(staging, h.albedo_buffer);
  }

  if (!dst.find<TransformData>()) {
    const nanovdb::Vec3R bbox_max = h.density_grid->grid<float>()->worldBBox().max();
    const nanovdb::Vec3R bbox_min = h.density_grid->grid<float>()->worldBBox().min();
    const nanovdb::Vec3R center = (bbox_max + bbox_min)/2;
    const nanovdb::Vec3R extent = bbox_max - bbox_min;
    const float scale = 1 / (float)(bbox_max - bbox_min).max();
    dst.make_component<TransformData>( make_transform(
      -MatrixType<nanovdb::Vec3R::ValueType,3,1>::Map(&center[0]).cast<float>() * scale,
      quatf_identity(),
      float3::Constant(scale)) );
  }
}

nanovdb::GridHandle<nanovdb::HostBuffer> load_vol(const fs::path& filename) {
  // code from https://github.com/mitsuba-renderer/mitsuba/blob/master/src/volume/gridvolume.cpp#L217
  enum EVolumeType {
    EFloat32 = 1,
    EFloat16 = 2,
    EUInt8 = 3,
    EQuantizedDirections = 4
  };

  fstream fs(filename.c_str(), fstream::in | fstream::binary);
  char header[3];
  fs.read(header, 3);
  if (header[0] != 'V' || header[1] != 'O' || header[2] != 'L') throw runtime_error("Error loading volume from a file (incorrect header). Filename:" + filename.string());
  uint8_t version;
  fs.read((char*)&version, 1);
  if (version != 3) throw runtime_error("Error loading volume from a file (incorrect header). Filename:" + filename.string());

  int type;
  fs.read((char*)&type, sizeof(int));
  if (type != EFloat32) throw runtime_error("Unsupported volume format (only support Float32). Filename:" + filename.string());

  int xres, yres, zres;
  fs.read((char*)&xres, sizeof(int));
  fs.read((char*)&yres, sizeof(int));
  fs.read((char*)&zres, sizeof(int));

  int channels;
  fs.read((char*)&channels, sizeof(int));
  if (type != EFloat32) throw runtime_error("Unsupported volume format (not Float32). Filename:" + filename.string());

  float3 pmin, pmax;
  fs.read((char*)&pmin, sizeof(float3));
  fs.read((char*)&pmax, sizeof(float3));

  if (channels == 1) {
    vector<float> data(xres * yres * zres);
    fs.read((char*)data.data(), sizeof(float) * xres * yres * zres);
    nanovdb::GridBuilder<float> builder(0, nanovdb::GridClass::FogVolume);
    builder([&](const nanovdb::Coord& ijk) -> float {
      return data[(ijk[2] * yres + ijk[1]) * xres + ijk[0]];
    }, nanovdb::CoordBBox(nanovdb::Coord(0), nanovdb::Coord(xres, yres, zres) - nanovdb::Coord(1)));
    return builder.getHandle<>(1.0, nanovdb::Vec3d(0), filename.stem().string(), nanovdb::GridClass::FogVolume);
  } else
    throw runtime_error("Unsupported volume format (wrong number of channels). Filename:" + filename.string());
}

void load_vol(Node& root, CommandBuffer& commandBuffer, const fs::path &filename) {
  nanovdb::GridHandle<nanovdb::HostBuffer> density_handle = load_vol(filename);
  if (density_handle) {
    if (!fs::exists(filename.string() + ".nvdb"))
      nanovdb::io::writeGrid(filename.string() + ".nvdb", density_handle);
    create_volume(root, commandBuffer, filename.stem().string(), root.make_component<nanovdb::GridHandle<nanovdb::HostBuffer>>(move(density_handle)));
  }
}

void load_nvdb(Node& root, CommandBuffer& commandBuffer, const fs::path &filename) {
  nanovdb::GridHandle<nanovdb::HostBuffer> density_handle = nanovdb::io::readGrid(filename.string().c_str());
  if (density_handle)
    create_volume(root, commandBuffer, filename.stem().string(), root.make_component<nanovdb::GridHandle<nanovdb::HostBuffer>>(move(density_handle)));
}

#ifdef STRATUM_ENABLE_OPENVDB
void load_vdb(Node& root, CommandBuffer& commandBuffer, const fs::path &filename) {
  static bool openvdb_initialized = false;
  if (!openvdb_initialized) {
    openvdb::initialize();
    openvdb_initialized = true;
  }
  openvdb::io::File file(filename.string().c_str());
  if (!file.open()) return;
  unordered_map<string, component_ptr<nanovdb::GridHandle<nanovdb::HostBuffer>>> grids;
  for (auto name_it = file.beginName(); name_it != file.endName(); ++name_it) {
    auto grid = root.make_child(*name_it).make_component<nanovdb::GridHandle<nanovdb::HostBuffer>>(nanovdb::openToNanoVDB(file.readGrid(*name_it)));
    grids.emplace(*name_it, grid);
    if (!fs::exists(filename.string() + *name_it + ".nvdb"))
      nanovdb::io::writeGrid(filename.string() + *name_it + ".nvdb", *grid);
  }
  file.close();
  if (grids.empty()) return;

  component_ptr<nanovdb::GridHandle<nanovdb::HostBuffer>> grid = grids.begin()->second;
  if (grid)
    create_volume(root, commandBuffer, filename.stem().string(), grid);
}
#endif

}