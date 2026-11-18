#include <vulkan/vulkan.h>

#include <stdio.h>

#define MAX_DEVICES 16

int main() {
  uint32_t version = 0;
  vkEnumerateInstanceVersion(&version);
  printf("vulkan %u.%u.%u\n", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version));

  VkInstanceCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &(VkApplicationInfo) {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "devices",
      .apiVersion = VK_API_VERSION_1_0,
    },
  };
  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&info, NULL, &instance) != VK_SUCCESS) {
    printf("no vulkan driver\n");
    return 1;
  }

  VkPhysicalDevice devices [MAX_DEVICES] = {0};
  uint32_t count = MAX_DEVICES;
  vkEnumeratePhysicalDevices(instance, &count, devices);
  for (uint32_t it = 0; it < count; it++) {
    VkPhysicalDeviceProperties properties = {0};
    vkGetPhysicalDeviceProperties(devices[it], &properties);
    printf("%s\n", properties.deviceName);
  }

  vkDestroyInstance(instance, NULL);
  return 0;
}
