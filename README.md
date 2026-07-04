# Sandbox

Tipu is a lightweight rendering abstraction for Vulkan, designed to mimic SDL GPU and meta's Vulkan wrappers, it is
focused on ease of use and modern Vulkan features.  

<p style="text-align: center">
<b>**Work In Progress**</b>
</p>
  
---

## Features

- Vulkan context class which handles window management and swapchain.
- Pipeline and Layout builder helpers.
- Image layout state presistence.
- **Dynamic rendering**.
- **Buffer Device Address** (BDA) based push constant.
- **Bindless textures** support.

## Technology

**Languages**: C++ 20, GLSL  
**Build System**: CMake  
**Asset Management**: [TinyGLTF](https://github.com/syoyo/tinygltf), [Shader-C](https://github.com/google/shaderc)  
**Third-Party**: [Volk](https://github.com/zeux/volk), [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator), [SDL3](https://github.com/libsdl-org/SDL), [GLM](https://github.com/g-truc/glm)

---

## Examples

<p style="text-align: center">
<b>Triangle</b>
</p>

<p style="text-align: center">
<b>Model Loader</b>
</p>

<p style="text-align: center">
<b>Skybox</b>
</p>
