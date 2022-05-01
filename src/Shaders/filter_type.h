#ifndef FILTER_KERNEL_TYPE_H
#define FILTER_KERNEL_TYPE_H

#ifdef __cplusplus
namespace stm {
#endif

enum FilterKernelType {
  eAtrous,
  eBox3,
  eBox5,
  eSubsampled,
  eBox3Subsampled,
  eBox5Subsampled,
  eFilterKernelTypeCount
};

#ifdef __cplusplus
}
namespace std {
inline string to_string(const stm::FilterKernelType& t) {
  switch (t) {
    default: return "Unknown";
    case stm::FilterKernelType::eAtrous: return "Atrous";
    case stm::FilterKernelType::eBox3: return "3x3 Box";
    case stm::FilterKernelType::eBox5: return "5x5 Box";
    case stm::FilterKernelType::eSubsampled: return "Subsampled";
    case stm::FilterKernelType::eBox3Subsampled: return "3x3 Box, then Subsampled";
    case stm::FilterKernelType::eBox5Subsampled: return "5x5 Box, then Subsampled";
  }
}
}
#endif

#endif