# Tipu Framework

Tipu is a lightweight rendering framework for Vulkan, designed to mimic SDL GPU and meta's Vulkan wrappers, it is
focused on ease of use and modern Vulkan features.

You can load and display models in less than 200 lines of code with this, chain up different render passes and
attachments.

<p align="center">
<b>**Work In Progress**</b>
</p>
  
---

## Features

- Context management.
- Pipeline and layout builder helpers.
- Image layout state presistence.
- **Dynamic rendering**.
- **Buffer Device Address** (BDA) based push constant.
- **Bindless textures** support.
- Model loader helper.
- Material system.
- ImGui support.
- MSAA support.
- PBR examples.

## Technology

**Languages**: C++ 20, GLSL  
**Build System**: CMake  
**Asset Management**: [TinyGLTF](https://github.com/syoyo/tinygltf), [Shader-C](https://github.com/google/shaderc)  
**Third-Party**: [Volk](https://github.com/zeux/volk), [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator), [SDL3](https://github.com/libsdl-org/SDL), [GLM](https://github.com/g-truc/glm), [ImGui](https://github.com/ocornut/imgui)

---

## Examples

<p align="center">
<b>Animation</b>
</p>
<p align="center">
<img src="docs/Animator.gif" alt="Animation"/>
</p>

<p align="center">
<b>PBR</b>
</p>
<p align="center">
<img src="docs/PBRExample.jpg" alt="PBR"/>
</p>

<p align="center">
<b>Multi-Mesh Import</b>
</p>
<p align="center">
<img src="docs/TankExample.jpg" alt="Tank"/>
</p>

<p align="center">
<b>Wireframe</b>
</p>
<p align="center">
<img src="docs/WireframeExample.jpg" alt="Toy"/>
</p>

<p align="center">
<b>ImGui</b>
</p>
<p align="center">
<img src="docs/ImGuiExample.jpg" alt="UI"/>
</p>

<p align="center">
<b>Cubemap</b>
</p>
<p align="center">
<img src="docs/CubemapExample.jpg" alt="Cubemap"/>
</p>
