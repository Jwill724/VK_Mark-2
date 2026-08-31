## Features
- Vulkan 1.4 GPU-Driven renderer
- Mesh shaders pipeline with visibility buffer
- GPUAddress table enables a 100% bindless indirect buffer system
- GPU culling and draw building
- Ray traced reflections and soft sun shadows, NRD denoiser
- Clustered shading(area lights, spot lights, point lights)
- SSGI through visibility bitmask ao with indirect lighting (XeGTAO based)
- Descriptor indexing (bindless rendering)
- PBR + IBL: Cook–Torrance GGX with Disney diffuse; split-sum IBL (prefiltered spec + BRDF LUT, irradiance)
- Multithreaded job system (EnkiTS)
- Retained mode render graph
- Async compute with multithreaded secondary recording
- GLTF 2.0 cached asset pipeline
- Block texture compression support
- Debug draw view (OBBs, wireframe, etc)
- Forward+ transparent rendering
- Order-independent-transparency (OIT)
- ImGui debugging tools
- Cascaded shadow mapping (PCF/PCSS filtering)
- Flashlight with shadow map
- Temporal Anti Aliasing
- Screen space contact shadows (Bend Studios)
- Ray marched directional volumetric lights
- Tonemapping (ACES Film)
- Push descriptors handling runtime render targets
- HI-Z Generation (Depth mip pyramid)
- Lens Flare
- Chromatic Aberration (Spartan Engine implementation)
- Bloom (Spartan Engine implementation)
- Mesh optimizer
- Tracy profiler

- Legacy systems
- SMAA
- CMAA2 (Intel)
- FXAA


## Future
-Runtime asset loading and handling
-Dynamic meshes, material and light interactions
-Auto Exposure
-Grand Turismo 7 tonemapping
-Physically based light units
-Atmospheric scattering sky with sun
-Planar reflections
-Parallax corrected cubemaps
-Water shaders
-Voxel volumetrics
-Make TAA work well
-Improvements to lens flare quality
-RTGI

## Screenshots
![Sponza](res/screenshots/sponza.png)
![Bistro](res/screenshots/bistro.png)
![SanMiguel](res/screenshots/helmet.png)
![200k Ducks](res/screenshots/ducks.png)

## Controls
- `W A S D` ‐ Move forward, left, back, right
- `Space` ‐ Move up
- `Ctrl` ‐ Move down
- `Mouse (Right Click + Move)` ‐ Look around
- `R` ‐ Reset camera to spawn/origin
- `Tab` ‐ Toggle ImGui editor setting
- `P` ‐ Toggle ImGui stats profiling window
- `F` ‐ Toggle flashlight
- `Esc` ‐ Exit application

## Requirements for build
- Windows 10/11
- Miniumum GPU 20 series+ or RDNA2+
- Vulkan SDK (1.4+)
- Visual Studio 2026

## Build steps
Open project file in visual studio 2026
Cmake to be utilized in future, doesn't currently work.

Asset downloads (Bistro.glb, Bistro.gltf, San-Miguel.gltf)
https://www.dropbox.com/scl/fo/2h6jnyho16z7w0lpjah9w/AIV4BUCfhIbN1sC1hEYl8BI?rlkey=4zdox8dw65t7n5hejpvm4nepz&st=q4nrzvub&dl=0

Download for Intel Sponza, just load in gltf.
https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html