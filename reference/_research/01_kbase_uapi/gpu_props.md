# GPU Properties

Retrieved through KBASE_IOCTL_GET_GPUPROPS. Two calls are needed: the first
with size 0 to obtain the required buffer size, the second with the actual
buffer.

The buffer is a sequence of (key, value) pairs. Keys are u32 and their low
two bits encode the size of the following value:

00 = u8
01 = u16
10 = u32
11 = u64

Values are little-endian and tightly packed with no padding.

## Key list

Product identification:
  1  PRODUCT_ID
  2  VERSION_STATUS
  3  MINOR_REVISION
  4  MAJOR_REVISION

Frequencies:
  6  GPU_FREQ_KHZ_MAX

Shader:
  8  LOG2_PROGRAM_COUNTER_SIZE
  18 MAX_THREADS
  19 MAX_WORKGROUP_SIZE
  20 MAX_BARRIER_SIZE
  21 MAX_REGISTERS
  22 MAX_TASK_QUEUE
  23 MAX_THREAD_GROUP_SPLIT

Texture:
  9  TEXTURE_FEATURES_0
  10 TEXTURE_FEATURES_1
  11 TEXTURE_FEATURES_2
  80 TEXTURE_FEATURES_3

Memory:
  12 GPU_AVAILABLE_MEMORY_SIZE
  13 L2_LOG2_LINE_SIZE
  14 L2_LOG2_CACHE_SIZE
  15 L2_NUM_L2_SLICES

Tiler:
  16 TILER_BIN_SIZE_BYTES
  17 TILER_MAX_ACTIVE_LEVELS

Implementation:
  24 IMPL_TECH

Raw registers:
  25-32 RAW_*_PRESENT and RAW_*_FEATURES
  33 RAW_AS_PRESENT
  34 RAW_JS_PRESENT
  35-50 RAW_JS_FEATURES_0 through RAW_JS_FEATURES_15
  51 RAW_TILER_FEATURES
  52-54 RAW_TEXTURE_FEATURES_0 through RAW_TEXTURE_FEATURES_2
  55 RAW_GPU_ID
  56 RAW_THREAD_MAX_THREADS
  57 RAW_THREAD_MAX_WORKGROUP_SIZE
  58 RAW_THREAD_MAX_BARRIER_SIZE
  59 RAW_THREAD_FEATURES
  60 RAW_COHERENCY_MODE

Coherency:
  61 COHERENCY_NUM_GROUPS
  62 COHERENCY_NUM_CORE_GROUPS
  63 COHERENCY_COHERENCY
  64-79 COHERENCY_GROUP_0 through COHERENCY_GROUP_15

Misc:
  82 NUM_EXEC_ENGINES
  83 RAW_THREAD_TLS_ALLOC
  84 TLS_ALLOC
  85 RAW_GPU_FEATURES

## Processed structures

The full property buffer is unpacked into base_gpu_props, which contains:

mali_base_gpu_core_props     product id, revisions, frequency, texture
mali_base_gpu_l2_cache_props L2 line size, cache size, slice count
mali_base_gpu_tiler_props    bin size, max active levels
mali_base_gpu_thread_props   thread limits, register file size
gpu_raw_gpu_props            all raw register values
mali_base_gpu_coherent_group_info  coherent group masks

## GPU identification

The GPU ID register packs several fields:

Bits  0-3   version status
Bits  4-11  version minor
Bits 12-15  version major
Bits 16-31  product ID (old format)

The new format (GPU_ID2) uses these bit positions:

Bit  0-3   version status
Bit  4-11  version minor
Bit  12-15 version major
Bit  16-19 product major
Bit  20-23 arch revision
Bit  24-27 arch minor
Bit  28-31 arch major

Helper macros are provided in gpu/mali_kbase_gpu_id.h.

Known product models for Valhall architecture (arch_major = 10):
  GPU_ID2_PRODUCT_TGRX  = make(10, 3)
  GPU_ID2_PRODUCT_TVAX  = make(10, 4)
  GPU_ID2_PRODUCT_LODX  = make(10, 7)

Newer GPUs, including the target G615, are not enumerated in the current
header. The actual value must be read from RAW_GPU_ID and compared against
the arch_major field.

## CSF global interface

KBASE_IOCTL_CS_GET_GLB_IFACE returns:

glb_version       CSF interface version
features          bitmask of CSF capabilities
group_num         number of CSG slots supported
total_stream_num  total number of CSIs across all groups
prfcnt_size       performance counter data sizes
instr_features    instrumentation features

This ioctl determines how many groups and streams Manvil can create on the
target device.
