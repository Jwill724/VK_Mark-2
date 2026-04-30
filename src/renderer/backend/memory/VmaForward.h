#pragma once

// VMA handle forward declarations — avoids pulling in vk_mem_alloc.h
// These are opaque pointer types identical to VMA's own typedefs
struct VmaAllocator_T;
struct VmaAllocation_T;

using VmaAllocator  = VmaAllocator_T*;
using VmaAllocation = VmaAllocation_T*;
