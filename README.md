# ren



## Ubuntu Deps
```bash
sudo apt install libvulkan-dev libfmt-dev libspirv-dev glslang-tools spirv-headers
```


## Fedora Deps
```bash
sudo dnf install cmake ninja luajit-devel SDL2-devel
```

# Building on windows documentation
## Windows Sucks for Developers.

I haven't done this before, so here we go.
- I installed visual studio code
- Vulkan SDK from [here](https://vulkan.lunarg.com/sdk/home#windows)
- I assume I have to install cmake
- I assume I'll have to install msvc somehow
- Installed MSVC from [here](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)


- `vcpkg install sdl2[vulkan]`

```json
{
    "cmake.configureArgs": [
    ],
    "cmake.configureSettings": {
        "CMAKE_TOOLCHAIN_FILE": "c:\\Users\\nick\\dev\\vcpkg\\scripts\\buildsystems\\vcpkg.cmake",
        "VULKAN_SDK": "C:\\VulkanSDK\\1.4.321.1",
        "CMAKE_BUILD_PARALLEL_LEVEL": "12"
    }
}
```

Trying this in visual studio.