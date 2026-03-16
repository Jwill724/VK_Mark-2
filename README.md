## Features
- Vulkan 1.4 Hybrid CPU/GPU-Driven forward+ renderer
- GPUAddress table enables a 100% bindless indirect buffer system
- Batched indirect instancing via `vkCmdDrawIndexedIndirect`
- Clustered shading
- Descriptor indexing (bindless rendering)
- PBR + IBL: Cook–Torrance GGX with Disney diffuse; split-sum IBL (prefiltered spec + BRDF LUT, irradiance)
- GLTF asset pipeline enabled (kinda) with multithreading (EnkiTS)
- AABB BVH culling and OBB visual debug
- Transparent depth sorting
- ImGui debugging tools
- SMAA
- CMAA2
- FXAA
- Cascaded shadow mapping via atlas
- Flashlight with shadow map
- Screen space contact shadows (Bend Studios)
- GTAO (with bent normals)
- Ray marched directional volumetric lights
- Tonemapping (ACES Film)
- Push descriptors
- HI-Z Generation (Depth mip pyramid)
- Lens Flare
- Chromatic Aberration
- Mesh optimizer
- Tracy profiler

## Future
-Render graph
-Better asset management (dynamic asset loading, resource handling)
-KTX texture format
-Full compute async
-Deeper multithreading (texture loading, cmd recording)
-Occlusion culling
-GPU frustum culling
-GPU batching and sorting

## Screenshots
![Sponza](res/screenshots/sponza.png)
![Bistro](res/screenshots/bistro.png)
![Helmet](res/screenshots/helmet.png)
![1000 Ducks](res/screenshots/ducks.png)

## Controls
- `W A S D` ‐ Move forward, left, back, right
- `Space` ‐ Move up
- `Ctrl` ‐ Move down
- `Mouse (Left Click + Move)` ‐ Look around
- `R` ‐ Reset camera to spawn/origin
- `Tab` ‐ Toggle ImGui editor setting
- `P` ‐ Toggle ImGui stats profiling window
- `F` ‐ Toggle flashlight
- `Esc` ‐ Exit application

## Requirements for build
- Windows 10+
- Vulkan SDK (1.4+)
- Visual Studio 2022

## Build steps
Open project file in visual studio 2022
Cmake to be utilized in future, doesn't currently work.

For Bistro asset (tree is busted)
https://www.dropbox.com/scl/fi/aozfte8k1aewhpl7omx3r/Bistro.glb?rlkey=wmy6ep2yezcs77bidxwqeeiec&st=ztk7qxun&dl=0