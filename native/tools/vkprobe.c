/* vkprobe — glibc aarch64 sanity check: Vulkan loader + ICD (Turnip/KGSL) from the app process. */
#include <stdio.h>
#include <vulkan/vulkan.h>

int main(void) {
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "vkprobe",
                              .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    VkResult r = vkCreateInstance(&ci, NULL, &inst);
    if (r != VK_SUCCESS) { printf("vkCreateInstance failed: %d\n", r); return 1; }

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, NULL);
    VkPhysicalDevice devs[8];
    if (n > 8) n = 8;
    vkEnumeratePhysicalDevices(inst, &n, devs);
    printf("physical devices: %u\n", n);
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devs[i], &p);
        printf("  [%u] %s  api %u.%u.%u  driver 0x%x\n", i, p.deviceName, VK_VERSION_MAJOR(p.apiVersion),
               VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion), p.driverVersion);
    }
    uint32_t ne = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &ne, NULL);
    VkExtensionProperties ext[64];
    if (ne > 64) ne = 64;
    vkEnumerateInstanceExtensionProperties(NULL, &ne, ext);
    printf("instance extensions:");
    for (uint32_t i = 0; i < ne; i++) printf(" %s", ext[i].extensionName);
    printf("\n");
    vkDestroyInstance(inst, NULL);
    return 0;
}
