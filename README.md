# Background

## Features

## Features

- Vulkan 1.4 GPU-Driven renderer
- GPUAddress table enables a 100% bindless indirect buffer system
- Batched indirect instancing via `vkCmdDrawIndexedIndirect`
- Multithreaded asset & command preparation (EnkiTS)
- Descriptor indexing (bindless rendering)
- Transfer and compute async enabled
- PBR shading (Cook-Torrance BRDF, GGX, Schlick-GGX, Unreal Fresnel)
- Full IBL support (irradiance map + specular prefilter + BRDF LUT)
- GLTF loading (with AABB bounds generation)
- AABB frustum culling
- Transparent depth sorting
- ImGui debugging tools
- MSAA (up to 8x), mipmapping, dynamic pipeline swapping

## Future
-GPU frustum culling
-GPU batching and sorting
-Cascade shadow mapping
-Clustered shading

## Screenshots


## Controls
- `W A S D` ‐ Move forward, left, back, right  
- `Space` ‐ Move up  
- `Ctrl` ‐ Move down  
- `Mouse (Left Click + Move)` ‐ Look around  
- `R` ‐ Reset camera to spawn/origin  
- `Tab` ‐ Toggle ImGui editor setting  
- `P` ‐ Toggle rendering stats  
- `Esc` ‐ Exit application

## Requirements for build
- Windows 10+
- Vulkan SDK (1.4+)
- CMake 3.20+
- Visual Studio 2022

## Build steps
```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release/Debug