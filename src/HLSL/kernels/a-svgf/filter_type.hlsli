#ifndef FILTER_KERNEL_TYPE_H
#define FILTER_KERNEL_TYPE_H

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
inline string to_string(const FilterKernelType& t) {
  switch (t) {
    default: return "Unknown";
    case FilterKernelType::eAtrous: return "Atrous";
    case FilterKernelType::eBox3: return "3x3 Box";
    case FilterKernelType::eBox5: return "5x5 Box";
    case FilterKernelType::eSubsampled: return "Subsampled";
    case FilterKernelType::eBox3Subsampled: return "3x3 Box, then Subsampled";
    case FilterKernelType::eBox5Subsampled: return "5x5 Box, then Subsampled";
  }
}
#endif

#endif