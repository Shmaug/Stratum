# Stratum

High performance modular plugin-based Vulkan rendering engine in C++17 with minimal dependencies.

## How to Build
run `setup.bat` or `setup.sh`, then build Stratum with CMake

# Rendering Overview
## Note: Stratum uses a **left-handed** coordinate system!
- `Scene` object
  - Stores a collection of Objects
    - See `Scene::AddObject()` and `Scene::RemoveObject()` (objects wont work unless they are first added to the scene)
  - Computes a binary BVH for the whole scene
    - Allows for raycasting for objects that implement `Object::Intersect()`
  - Computes active lights and renders shadows, which can be referenced with `Scene::LightBuffer()`, `Scene::ShadowBuffer()`, and `Scene::ShadowAtlas()`
  - Use `Scene::LoadModelScene()` to efficiently load multiple `MeshRenderer`s (or `SkinnedMeshRenderer`s) from one 3D file
    - Stores all vertices and indices in the same buffer
  - Computes timing
    - `Scene::TotalTime()`: Total time in seconds since Stratum has started
    - `Scene::DeltaTIme()`: Delta time in seconds between last frame and the current frame
- `Object`
  - Base class for all Scene Objects. Stores a position, rotation, and scale that is used to compute an object-to-parent matrix (see `Object::ObjectToParent()`). Objects can have other Objects within them as children, allowing for hierarchical scene graphs.
  - Almost completely virtual, designed to be inhereted from
  - Supports layer masking with `Object::LayerMask()` to classify different objects
- `Camera`
  - Represents a camera in 3D space. Provides functionality for stereo rendering, and more
  - Inherets `Object`. Computes View matrices based on its transform as an `Object`
  - **Note: For precision, Camera view matrices are always centered at the origin.**
- `Renderer`
  - Base class for all Renderers. Inherit and override this to implement a custom renderer.
- `MeshRenderer`
  - Renders a `Mesh` with a `Material`.
- `SkinnedMeshRenderer`
  - Renders a skinned `Mesh` with a `Material`. Computes skinning from an `AnimationRig`.
- `ClothRenderer`
  - Renders a `Mesh` simulating soft-body spring physics along triangle edges, and aerodynamic drag along triangle faces.
- `Light`
  - Defines a light using the built-in lighting system.
- `GUI`
  - Provides a basic immediate-mode GUI system, similar to Unity's EditorGUI. It can draw GUI content in world-space and in screen-space.

# Content Overview
- `Asset`
  - Represents any Asset loadable by the `AssetManager`
  - Used internally by the `AssetManager`
  - Inherit this if you intend to implement a custom Asset type (custom asset loaders within `AssetManager` planned)
- `AssetManager`
  - Loads and tracks Assets. Assets loaded using this do **not** have to be manually deleted. Use this to load common assets.
- `Mesh`
  - Stores a collection of vertices and indices on the GPU
    - This means vertices and indices are not accessible unless otherwise stored
  - Also stores weight data and shape key data for animations
  - Also can store a triangle BVH (for raycasting)
  - Static functions for creating cubes and planes (`Mesh::CreateCube()` and `Mesh::CreatePlane()`)
- `Font`
  - Represents a rasterized TrueType (*.ttf) font at a specific pixel size
  - Can draw strings in the world or on the screen with `DrawString()` or `DrawString()`
- `Texture`
  - Stores a texture on the GPU
  - Can compute mipmaps in the constructor
- `Shader`
  - Represents a shader compiled with Stratums ShaderCompiler. The ShaderCompiler uses reflection to determine the layout, passes, and other metadata included within shaders
  - Stores both compute and graphics shaders
  - Use `GetGraphics()` and `GetCompute()` to get usable shader *variants*.
- `Material`
  - Represents a Shader with a collection of parameters. Used by `MeshRenderer`

# Shader Overview
Stratum provides a custom shader compiler. It uses SPIRV reflection and custom directives to support automatic generation of data such as descriptor and pipeline layouts. It also provides macro variants, similar to Unity. Here is a list of all supported directives:
- `#pragma vertex <function> <main/depth>`
  - Specifies vertex shader entrypoint. `pass` is optional, and defaults to `main`
- `#pragma fragment <function> <main/depth>`
  - Specifies fragment shader entrypoint. `pass` is optional, and defaults to `main`
- `#pragma kernel <function>`
  - Specifies a kernel entrypoint for a compute shader
- `#pragma multi_compile <keyword1> <keyword2> ...`
  - Specifies shader *variants*. The shader is compiled multiple times, defining a different keyword each time. These different compilations are referred to as *variants*.
- `#pragma render_queue <number>`
  - Specifies the render queue to be used by this shader. Used by `Material`.
- `#pragma color_mask <mask>`
  - Specifies a color write mask for the fragment shader. `<mask>` must contain subset of the characters `rgba`. Examples:
    - `#pragma color_mask rgb`
    - `#pragma color_mask rgba`
    - `#pragma color_mask rg`
- `#pragma zwrite <true/false>`
  - Specifies whether this shader writes to the zbuffer
- `#pragma ztest <true/false>`
  - Specifies whether this shader uses depth-testing
- `#pragma depth_op <less/greater/lequal/gequal/equal/nequal/never/always`>`
  - Specifies the depth compare op.
- `#pragma cull <front/back/false>`
  - Specifies the culling mode
- `#pragma fill <solid/line/point>`
  - Specifes the fill mode
- `#pragma blend <opaque/alpha/add/multiply>`
  - Specifies the blend mode
- `#pragma array <name> <number>`
  - Specifies that the descriptor named `<name>` is an array of size `<number>`. This is used in addition to specifying the descriptor as an array in native syntax (GLSL or HLSL)
- `#pragma static_sampler <name> <magFilter=linear> <minFilter=linear> <filter=linear> <addressModeU=repeat> <addressModeV=repeat> <addressModeW=repeat> <addressMode=repeat> <maxAnisotropy=2> <borderColor=int_opaque_black> <unnormalizedCoordinates=false> <compareOp=always> <mipmapMode=linear> <minLod=0> <maxLod=12> <mipLodBias=0>`
  - Specifies that the sampler descriptor named `<name>` is a static/immutable sampler. All arguments after `<name>` are optional and defaulted to the above values, and can be specified as `argument=value` Examples:
    - `#pragma static_sampler ShadowSampler maxAnisotropy=0 maxLod=0 addressMode=clamp_border borderColor=float_opaque_white compareOp=less`

## Stereo Rendering
Cameras have a `StereoMode` property which is implemented by rendering twice, calling `Camera::SetStereo()` to set the left/right eyes before rendering each time