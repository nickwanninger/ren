#include <ren/Vulkan.h>

#include "vkb/VkBootstrap.h"


ren::VulkanSwapchain::VulkanSwapchain(ren::VulkanInstance &vulkan_instance)
    : vulkan_instance(vulkan_instance) {
  // get the width of vulkan_instance.window from SDL
  int width, height;
  SDL_Vulkan_GetDrawableSize(vulkan_instance.window, &width, &height);
}


ren::VulkanSwapchain::~VulkanSwapchain() {
}
