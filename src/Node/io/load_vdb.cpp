#include "../Scene.hpp"

#ifdef STRATUM_ENABLE_OPENVDB
namespace std {
  template<class F, class...ArgTypes>
  using result_of = invoke_result<F, ArgTypes...>;
}
#include <openvdb/openvdb.h>
#endif

#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/IO.h>
#include <nanovdb/util/OpenToNanoVDB.h>

using namespace stm::hlsl;
namespace stm {

inline HeterogeneousVolume upload_grid(CommandBuffer& commandBuffer, const string& name, const component_ptr<nanovdb::GridHandle<nanovdb::HostBuffer>>& handle) {
  HeterogeneousVolume h;
  h.sigma_s = float3::Ones();
  h.anisotropy = 0.f;
  h.sigma_a = float3::Ones();
  h.handle = handle;
  h.attenuation_unit = .03f;
  h.max_density = handle->grid<float>()->tree().root().maximum();

  Buffer::View<byte> staging = make_shared<Buffer>(commandBuffer.mDevice, name + "/staging", handle->size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
  memcpy(staging.data(), handle->data(), handle->size());
  h.buffer = make_shared<Buffer>(commandBuffer.mDevice, name, handle->size(), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
  commandBuffer.copy_buffer(staging, h.buffer);
  return h;
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
  openvdb::GridBase::Ptr vdbgrid = file.readGrid(file.beginName().gridName());
  file.close();

  nanovdb::GridHandle<nanovdb::HostBuffer> handle = nanovdb::openToNanoVDB(vdbgrid);
  if (handle) {
    HeterogeneousVolume& v = *root.make_component<HeterogeneousVolume>( upload_grid(commandBuffer, filename.stem().string(), root.make_component<nanovdb::GridHandle<nanovdb::HostBuffer>>(move(handle))) );
    if (!root.find<TransformData>()) {
      const nanovdb::Vec3R bbox_max = v.handle->grid<float>()->worldBBox().max();
      const nanovdb::Vec3R bbox_min = v.handle->grid<float>()->worldBBox().min();
      const nanovdb::Vec3R center = (bbox_max + bbox_min)/2;
      const nanovdb::Vec3R extent = bbox_max - bbox_min;
      const float scale = 1 / (float)(bbox_max - bbox_min).max();
      root.make_component<TransformData>( make_transform(
        -MatrixType<nanovdb::Vec3R::ValueType,3,1>::Map(&center[0]).cast<float>() * scale,
        quatf_identity(),
        float3::Constant(scale)) );
    }
  }
}
#endif

void load_nvdb(Node& root, CommandBuffer& commandBuffer, const fs::path &filename) {
  nanovdb::GridHandle<nanovdb::HostBuffer> handle = nanovdb::io::readGrid(filename.string().c_str());
  if (handle)
    root.make_component<HeterogeneousVolume>( upload_grid(commandBuffer, filename.stem().string(), root.make_component<nanovdb::GridHandle<nanovdb::HostBuffer>>(move(handle))) );
}

}