#pragma once

#include <cstdint>

namespace camel::tensor {

enum DLDeviceType : int32_t {
    kDLCPU = 1,
};

struct DLDevice {
    int32_t device_type;
    int32_t device_id;
};

struct DLDataType {
    uint8_t code;
    uint8_t bits;
    uint16_t lanes;
};

struct DLTensor {
    void *data;
    DLDevice device;
    int32_t ndim;
    DLDataType dtype;
    int64_t *shape;
    int64_t *strides;
    uint64_t byte_offset;
};

struct DLManagedTensor {
    DLTensor dl_tensor;
    void *manager_ctx;
    void (*deleter)(DLManagedTensor *self);
};

} // namespace camel::tensor
