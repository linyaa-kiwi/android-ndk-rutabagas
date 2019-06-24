// Copyright 2019 Google Inc
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of Google Inc. nor the names of its contributors may be
//    used to endorse or promote products derived from this software without
//    specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// stdlib
#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Linux
#include <sys/types.h>
#include <unistd.h>

// Vulkan
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_android.h>

// Android
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

// local
#include "util/alloc.h"
#include "util/check.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/ru_chan.h"
#include "util/ru_queue.h"
#include "util/ru_thread.h"

#include "ru_rend.h"

#define RU_FMT_MASK32 "#08x"
#define RU_FMT_MASK64 "#016" PRIx64
#define RU_FMT_VK_FLAGS RU_FMT_MASK32

typedef struct RuInstance {
    VkInstance vk;
    VkDebugReportCallbackEXT debug_report_cb;
    PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT;
    PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT;
    PFN_vkGetPhysicalDeviceFeatures2KHR vkGetPhysicalDeviceFeatures2KHR;
    PFN_vkGetPhysicalDeviceProperties2KHR vkGetPhysicalDeviceProperties2KHR;
    PFN_vkGetPhysicalDeviceImageFormatProperties2KHR vkGetPhysicalDeviceImageFormatProperties2KHR;

    // TODO: Move these to RuDevice
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID;
    PFN_vkCreateSamplerYcbcrConversionKHR vkCreateSamplerYcbcrConversionKHR;
    PFN_vkCmdPushDescriptorSetKHR vkCmdPushDescriptorSetKHR;
} RuInstance;

typedef struct RuPhysicalDevice {
    RuInstance *inst _not_owned_;
    VkPhysicalDevice vk;
    uint32_t index; // from vkEnumeratePhysicalDevices
    uint32_t avail_ext_count;
    VkExtensionProperties *avail_ext_props;
    VkPhysicalDeviceProperties props;
    VkPhysicalDevicePushDescriptorPropertiesKHR push_desc_props;
    VkPhysicalDeviceMemoryProperties mem_props;
    uint32_t queue_fam_count;
    VkQueueFamilyProperties *queue_fam_props; // length is queue_fam_count;
} RuPhysicalDevice;

typedef struct RuSurface {
    RuPhysicalDevice *phys_dev _not_owned_;
    VkSurfaceKHR vk;
    ANativeWindow *window _not_owned_;
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR *formats;
    uint32_t format_count;
    VkBool32 *queue_fam_support; // length is RuPhysicalDevice::queue_fam_count
} RuSurface;

typedef struct RuDevice {
    // Not much here...
    RuPhysicalDevice *phys_dev _not_owned_;
    VkDevice vk;
} RuDevice;

// Resources for the scene that are specific to each AHB.
typedef struct RuAhb {
    AHardwareBuffer *ahb;
    VkDeviceMemory mem;
    VkImage image;
    VkImageView image_view;
    VkSamplerYcbcrConversionKHR sampler_ycbcr_conv;
    VkSampler sampler;

    // When using VkSamplerYcbcrConversionKHR, the Vulkan spec requires that
    // the VkDescriptorSetLayoutBinding use use an immutable
    // combined-image-sampler.
    VkDescriptorSetLayout desc_set_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;

    // If non-null, the AImage holds a reference to the AHB.
    AImage *aimage;

    // RuRend::aimage_reader holds a reference to the AHB. Therefore the AHB
    // may continue to receive updates from the media decoder.
    bool in_aimage_reader;
} RuAhb;

typedef struct RuAImageHeap {
    AImageReader *aimage_reader;

    // Incremented by AImageReader_ImageListener::onImageAvailable.
    struct {
        pthread_mutex_t mutex;
        pthread_cond_t cond;
        uint32_t count;
    } aimage_available;
} RuAImageHeap;

typedef struct RuAhbCache {
    // A slot is valid iff RuAhb::ahb is non-null.
    RuAhb slots[64];
} RuAhbCache;

typedef struct RuSwapchain {
    RuDevice *dev _not_owned_;
    VkSwapchainKHR vk;
    VkExtent2D extent;
    uint32_t len;
    VkImage *images _not_owned_;
    uint32_t queue_fam_index;
    VkResult status; // set by vkQueuePresentKHR
} RuSwapchain;

// Container for all resources needed to record a frame's command buffer.
//
// It owns the resources dependent on the swapchain, such as VkFramebuffer. It
// merely references the resources independent of the swapchain, such as the
// RuAhb.
typedef struct RuFrame {
    bool is_reset;

    // Persistent Data
    // ---------------
    // These members are initialized along with the struct, and share their
    // lifetime with the struct.

    // The swapchain image in `framebuffer`.
    uint32_t swapchain_image_index;
    VkImage swapchain_image _not_owned_;
    VkImageView swapchain_image_view;

    VkCommandBuffer cmd_buffer;
    VkFramebuffer framebuffer;
    VkExtent2D extent;

    // Releases `cmd_buffer`.
    VkFence release_fence;
    VkSemaphore release_sem;

    // Acquired Data
    // -------------
    // These members are freshly set each time the frame is acquired.

    // Received from RuMedia when a new media frame is available.
    RuAhb *rahb;
} RuFrame;

// All child resources use the same queue family as the
// swapchain, RuSwapchain::queue_fam_index.
typedef struct RuFramechain {
    RuSwapchain *swapchain _not_owned_;
    VkFence swapchain_fence;
    RuFrame *frames; // length is swapchain->len
    RuQueue submitted_frames;
} RuFramechain;

typedef enum RuRendEventType {
    RU_REND_EVENT_START,
    RU_REND_EVENT_STOP,
    RU_REND_EVENT_PAUSE,
    RU_REND_EVENT_UNPAUSE,
    RU_REND_EVENT_BIND_WINDOW,
    RU_REND_EVENT_UNBIND_WINDOW,
    RU_REND_EVENT_AIMAGE_BUFFER_REMOVED,
} RuRendEventType;

typedef struct RuRendEvent {
    RuRendEventType type;

    union {
        struct {
            AImageReader *aimage_reader;
        } start;

        struct {
            ANativeWindow *window;
        } bind_window;

        struct {
            AHardwareBuffer *ahb;
        } aimage_buffer_removed;
    };
} RuRendEvent;

typedef struct RuRend {
    RuInstance inst;
    RuPhysicalDevice phys_dev;
    RuDevice dev;
    RuRendUseExternalFormat use_ext_format;

    // For simplicity, we use one VkQueue and one VkCommandPool.
    uint32_t queue_fam_index;
    VkQueue queue;

    VkCommandPool cmd_pool;
    VkRenderPass render_pass;
    VkShaderModule vert_module;
    VkShaderModule frag_module;

    // Lifetime is that of app's ANativeWindow.
    RuSurface *surf;

    // We create/destroy these in response to window events and to errors from
    // vkQueuePresentKHR.
    RuSwapchain *swapchain;
    RuFramechain *framechain;

    RuAhbCache ahb_cache;
    RuAImageHeap aimage_heap; // valid iff RuAImageHeap::aimage_reader != NULL

    RuChan event_chan;
    pthread_t thread; // See ru_rend_thread().
} RuRend;

// Use the driver's default allocator.
static const VkAllocationCallbacks *ru_alloc_cb = NULL;

// For vkCmdPushDescriptorSetKHR.
static const uint32_t need_push_descs = 1;

// For simplicity, hard-code the format we use for presentation.
static const VkSurfaceFormatKHR ru_present_format = {
    .format = VK_FORMAT_R8G8B8A8_UNORM,
    .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
};

static void *ru_rend_thread(void *_rend);

static void __attribute__((sentinel))
ru_chain_vk_structs(void *s, ...) {
    struct vk_struct {
        VkStructureType sType;
        void *pNext;
    };

    struct vk_struct *next = s;

    va_list va;
    va_start(va, s);

    while (next) {
        next->pNext = va_arg(va, typeof(next));
        next = next->pNext;
    }

    va_end(va);
}

static bool _must_use_result_
ru_has_layer(const VkLayerProperties *props, uint32_t prop_count,
          const char *name)
{
    for (uint32_t i = 0; i < prop_count; ++i) {
        if (strcmp(props[i].layerName, name) == 0) {
            return true;
        }
    }

    return false;
}

static bool _must_use_result_
ru_has_extension(const VkExtensionProperties *props, uint32_t prop_count,
          const char *name)
{
    for (uint32_t i = 0; i < prop_count; ++i) {
        if (strcmp(props[i].extensionName, name) == 0) {
            return true;
        }
    }

    return false;
}

static bool _must_use_result_
ru_surface_format_eq(VkSurfaceFormatKHR a, VkSurfaceFormatKHR b) {
    return a.format == b.format &&
           a.colorSpace == b.colorSpace;
}

static uint32_t _must_use_result_
ru_find_surface_format(const VkSurfaceFormatKHR *haystack, uint32_t len,
        VkSurfaceFormatKHR needle)
{
    for (uint32_t i = 0; i < len; ++i) {
        if (ru_surface_format_eq(haystack[i], needle)) {
            return i;
        }
    }

    return UINT32_MAX;
}

static uint32_t _must_use_result_
ru_choose_queue_family(RuPhysicalDevice *phys_dev) {
    // From the Vulkan 1.1.11 spec:
    //
    //   On Android, all physical devices and queue families must be capable of
    //   presentation with any native window.
    //
    // Therefore we simply choose the first graphics queue.
    for (uint32_t i = 0; i < phys_dev->queue_fam_count; ++i) {
        if (phys_dev->queue_fam_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            return i;
        }
    }

    die("failed to find a graphics queue");
}

static int
ru_vk_debug_report_flags_to_android_log_level(VkDebugReportFlagsEXT flags) {
    if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        return ANDROID_LOG_ERROR;
    } else if (flags & (VK_DEBUG_REPORT_WARNING_BIT_EXT |
                        VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)) {
        return ANDROID_LOG_WARN;
    } else if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
        return ANDROID_LOG_INFO;
    } else if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        return ANDROID_LOG_DEBUG;
    } else {
        assert(!"bad VkDebugReportFlagsEXT");
        return ANDROID_LOG_VERBOSE;
    }
}

// Vulkan on android-armeabi-v7a uses a non-default calling convention, which
// we must use here to avoid compilation failure.
static VKAPI_PTR VkBool32
ru_vk_debug_report(
        VkDebugReportFlagsEXT flags,
        VkDebugReportObjectTypeEXT objectType,
        uint64_t object,
        size_t location,
        int32_t messageCode,
        const char *pLayerPrefix,
        const char *pMessage,
        void *pUserData) {
    int level = ru_vk_debug_report_flags_to_android_log_level(flags);

    __android_log_print(
        level,
        LOG_TAG,
        "vkDebug:0x%x:%i:0x%"PRIx64":%zu:%i:%s:%s",
        flags,
        objectType,
        object,
        location,
        messageCode,
        pLayerPrefix,
        pMessage);

    return VK_FALSE;
}

#define ru_instance_init_proc_addr(inst, name) \
    __ru_instance_init_proc_addr((inst), UNIQ(inst), name)

#define __ru_instance_init_proc_addr(inst, inst_uniq, name) ({ \
    let inst_uniq = (inst); \
    inst_uniq->name = (PFN_##name) vkGetInstanceProcAddr(inst_uniq->vk, #name); \
    if (!inst_uniq->name) { \
        die("vkGetInstanceProcAddr(\"%s\") failed", #name); \
    } \
})

static void
ru_instance_init(
        bool use_validation,
        RuInstance *inst)
{
    *inst = (RuInstance) {0};

    uint32_t layer_count = 0;
    check(vkEnumerateInstanceLayerProperties(&layer_count, NULL));

    VkLayerProperties layer_props[layer_count];
    check(vkEnumerateInstanceLayerProperties(&layer_count, layer_props));

    logd("Query Vulkan layers:");
    for (uint32_t i = 0; i < layer_count; ++i) {
        logd("    %s", layer_props[i].layerName);
    }

    static const char *validation_layers[] = {
        "VK_LAYER_GOOGLE_threading",
        "VK_LAYER_LUNARG_parameter_validation",
        "VK_LAYER_LUNARG_object_tracker",
        "VK_LAYER_LUNARG_core_validation",
        "VK_LAYER_GOOGLE_unique_objects",
    };

    const char **enable_layers = NULL;
    uint32_t enable_layer_count = 0;

    if (use_validation) {
        enable_layers = validation_layers;
        enable_layer_count = ARRAY_LEN(validation_layers);
    }

    logd("Enable Vulkan layers:");
    if (enable_layer_count == 0) {
        logd("    none");
    }
    for (uint32_t i = 0; i < enable_layer_count; ++i) {
        if (!ru_has_layer(layer_props, layer_count, enable_layers[i])) {
            die("Vulkan does not have layer %s", enable_layers[i]);
        }

        logd("    %s", enable_layers[i]);
    }

    uint32_t ext_count = 0;
    check(vkEnumerateInstanceExtensionProperties(/*layerName*/ NULL,
                &ext_count, NULL));

    VkExtensionProperties ext_props[ext_count];
    check(vkEnumerateInstanceExtensionProperties(/*layerName*/ NULL,
                &ext_count, ext_props));

    logd("Query Vulkan instance extensions:");
    for (uint32_t i = 0; i < ext_count; ++i) {
        logd("    %s", ext_props[i].extensionName);
    }

    static const char *enable_exts[] = {
        "VK_EXT_debug_report",
            // Requires:
            //    nothing

        "VK_KHR_surface",
            // Requires:
            //     nothing

        "VK_KHR_android_surface",
            // Requires:
            //     VK_KHR_surface

        "VK_KHR_external_memory_capabilities",
            // Requires:
            //     nothing

        "VK_KHR_get_physical_device_properties2",
            // Requires:
            //     nothing
    };

    static const uint32_t enable_ext_count =
        ARRAY_LEN(enable_exts);

    logd("Enable Vulkan instance extensions:");
    for (uint32_t i = 0; i < enable_ext_count; ++i) {
        if (!ru_has_extension(ext_props, ext_count, enable_exts[i])) {
            die("Vulkan does not have instance extension %s", enable_exts[i]);
        }

        logd("    %s", enable_exts[i]);
    }

    check(vkCreateInstance(
        &(VkInstanceCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .flags = 0,
            .pApplicationInfo = &(VkApplicationInfo) {
                .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                .pApplicationName = NULL,
                .applicationVersion = 0,
                .pEngineName = NULL,
                .engineVersion = 0,
                .apiVersion = VK_MAKE_VERSION(1, 0, 0),
            },
            .enabledLayerCount = enable_layer_count,
            .ppEnabledLayerNames = enable_layers,
            .enabledExtensionCount = ARRAY_LEN(enable_exts),
            .ppEnabledExtensionNames = enable_exts,
        },
        ru_alloc_cb,
        &inst->vk));

    ru_instance_init_proc_addr(inst, vkCreateDebugReportCallbackEXT);
    ru_instance_init_proc_addr(inst, vkDestroyDebugReportCallbackEXT);
    ru_instance_init_proc_addr(inst, vkGetAndroidHardwareBufferPropertiesANDROID);
    ru_instance_init_proc_addr(inst, vkGetPhysicalDeviceFeatures2KHR);
    ru_instance_init_proc_addr(inst, vkGetPhysicalDeviceProperties2KHR);
    ru_instance_init_proc_addr(inst, vkGetPhysicalDeviceImageFormatProperties2KHR);
    ru_instance_init_proc_addr(inst, vkCreateSamplerYcbcrConversionKHR);
    ru_instance_init_proc_addr(inst, vkCmdPushDescriptorSetKHR);

    check(inst->vkCreateDebugReportCallbackEXT(inst->vk,
        &(VkDebugReportCallbackCreateInfoEXT) {
            .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
            .flags = ~0,
            .pfnCallback = ru_vk_debug_report,
            .pUserData = NULL,
        },
        ru_alloc_cb,
        &inst->debug_report_cb));
}

static void
ru_instance_finish(RuInstance *inst) {
    if (inst->debug_report_cb)
        inst->vkDestroyDebugReportCallbackEXT(inst->vk, inst->debug_report_cb, ru_alloc_cb);
    vkDestroyInstance(inst->vk, ru_alloc_cb);
}

static void
ru_phys_dev_init(
        RuInstance *inst,
        RuPhysicalDevice *phys_dev)
{
    uint32_t phys_dev_count;
    check(vkEnumeratePhysicalDevices(inst->vk, &phys_dev_count, NULL));
    if (phys_dev_count == 0)
        die("no VkPhysicalDevice found");

    VkPhysicalDevice vk_phys_devs[phys_dev_count];
    check(vkEnumeratePhysicalDevices(inst->vk, &phys_dev_count, vk_phys_devs));

    logd("Query Vulkan physical devices:");
    for (uint32_t i = 0; i < phys_dev_count; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(vk_phys_devs[i], &props);

        uint32_t ext_count;
        check(vkEnumerateDeviceExtensionProperties(vk_phys_devs[i],
                /*layer*/ NULL, &ext_count, NULL));

        VkExtensionProperties ext_props[ext_count];
        check(vkEnumerateDeviceExtensionProperties(vk_phys_devs[i],
                /*layer*/ NULL, &ext_count, ext_props));

        logd("    VkPhysicalDevice %u :", i);
        logd("        apiVersion: %u.%u.%u",
                VK_VERSION_MAJOR(props.apiVersion),
                VK_VERSION_MINOR(props.apiVersion),
                VK_VERSION_PATCH(props.apiVersion));
        logd("        driverVersion: %u.%u.%u",
                VK_VERSION_MAJOR(props.driverVersion),
                VK_VERSION_MINOR(props.driverVersion),
                VK_VERSION_PATCH(props.driverVersion));
        logd("        vendorID: 0x%x", props.vendorID);
        logd("        deviceID: 0x%x", props.deviceID);
        logd("        deviceType: %d", props.deviceType);
        logd("        deviceName: %s", props.deviceName);
    }

    // Simply choose the first device.
    //
    // This is safe on Android Pie and earlier because there the loader supports at
    // most one VkPhysicalDevice.
    const uint32_t index = 0;
    VkPhysicalDevice vk_phys_dev = vk_phys_devs[0];

    uint32_t ext_count;
    check(vkEnumerateDeviceExtensionProperties(vk_phys_dev,
            /*layer*/ NULL, &ext_count, NULL));

    let ext_props = new_array(VkExtensionProperties, ext_count);
    check(vkEnumerateDeviceExtensionProperties(vk_phys_dev,
            /*layer*/ NULL, &ext_count, ext_props));

    VkPhysicalDeviceProperties2KHR props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR,
    };

    VkPhysicalDevicePushDescriptorPropertiesKHR push_desc_props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR,
    };

    ru_chain_vk_structs(
        &props2,
        &push_desc_props,
        NULL);

    inst->vkGetPhysicalDeviceProperties2KHR(vk_phys_dev, &props2);

    VkPhysicalDeviceFeatures2 features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    };

    VkPhysicalDeviceSamplerYcbcrConversionFeaturesKHR ycbcr_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,
    };

    ru_chain_vk_structs(
        &features,
        &ycbcr_features,
        NULL);

    inst->vkGetPhysicalDeviceFeatures2KHR(vk_phys_dev, &features);

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(vk_phys_dev, &mem_props);

    logd("Choose VkPhysicalDevice 0:");
    logd("    deviceExtensions:");
    for (uint32_t j = 0; j < ext_count; ++j) {
        logd("            %s", ext_props[j].extensionName);
    }

    logd("    memoryHeaps:");
    for (uint32_t i = 0; i < mem_props.memoryHeapCount; ++i) {
        VkMemoryHeap heap = mem_props.memoryHeaps[i];
        logd("        VkMemoryHeap %u :", i);
        logd("            size: %"PRIu64"B", heap.size);
        logd("            flags: %" RU_FMT_VK_FLAGS, heap.flags);
    }

    logd("    memoryTypes:");
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        VkMemoryType type = mem_props.memoryTypes[i];
        logd("        VkMemoryType %u :", i);
        logd("            propertyFlags: %" RU_FMT_VK_FLAGS, type.propertyFlags);
        logd("            heapIndex: %u", type.heapIndex);
    }

    uint32_t queue_fam_count;
    vkGetPhysicalDeviceQueueFamilyProperties(vk_phys_dev, &queue_fam_count,
            NULL);

    let queue_fam_props = new_array(VkQueueFamilyProperties, queue_fam_count);
    vkGetPhysicalDeviceQueueFamilyProperties(vk_phys_dev, &queue_fam_count,
            queue_fam_props);

    logd("    queueFamilyProperties:");
    for (uint32_t i = 0; i < queue_fam_count; ++i) {
        // Print only the info that affects our choice of queue family.
        VkQueueFamilyProperties p = queue_fam_props[i];
        logd("        [%u]:", i);
        logd("            queueFlags: %" RU_FMT_VK_FLAGS, p.queueFlags);
        logd("            queueCount: %u", p.queueCount);
    }

    logd("    samplerYcbcrConversion: %d", ycbcr_features.samplerYcbcrConversion);
    logd("    maxPushDescriptors: %u", push_desc_props.maxPushDescriptors);

    if (!ycbcr_features.samplerYcbcrConversion)
        die("VkPhysicalDevice lacks samplerYcbcrConversion");

    if (push_desc_props.maxPushDescriptors < need_push_descs) {
        die("VkPhysicalDevice does not support %u push descriptors",
            need_push_descs);
    }

    *phys_dev = (RuPhysicalDevice) {
        .inst = inst,
        .vk = vk_phys_dev,
        .index = index,
        .avail_ext_count = ext_count,
        .avail_ext_props = ext_props,
        .props = props2.properties,
        .push_desc_props = push_desc_props,
        .mem_props = mem_props,
        .queue_fam_count = queue_fam_count,
        .queue_fam_props = queue_fam_props,
    };
}

static void
ru_phys_dev_finish(RuPhysicalDevice *phys_dev) {
    free(phys_dev->queue_fam_props);
}

static RuSurface * _malloc_ _must_use_result_
ru_surface_new(
        RuPhysicalDevice *phys_dev,
        ANativeWindow *window)
{
    RuInstance *inst = phys_dev->inst;

    VkSurfaceKHR vk_surf;
    check(vkCreateAndroidSurfaceKHR(inst->vk,
        &(VkAndroidSurfaceCreateInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
            .flags = 0,
            .window = window,
        },
        ru_alloc_cb,
        &vk_surf));

    VkSurfaceCapabilitiesKHR caps;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_dev->vk, vk_surf,
                &caps));

    uint32_t format_count;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(phys_dev->vk, vk_surf,
                &format_count, NULL));
    if (format_count == 0)
        die("VkSurface has no available VkFormat");

    let formats = new_array(VkSurfaceFormatKHR, format_count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(phys_dev->vk, vk_surf,
                &format_count, formats));

    logd("Query Vulkan surface formats:");
    for (uint32_t i = 0; i < format_count; ++i) {
        VkSurfaceFormatKHR f = formats[i];

        logd("    VkSurfaceFormatKHR[%u]:", i);
        logd("        format: %d", f.format);
        logd("        colorSpace: %d", f.colorSpace);
    }

    uint32_t present_format_index =
        ru_find_surface_format(formats, format_count, ru_present_format);

    if (present_format_index == UINT32_MAX) {
        die("VkSurface does not support VkSurfaceFormat{%d, %d}",
                ru_present_format.format,
                ru_present_format.colorSpace);
    }

    logd("Choose VkSurfaceFormatKHR %u", present_format_index);

    logd("Query Vk queue family surface support:");
    let queue_fam_support = new_array(VkBool32, phys_dev->queue_fam_count);
    for (uint32_t i = 0; i < phys_dev->queue_fam_count; ++i) {
        check(vkGetPhysicalDeviceSurfaceSupportKHR(phys_dev->vk, i, vk_surf,
                    &queue_fam_support[i]));
        logd("        [%u]: surfaceSupport: %u", i, queue_fam_support[i]);
    }

    return new_init(RuSurface,
        .phys_dev = phys_dev,
        .vk = vk_surf,
        .window = window,
        .caps = caps,
        .formats = formats,
        .format_count = format_count,
        .queue_fam_support = queue_fam_support,
    );
}

static void
ru_surface_free(RuSurface *surf) {
    if (!surf)
        return;

    RuInstance *inst = surf->phys_dev->inst;

    vkDestroySurfaceKHR(inst->vk, surf->vk, ru_alloc_cb);
    free(surf->queue_fam_support);
    free(surf->formats);
    free(surf);
}

static void
ru_device_init(
        RuPhysicalDevice *phys_dev,
        RuDevice *dev)
{
    static const char *enable_exts[] = {
        "VK_KHR_swapchain",
            // Requires:
            //     i/VK_KHR_surface

        "VK_ANDROID_external_memory_android_hardware_buffer",
            // Requires:
            //     d/VK_KHR_sampler_ycbcr_conversion
            //     d/VK_EXT_queue_family_foreign
            //     d/VK_KHR_external_memory

        "VK_KHR_external_memory",
            // Requires:
            //     i/VK_KHR_external_memory_capabilities

        // WORKAROUND: VK_EXT_queue_family_foreign
        //
        // You may need to disable this because Intel forgot to implement it,
        // even though VK_ANDROID_external_memory_android_hardware_buffer
        // requires it.  Luckily, the implementation for Intel is one line of
        // code.
#if 0
        "VK_EXT_queue_family_foreign",
            // Requires:
            //     d/VK_KHR_external_memory
#endif

        "VK_KHR_sampler_ycbcr_conversion",
            // Requires:
            //     d/VK_KHR_maintenance1
            //     d/VK_KHR_bind_memory2
            //     d/VK_KHR_get_memory_requirements2
            //     i/VK_KHR_get_physical_device_properties2

        "VK_KHR_maintenance1",
            // Requires:
            //     nothing

        "VK_KHR_bind_memory2",
            // Requires:
            //     nothing

        "VK_KHR_get_memory_requirements2",
            // Requires:
            //     nothing

        "VK_KHR_push_descriptor",
            // Requires:
            //     i/VK_KHR_get_physical_device_properties2
    };

    logd("Enable Vulkan device extensions:");
    for (uint32_t i = 0; i < ARRAY_LEN(enable_exts); ++i) {
        if (!ru_has_extension(
                    phys_dev->avail_ext_props,
                    phys_dev->avail_ext_count,
                    enable_exts[i]))
        {
            die("Vulkan does not have device extension %s", enable_exts[i]);
        }

        logd("    %s", enable_exts[i]);
    }

    // Acquire exactly one VkQueue handle for each queue family.
    VkDeviceQueueCreateInfo queue_create_infos[phys_dev->queue_fam_count];
    for (uint32_t i = 0; i < phys_dev->queue_fam_count; ++i) {
            queue_create_infos[i] = (VkDeviceQueueCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .flags = 0,
                .queueFamilyIndex = i,
                .queueCount = 1,
                .pQueuePriorities = (float[]) { 1.0 },
            };
    }

    VkDevice vk_dev;
    check(vkCreateDevice(phys_dev->vk,
        &(VkDeviceCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = phys_dev->queue_fam_count,
            .pQueueCreateInfos = queue_create_infos,
            .enabledExtensionCount = ARRAY_LEN(enable_exts),
            .ppEnabledExtensionNames = enable_exts,
        },
        ru_alloc_cb,
        &vk_dev));

    *dev = (RuDevice) {
        .phys_dev = phys_dev,
        .vk = vk_dev,
    };
}

static void
ru_device_finish(RuDevice *dev)
{
    vkDestroyDevice(dev->vk, ru_alloc_cb);
}

static RuSwapchain * _must_use_result_ _malloc_
ru_swapchain_new(
        RuDevice *dev,
        RuSurface *surf,
        uint32_t queue_fam_index)
{
    const VkExtent2D extent = {
        .width = ANativeWindow_getWidth(surf->window),
        .height = ANativeWindow_getHeight(surf->window),
    };

    if (!surf->queue_fam_support[queue_fam_index]) {
        die("VkSurface does not support queue family %u",
                queue_fam_index);
    }

    const VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surf->vk,
        .minImageCount = surf->caps.minImageCount,
        .imageFormat = ru_present_format.format,
        .imageColorSpace = ru_present_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = (uint32_t[]) { queue_fam_index },
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = ({
            // Be sloppy. Choose any supported bit.
            assert(surf->caps.supportedCompositeAlpha != 0);
            1 << (__builtin_ffs(surf->caps.supportedCompositeAlpha) - 1);
        }),
        // The Vulkan spec requires that all surfaces support fifo mode.
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,

        .clipped = false,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    if ((info.imageUsage & ~surf->caps.supportedUsageFlags)) {
        die("VkSurface does not support VkImageUsageFlags(0x%08x)",
            info.imageUsage);
    }

    VkSwapchainKHR vk_swapchain;
    check(vkCreateSwapchainKHR(dev->vk, &info, ru_alloc_cb, &vk_swapchain));

    uint32_t len;
    check(vkGetSwapchainImagesKHR(dev->vk, vk_swapchain, &len, NULL));

    let images = new_array(VkImage, len);

    check(vkGetSwapchainImagesKHR(dev->vk, vk_swapchain, &len, images));

    return new_init(RuSwapchain,
        .dev = dev,
        .vk = vk_swapchain,
        .extent = extent,
        .len = len,
        .images = images,
        .queue_fam_index = queue_fam_index,
        .status = VK_SUCCESS,
    );
}

static void
ru_swapchain_free(RuSwapchain *swapchain)
{
    if (!swapchain)
        return;

    RuDevice *dev = swapchain->dev;

    vkDestroySwapchainKHR(dev->vk, swapchain->vk, ru_alloc_cb);
    free(swapchain->images);
    free(swapchain);
}

static void
ru_ahb_choose_image_creation_params(
        RuPhysicalDevice *phys_dev,
        uint32_t queue_fam_index,
        RuRendUseExternalFormat use_ext_format,
        AHardwareBuffer *ahb,
        const AHardwareBuffer_Desc *ahb_desc,
        const VkAndroidHardwareBufferPropertiesANDROID *ahb_props,
        const VkAndroidHardwareBufferFormatPropertiesANDROID *ahb_format_props,
        VkImageCreateInfo *out_image_create_info,
        VkExternalMemoryImageCreateInfo *out_ext_mem_image_create_info,
        VkExternalFormatANDROID *out_ext_format)
{
    RuInstance *inst = phys_dev->inst;
    VkResult vk_result;

    #define LOG_PREFIX "importing ahb %p: "

    // If using an external format, the spec requires that usage be exactly
    // VK_IMAGE_USAGE_SAMPLED_BIT. Luckily, that is the only bit this app
    // requires.
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;

    // Required for VK_IMAGE_USAGE_SAMPLED_BIT.
    if (!(ahb_desc->usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE)) {
        die(LOG_PREFIX "lacks AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE", ahb);
    }

    // On VkExternalFormatANDROID:
    //
    // When creating a VkImage for an AHB, the spec always allows using the
    // AHB's external format, and requires it when
    // VkAndroidHardwareBufferFormatPropertiesANDROID::format is undefined.

    if (ahb_format_props->format == VK_FORMAT_UNDEFINED) {
        logd(LOG_PREFIX "external format required", ahb);
        goto use_ext_format;
    }

    logd(LOG_PREFIX "external format not required", ahb);

    if (use_ext_format == RU_REND_USE_EXTERNAL_FORMAT_ALWAYS) {
        logd(LOG_PREFIX "use external format because user set useVkExternalFormat=always", ahb);
        goto use_ext_format;
    }

    logd(LOG_PREFIX "try non-external format", ahb);

    // On vkGetPhysicalDeviceImageFormatProperties2:
    //
    // When creating an AHB image with external format, the spec prohibits
    // calling vkGetPhysicalDeviceImageFormatProperties2. Instead, the app
    // proceeds directly from vkGetAndroidHardwareBufferProperties to
    // vkCreateImage.
    //
    // When creating an AHB image without external format, the spec requires
    // the following query before vkCreateImage:
    //
    //     vkGetPhysicalDeviceImageFormatProperties2KHR
    //         in:
    //             VkPhysicalDeviceImageFormatInfo2KHR
    //             VkPhysicalDeviceExternalImageFormatInfoKHR
    //         out:
    //             VkImageFormatProperties2KHR
    //             VkExternalImageFormatPropertiesKHR
    //             VkAndroidHardwareBufferUsageANDROID
    //             VkSamplerYcbcrConversionImageFormatPropertiesKHR
    //
    // For more details, see section "Image Creation Limits".

    // XXX: The spec is vague on how to choose the tiling for an AHB when using
    // a non-external format. This is likely an oversight of the
    // VK_ANDROID_external_memory_android_hardware_buffer spec.  To workaround
    // it, we play slot-machine with the tiling
    // vkGetPhysicalDeviceImageFormatProperties2KHR succeeds.
    static const VkImageTiling tiling_choices[] = {
        VK_IMAGE_TILING_LINEAR,
        VK_IMAGE_TILING_OPTIMAL,
    };

    // TODO: Prefer VK_IMAGE_CREATE_DISJOINT_BIT when supported.

    for (int t = 0; t < ARRAY_LEN(tiling_choices); ++t) {
        VkPhysicalDeviceImageFormatInfo2KHR image_format_info = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2_KHR,
            .format = ahb_format_props->format,
            .type = VK_IMAGE_TYPE_2D,
            .tiling = tiling_choices[t],
            .usage = usage,
            .flags = 0,
        };

        VkPhysicalDeviceExternalImageFormatInfoKHR ext_image_format_info = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO_KHR,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
        };

        ru_chain_vk_structs(
            &image_format_info,
            &ext_image_format_info,
            NULL);

        VkImageFormatProperties2KHR image_format_props = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2_KHR,
        };

        VkExternalImageFormatPropertiesKHR ext_image_format_props = {
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES_KHR,
        };

        VkAndroidHardwareBufferUsageANDROID ahb_buffer_usage = {
            .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID,
        };

        VkSamplerYcbcrConversionImageFormatPropertiesKHR sampler_ycbcr_conv_image_format_props = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES_KHR,
        };

        ru_chain_vk_structs(
            &image_format_props,
            &ahb_buffer_usage,
            &sampler_ycbcr_conv_image_format_props,
            &ext_image_format_props,
            NULL);

        logd(LOG_PREFIX "query config:", ahb);
        logd("    VkPhysicalDeviceImageFormatInfo2KHR:");
        logd("        format: %d", image_format_info.format);
        logd("        type: %d", image_format_info.type);
        logd("        tiling: %d", image_format_info.tiling);
        logd("        usage: %" RU_FMT_VK_FLAGS, image_format_info.usage);
        logd("        flags: %" RU_FMT_VK_FLAGS, image_format_info.flags);
        logd("    VkPhysicalDeviceExternalImageFormatInfoKHR:");
        logd("        handleType: %#x", ext_image_format_info.handleType);

        vk_result = inst->vkGetPhysicalDeviceImageFormatProperties2KHR(
                phys_dev->vk, &image_format_info, &image_format_props);

        switch (vk_result) {
            case VK_SUCCESS:
                break;
            case VK_ERROR_FORMAT_NOT_SUPPORTED:
                logd(LOG_PREFIX "query returned "
                    "VK_ERROR_FORMAT_NOT_SUPPORTED", ahb);
                goto try_again;
            default:
                die(LOG_PREFIX "vkGetPhysicalDeviceFeatures2KHR returned "
                    "unexpected " "VkResult(%d)", ahb, vk_result);
        }

        if (!(ext_image_format_props.externalMemoryProperties.externalMemoryFeatures &
                VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
        {
            die(LOG_PREFIX "config does not support"
                    "VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT", ahb);
            goto try_again;
        }

        logd(LOG_PREFIX "query success", ahb);

        *out_image_create_info = (VkImageCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags = image_format_info.flags,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = image_format_info.format,
            .extent = (VkExtent3D) {
                .width = ahb_desc->width,
                .height = ahb_desc->height,
                .depth = 1,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = image_format_info.tiling,
            .usage = image_format_info.usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = (uint32_t[]) { queue_fam_index },
            .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
        };

        *out_ext_mem_image_create_info = (VkExternalMemoryImageCreateInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
        };

        *out_ext_format = (VkExternalFormatANDROID) {
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
            .externalFormat = VK_FORMAT_UNDEFINED,
        };

        return;

 try_again:;
    }

    logd(LOG_PREFIX "all queries failed, fallback to external format", ahb);

 use_ext_format:
    if (use_ext_format == RU_REND_USE_EXTERNAL_FORMAT_NEVER)
        die(LOG_PREFIX "give up because user set useVkExternalFormat=never", ahb);

    *out_image_create_info = (VkImageCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,

        // When using an external format, the spec prohibits:
        //   VK_IMAGE_CREATE_DISJOINT_BIT
        //   VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT
        .flags = 0,

        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_UNDEFINED,
        .extent = (VkExtent3D) {
            .width = ahb_desc->width,
            .height = ahb_desc->height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, // required for external formats
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = (uint32_t[]) { queue_fam_index },
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };

    *out_ext_mem_image_create_info = (VkExternalMemoryImageCreateInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
    };

    *out_ext_format = (VkExternalFormatANDROID) {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
        .externalFormat = ahb_format_props->externalFormat,
    };

    #undef LOG_PREFIX
}


static void
ru_ahb_init(
        RuRend *rend,
        AHardwareBuffer *ahb,
        RuAhb *rahb)
{
    RuInstance *inst = &rend->inst;
    RuPhysicalDevice *phys_dev = &rend->phys_dev;
    RuDevice *dev = &rend->dev;

    AHardwareBuffer_acquire(ahb);

    AHardwareBuffer_Desc ahb_desc;
    AHardwareBuffer_describe(ahb, &ahb_desc);

    VkAndroidHardwareBufferPropertiesANDROID ahb_props = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
    };

    VkAndroidHardwareBufferFormatPropertiesANDROID ahb_format_props = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
    };

    ru_chain_vk_structs(
            &ahb_props,
            &ahb_format_props,
            NULL);

    check(inst->vkGetAndroidHardwareBufferPropertiesANDROID(dev->vk, ahb,
                &ahb_props));

    logd("importing ahb %p:", ahb);
    logd("    AHardwareBuffer_Desc:");
    logd("        width: %u", ahb_desc.width);
    logd("        height: %u", ahb_desc.height);
    logd("        layers: %u", ahb_desc.layers);
    logd("        format: %u", ahb_desc.format);
    logd("        usage: %" RU_FMT_MASK64, ahb_desc.usage);
    logd("        stride: %u", ahb_desc.stride);
    logd("    VkAndroidHardwareBufferPropertiesANDROID:");
    logd("        allocationSize: %"PRIu64, ahb_props.allocationSize);
    logd("        memoryTypeBits: %" RU_FMT_VK_FLAGS, ahb_props.memoryTypeBits);
    logd("    VkAndroidHardwareBufferFormatPropertiesANDROID:");
    logd("        format: %u", ahb_format_props.format);
    logd("        externalFormat: %"PRIu64, ahb_format_props.externalFormat);
    logd("        formatFeatures: %" RU_FMT_VK_FLAGS, ahb_format_props.formatFeatures);
    logd("        samplerYcbcrConversionComponents:");
    logd("            r: %u", ahb_format_props.samplerYcbcrConversionComponents.r);
    logd("            g: %u", ahb_format_props.samplerYcbcrConversionComponents.g);
    logd("            b: %u", ahb_format_props.samplerYcbcrConversionComponents.b);
    logd("            a: %u", ahb_format_props.samplerYcbcrConversionComponents.a);
    logd("        suggestedYcbcrModel: %u", ahb_format_props.suggestedYcbcrModel);
    logd("        suggestedYcbcrRange: %u", ahb_format_props.suggestedYcbcrRange);
    logd("        suggestedXChromaOffset: %u", ahb_format_props.suggestedXChromaOffset);
    logd("        suggestedYChromaOffset: %u", ahb_format_props.suggestedYChromaOffset);

    VkImageCreateInfo image_create_info;
    VkExternalMemoryImageCreateInfoKHR ext_mem_image_create_info;
    VkExternalFormatANDROID ext_format;
    ru_ahb_choose_image_creation_params(
        phys_dev,
        rend->queue_fam_index,
        rend->use_ext_format,
        ahb,
        &ahb_desc,
        &ahb_props,
        &ahb_format_props,
        &image_create_info,
        &ext_mem_image_create_info,
        &ext_format);

    ru_chain_vk_structs(
            &image_create_info,
            &ext_mem_image_create_info,
            &ext_format,
            NULL);

    VkImage image;
    check(vkCreateImage(dev->vk, &image_create_info, ru_alloc_cb, &image));

    // Memory allocation and binding are unusual for AHB images.  The app
    // doesn't call vkGetImageMemoryRequirements2 because the spec prohibits
    // calling it on AHB images before they are bound to memory. Instead, the
    // spec requires the app to import the AHB as VkDeviceMemory dedicated to
    // a VkImage. The spec permits the app to call
    // vkGetImageMemoryRequirements2 *after* binding if needed, which is rare.

    VkMemoryAllocateInfo mem_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,

        .memoryTypeIndex = ({
            // Be sloppy. Choose any supported bit.
            assert(ahb_props.memoryTypeBits != 0);
            1 << (__builtin_ffs(ahb_props.memoryTypeBits) - 1);
        }),

        .allocationSize = ahb_props.allocationSize,
    };

    VkImportAndroidHardwareBufferInfoANDROID import_ahb_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
        .buffer = ahb,
    };

    // Required for AHB images.
    VkMemoryDedicatedAllocateInfo mem_ded_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = image,
    };

    ru_chain_vk_structs(
        &mem_alloc_info,
        &import_ahb_info,
        &mem_ded_alloc_info,
        NULL);

    VkDeviceMemory mem;
    check(vkAllocateMemory(dev->vk, &mem_alloc_info, ru_alloc_cb, &mem));

    // Dedicated memory bindings require offset 0.
    check(vkBindImageMemory(dev->vk, image, mem, /*offset*/ 0));

    VkSamplerYcbcrConversionCreateInfoKHR sampler_ycbcr_conv_create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO_KHR,
        .format = image_create_info.format,
        .ycbcrModel = ahb_format_props.suggestedYcbcrModel,
        .ycbcrRange = ahb_format_props.suggestedYcbcrRange,
        .components = ahb_format_props.samplerYcbcrConversionComponents,
        .xChromaOffset = ahb_format_props.suggestedXChromaOffset,
        .yChromaOffset = ahb_format_props.suggestedYChromaOffset,
        .chromaFilter = VK_FILTER_NEAREST,
        .forceExplicitReconstruction = false,
    };

    ru_chain_vk_structs(
        &sampler_ycbcr_conv_create_info,
        &ext_format,
        NULL);

    VkSamplerYcbcrConversion sampler_ycbcr_conv;
    check(inst->vkCreateSamplerYcbcrConversionKHR(dev->vk,
            &sampler_ycbcr_conv_create_info, ru_alloc_cb, &sampler_ycbcr_conv));

    VkSamplerCreateInfo sampler_create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .flags = 0,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = false,
        .compareEnable = false,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .unnormalizedCoordinates = false,
    };

    VkSamplerYcbcrConversionInfo sampler_ycbcr_conv_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
        .conversion = sampler_ycbcr_conv,
    };

    ru_chain_vk_structs(
        &sampler_create_info,
        &sampler_ycbcr_conv_info,
        NULL);

    VkSampler sampler;
    check(vkCreateSampler(dev->vk, &sampler_create_info, ru_alloc_cb,
            &sampler));

    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,

        // From the Vulkan 1.1.111 spec:
        //
        //     If the image has a multi-planar format and
        //     subresourceRange.aspectMask is VK_IMAGE_ASPECT_COLOR_BIT, format
        //     must be identical to the image format, and the sampler to be
        //     used with the image view must enable sampler Y’CBCR conversion.
        //
        //     If image was not created with the
        //     VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT flag, or if the format of the
        //     image is a multi-planar format and if
        //     subresourceRange.aspectMask is VK_IMAGE_ASPECT_COLOR_BIT, format
        //     must be identical to the format used to create image.
        //
        //     If image has an external format, the pNext chain must contain an
        //     instance of VkSamplerYcbcrConversionInfo with a conversion
        //     object created with the same external format as image.
        .format = image_create_info.format,

        // Don't swizzle again. We already provided
        // VkAndroidHardwareBufferFormatPropertiesANDROID::samplerYcbcrConversionComponents
        // to VkSamplerYcbcrConversion.
        //
        // From the Vulkan 1.1.111 spec:
        //
        //     If image has an external format, all members of components must
        //     be VK_COMPONENT_SWIZZLE_IDENTITY.
        .components = (VkComponentMapping) {0}, // identity mapping

        .subresourceRange = (VkImageSubresourceRange) {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    ru_chain_vk_structs(
        &image_view_create_info,
        &sampler_ycbcr_conv_info,
        NULL);

    VkImageView image_view;
    check(vkCreateImageView(dev->vk, &image_view_create_info, ru_alloc_cb,
            &image_view));

    // When using VkSamplerYcbcrConversionKHR, the Vulkan spec requires that
    // the VkDescriptorSetLayoutBinding use use an immutable
    // combined-image-sampler.
    VkDescriptorSetLayout desc_set_layout;
    check(vkCreateDescriptorSetLayout(dev->vk,
        &(VkDescriptorSetLayoutCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
            .bindingCount = 1,
            .pBindings = (VkDescriptorSetLayoutBinding[]) {
                {
                    .binding = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = (VkSampler[]) {
                        sampler,
                    },
                },
            },
        },
        ru_alloc_cb,
        &desc_set_layout));

    VkPipelineLayout pipeline_layout;
    check(vkCreatePipelineLayout(dev->vk,
        &(VkPipelineLayoutCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = (VkDescriptorSetLayout[]) { desc_set_layout },
            .pushConstantRangeCount = 0,
        },
        ru_alloc_cb,
        &pipeline_layout));

    VkPipeline pipeline;
    check(vkCreateGraphicsPipelines(dev->vk,
        (VkPipelineCache) VK_NULL_HANDLE,
        /*count*/ 1,
        (VkGraphicsPipelineCreateInfo[]) {
            {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 2,
                .pStages = (VkPipelineShaderStageCreateInfo[]) {
                    {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .module = rend->vert_module,
                        .pName = "main",
                    },
                    {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .module = rend->frag_module,
                        .pName = "main",
                    },
                },
                .pVertexInputState = &(VkPipelineVertexInputStateCreateInfo) {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                    .vertexBindingDescriptionCount = 0,
                },
                .pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo) {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                    .primitiveRestartEnable = false,
                },
                .pViewportState = &(VkPipelineViewportStateCreateInfo) {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                    .viewportCount = 1,
                    .pViewports = NULL, // dynamic
                    .scissorCount = 1,
                    .pScissors = NULL, // dynamic
                },
                .pRasterizationState = &(VkPipelineRasterizationStateCreateInfo) {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                    .depthClampEnable = false,
                    .rasterizerDiscardEnable = false,
                    .polygonMode = VK_POLYGON_MODE_FILL,
                    .cullMode = VK_CULL_MODE_NONE,
                    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                    .depthBiasEnable = false,
                    .lineWidth = 1.0,
                },
                .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                    .rasterizationSamples = 1,
                },
                .pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                    .logicOpEnable = false,
                    .attachmentCount = 1,
                    .pAttachments = (VkPipelineColorBlendAttachmentState []) {
                        {
                            .blendEnable = false,
                            .colorWriteMask =
                                VK_COLOR_COMPONENT_R_BIT |
                                VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT |
                                VK_COLOR_COMPONENT_A_BIT,
                        },
                    },
                },
                .pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                    .flags = 0,
                    .dynamicStateCount = 2,
                    .pDynamicStates = (VkDynamicState[]) {
                        VK_DYNAMIC_STATE_VIEWPORT,
                        VK_DYNAMIC_STATE_SCISSOR,
                    },
                },
                .layout = pipeline_layout,
                .renderPass = rend->render_pass,
                .subpass = 0,
                .basePipelineHandle = VK_NULL_HANDLE,
                .basePipelineIndex = 0, // ignored
            },
        },
        ru_alloc_cb,
        &pipeline));

     *rahb = (RuAhb) {
        .ahb = ahb,
        .mem = mem,
        .image = image,
        .image_view = image_view,
        .sampler_ycbcr_conv = sampler_ycbcr_conv,
        .sampler = sampler,
        .desc_set_layout = desc_set_layout,
        .pipeline_layout = pipeline_layout,
        .pipeline = pipeline,
        .aimage = NULL,
        .in_aimage_reader = false,
    };
}

static void
ru_ahb_finish(RuDevice *dev, RuAhb *rahb) {
    if (rahb->aimage) {
        // Assume that if we own an AImage then the AImageReader holds
        // a reference to the AImage's AHB.
        assert(rahb->in_aimage_reader);
        AImage_delete(rahb->aimage);
    }

    vkDestroyPipeline(dev->vk, rahb->pipeline, ru_alloc_cb);
    vkDestroyPipelineLayout(dev->vk, rahb->pipeline_layout, ru_alloc_cb);
    vkDestroyDescriptorSetLayout(dev->vk, rahb->desc_set_layout, ru_alloc_cb);
    vkDestroySampler(dev->vk, rahb->sampler, ru_alloc_cb);

    // FIXME: vkDestroySamplerYcbcrConversion(dev->vk, rahb->sampler_ycbcr_conv, ru_alloc_cb);
    logd("WORKAROUND: Avoid vkDestroySamplerYcbcrConversion; it crashes "
            "libVkLayer_unique_objects.so");

    vkDestroyImageView(dev->vk, rahb->image_view, ru_alloc_cb);
    vkDestroyImage(dev->vk, rahb->image, ru_alloc_cb);
    vkFreeMemory(dev->vk, rahb->mem, ru_alloc_cb);
    AHardwareBuffer_release(rahb->ahb);
}

static void
ru_frame_reset(RuDevice *dev, RuFrame *frame) {
    assert(!frame->is_reset);

    check(vkResetFences(dev->vk,
        /*fenceCount*/ 1,
        (VkFence[]) { frame->release_fence }));

    if (frame->rahb) {
        if (frame->rahb->aimage) {
            AImage_delete(frame->rahb->aimage);
            frame->rahb->aimage = NULL;
        }

        frame->rahb = NULL;
    }

    frame->is_reset = true;
}

static RuFramechain * _malloc_ _must_use_result_
ru_framechain_new(
        RuSwapchain *swapchain,
        VkCommandPool cmd_pool,
        VkRenderPass render_pass)
{
    RuDevice *dev = swapchain->dev;
    const uint32_t len = swapchain->len;

    VkFence swapchain_fence;
    check(vkCreateFence(dev->vk,
        &(VkFenceCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = 0,
        },
        ru_alloc_cb,
        &swapchain_fence));

    VkCommandBuffer cmd_buffers[len];
    check(vkAllocateCommandBuffers(dev->vk,
        &(VkCommandBufferAllocateInfo) {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = cmd_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = len,
        },
        cmd_buffers));

    let frames = new_array(RuFrame, len);

    for (uint32_t i = 0; i < len; ++i) {
        VkImage image = swapchain->images[i];

        VkImageView image_view;
        check(vkCreateImageView(dev->vk,
            &(VkImageViewCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = ru_present_format.format,
                .components = (VkComponentMapping) {0}, // identity mapping
                .subresourceRange = (VkImageSubresourceRange) {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
            ru_alloc_cb,
            &image_view));

        VkFramebuffer framebuffer;
        check(vkCreateFramebuffer(dev->vk,
            &(VkFramebufferCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = render_pass,
                .attachmentCount = 1,
                .pAttachments = (VkImageView[]) {
                    image_view,
                },
                .width = swapchain->extent.width,
                .height = swapchain->extent.height,
                .layers = 1,
            },
            ru_alloc_cb,
            &framebuffer));

        VkSemaphore release_sem;
        check(vkCreateSemaphore(dev->vk,
            &(VkSemaphoreCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            },
            ru_alloc_cb,
            &release_sem));

        VkFence release_fence;
        check(vkCreateFence(dev->vk,
            &(VkFenceCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = 0,
            },
            ru_alloc_cb,
            &release_fence));

        frames[i] = (RuFrame) {
            .swapchain_image_index = i,
            .swapchain_image = image,
            .swapchain_image_view = image_view,

            .cmd_buffer = cmd_buffers[i],
            .framebuffer = framebuffer,
            .extent = swapchain->extent,

            .release_fence = release_fence,
            .release_sem = release_sem,

            .rahb = NULL,

            .is_reset = true,
        };
    }

    let framechain = new(RuFramechain);
    framechain->swapchain = swapchain;
    framechain->swapchain_fence = swapchain_fence;
    framechain->frames = frames;
    ru_queue_init(&framechain->submitted_frames, sizeof(RuFrame*),
            swapchain->len);

    return framechain;
}

static void
ru_framechain_free(RuFramechain *framechain)
{
    if (!framechain)
        return;

    RuDevice *dev = framechain->swapchain->dev;
    const uint32_t len = framechain->swapchain->len;

    VkFence fences[len];
    for (uint32_t i = 0; i < len; ++i) {
        fences[i] = framechain->frames[i].release_fence;
    }

    // Wait for all resources to become unused.
    check(vkWaitForFences(dev->vk, len, fences,
        /*waitAll*/ true,
        /*timeout*/ UINT64_MAX));

    for (uint32_t i = 0; i < len; ++i) {
        RuFrame *frame = &framechain->frames[i];

        if (frame->rahb) {
            if (frame->rahb->aimage) {
                AImage_delete(frame->rahb->aimage);
                frame->rahb->aimage = NULL;
            }
        }

        vkDestroySemaphore(dev->vk, frame->release_sem, ru_alloc_cb);
        vkDestroyFence(dev->vk, frame->release_fence, ru_alloc_cb);
        vkDestroyFramebuffer(dev->vk, frame->framebuffer, ru_alloc_cb);
        vkDestroyImageView(dev->vk, frame->swapchain_image_view, ru_alloc_cb);

    }

    vkDestroyFence(dev->vk, framechain->swapchain_fence, ru_alloc_cb);
    ru_queue_finish(&framechain->submitted_frames);
    free(framechain->frames);
    free(framechain);
}

static void
ru_framechain_submit(
        RuFramechain *framechain,
        RuFrame *frame,
        VkQueue queue)
{
    ru_queue_push(&framechain->submitted_frames, &frame);

    check(vkQueueSubmit(queue,
        /*submitCount*/ 1,
        (VkSubmitInfo[]) {
            {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount = 0,
                .commandBufferCount = 1,
                .pCommandBuffers = (VkCommandBuffer[]) {
                    frame->cmd_buffer,
                },
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = (VkSemaphore[]) {
                    frame->release_sem,
                },
            },
        },
        frame->release_fence));

    VkResult swapchain_result = VK_SUCCESS;
    VkResult present_result = vkQueuePresentKHR(queue,
        &(VkPresentInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = (VkSemaphore[]) {
                frame->release_sem,
            },
            .swapchainCount = 1,
            .pSwapchains = (VkSwapchainKHR[]) {
                framechain->swapchain->vk,
            },
            .pImageIndices = (uint32_t[]) {
                frame->swapchain_image_index,
            },
            &swapchain_result,
        });

    switch (swapchain_result) {
        case VK_SUCCESS:
        case VK_ERROR_OUT_OF_DATE_KHR:
        case VK_SUBOPTIMAL_KHR:
            framechain->swapchain->status = swapchain_result;
            break;
        default:
            die("vkQueuePresentKHR returned VkResult(%d)", swapchain_result);
    }

    if (present_result != VK_SUCCESS)
        die("vkQueuePresentKHR returned VkResult(%d)", present_result);
}

static void
ru_framechain_collect(RuDevice *dev, RuFramechain *framechain) {
    for (;;) {
        RuFrame *frame;
        if (!ru_queue_peek(&framechain->submitted_frames, &frame))
            return;

        if (!frame->is_reset) {
            VkResult r = vkGetFenceStatus(dev->vk, frame->release_fence);
            switch (r) {
                case VK_SUCCESS:
                    break;
                case VK_NOT_READY:
                    return;
                default:
                    die("%s: vkGetFenceStatus failed with VkResult(%d)",
                            __func__, r);
            }

            ru_frame_reset(dev, frame);
        }

        (void) ru_queue_pop(&framechain->submitted_frames, NULL);
    }
}

#define ru_ahb_cache_each_slot(cache, slot) \
    __ru_ahb_cache_each_slot(UNIQ(_cache), (cache), UNIQ(i), slot)

#define __ru_ahb_cache_each_slot(uniq_cache, cache, uniq_i, slot) \
    RuAhbCache *uniq_cache = (cache); \
    size_t uniq_i = 0; \
    for (typeof(&uniq_cache->slots[0]) slot = NULL; \
         uniq_i < ARRAY_LEN(uniq_cache->slots) \
            && (slot = &uniq_cache->slots[uniq_i], true); \
         ++uniq_i)

static RuAhb * _must_use_result_
ru_ahb_cache_search(RuAhbCache *cache, AHardwareBuffer *ahb) {
    ru_ahb_cache_each_slot(cache, rahb) {
        if (rahb->ahb == ahb) {
            return rahb;
        }
    }

    return NULL;
}

static RuAhb * _must_use_result_
ru_rend_import_ahb(RuRend *rend, AHardwareBuffer *ahb) {
    RuAhbCache *cache = &rend->ahb_cache;
    RuAhb *rahb;

    rahb = ru_ahb_cache_search(cache, ahb);
    if (rahb)
        return rahb;

    // Cache miss. Find empty slot.
    rahb = ru_ahb_cache_search(cache, NULL);
    if (!rahb)
        die("RuAhbCache is full");

    ru_ahb_init(rend, ahb, rahb);

    return rahb;
}

static void
ru_rend_purge_dead_ahbs(RuRend *rend) {
    RuDevice *dev = &rend->dev;

    ru_ahb_cache_each_slot(&rend->ahb_cache, slot) {
        if (!slot->ahb) {
            // invalid slot
            continue;
        }

        if (slot->in_aimage_reader) {
            // The AImageReader still holds a reference to the AHB. Therefore
            // the media decoder may continue to update it.
            continue;
        }

        assert(!slot->aimage);
        ru_ahb_finish(dev, slot);

        // Invalidate the slot
        slot->ahb = NULL;
    }
}

static void
on_aimage_available(void *_heap, AImageReader *reader) {
    static _Atomic uint64_t seq = 0;
    logd("%s: seq=%"PRIu64, __func__, ++seq);

    RuAImageHeap *heap = _heap;

    assert(reader == heap->aimage_reader);

    ru_mutex_lock_scoped(&heap->aimage_available.mutex);

    ++heap->aimage_available.count;

    if (pthread_cond_broadcast(&heap->aimage_available.cond))
        abort();
}

static void
ru_aimage_heap_init(RuAImageHeap *heap, AImageReader *reader) {
    *heap = (RuAImageHeap) {
        .aimage_reader = reader,
        .aimage_available = {
            .mutex = PTHREAD_MUTEX_INITIALIZER,
            .cond = PTHREAD_COND_INITIALIZER,

            // Assume the media decoder has already begun and therefore images
            // are already available.
            .count = 1,
        },
    };

    AImageReader_setImageListener(reader,
        &(AImageReader_ImageListener) {
            .context = heap,
            .onImageAvailable = on_aimage_available,
        });
}

static void
ru_aimage_heap_finish(RuAImageHeap *heap) {
    assert(heap->aimage_reader);

    AImageReader_setImageListener(heap->aimage_reader, NULL);

    if (pthread_mutex_init(&heap->aimage_available.mutex, NULL))
        abort();

    if (pthread_cond_init(&heap->aimage_available.cond, NULL))
        abort();
}

static AImage * _must_use_result_
ru_aimage_heap_pop_wait(RuAImageHeap *heap) {
    static _Atomic uint64_t seq = 0;
    logd("%s: seq=%"PRIu64, __func__, ++seq);

    int ret;

    ru_mutex_lock_scoped(&heap->aimage_available.mutex);

 try_again:
    while (heap->aimage_available.count == 0) {
        // FIXME: Avoid deadlock when the media decoder is done.
        if (pthread_cond_wait(&heap->aimage_available.cond,
                    &heap->aimage_available.mutex)) {
            abort();
        }
    }

    // TODO: Use AImageReader_acquireLatestImageAsync.
    AImage *aimage;
    ret = AImageReader_acquireLatestImage(heap->aimage_reader, &aimage);

    heap->aimage_available.count = 0;

    switch (ret) {
        case AMEDIA_OK:
            assert(aimage);
            break;
        case AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE:
            goto try_again;
        default:
            die("AImageReader_acquireLatestImage: unexpected error=%d", ret);
    }

    return aimage;
}

static RuFrame * _must_use_result_
ru_rend_next_frame(RuRend *rend) {
    RuDevice *dev = &rend->dev;
    RuFramechain *framechain = rend->framechain;
    RuSwapchain *swapchain = framechain->swapchain;
    int ret;

    check(vkResetFences(dev->vk,
        /*fenceCount*/ 1,
        (VkFence[]) { framechain->swapchain_fence }));

    uint32_t frame_index;
    check(vkAcquireNextImageKHR(dev->vk, swapchain->vk,
        /*timeout*/ UINT64_MAX,
        /*semaphore*/ VK_NULL_HANDLE,
        framechain->swapchain_fence,
        &frame_index));

    RuFrame *frame = &framechain->frames[frame_index];

    if (!frame->is_reset) {
        // Block until the queue is no longer accessing the old frame's resources.
        check(vkWaitForFences(dev->vk,
            /*fenceCount*/ 1,
            (VkFence[]) { frame->release_fence },
            /*waitAll*/ true,
            /*timeout*/ UINT64_MAX));

        ru_frame_reset(dev, frame);
    }

    // We want to present the most recently decoded video frame. So we postpone
    // pulling the AImage until the swapchain's VkImage is ready for rendering.
    check(vkWaitForFences(dev->vk,
        /*fenceCount*/ 1,
        (VkFence[]) { framechain->swapchain_fence },
        /*waitAll*/ true,
        /*timeout*/ UINT64_MAX));

    // FIXME: Avoid deadlock when the media decoder is done.
    AImage *aimage = ru_aimage_heap_pop_wait(&rend->aimage_heap);
    assert(aimage);

    AHardwareBuffer *ahb;
    ret = AImage_getHardwareBuffer(aimage, &ahb);
    if (ret)
        die("AImage_getHardwareBuffer failed: error=%d", ret);

    frame->is_reset = false;
    frame->rahb = ru_rend_import_ahb(rend, ahb);
    frame->rahb->aimage = aimage;
    frame->rahb->in_aimage_reader = true;

    return frame;
}

static const char *
ru_rend_event_type_to_str(RuRendEventType t) {
    #define CASE(name) case name: return #name

    switch (t) {
        CASE(RU_REND_EVENT_START);
        CASE(RU_REND_EVENT_STOP);
        CASE(RU_REND_EVENT_PAUSE);
        CASE(RU_REND_EVENT_UNPAUSE);
        CASE(RU_REND_EVENT_BIND_WINDOW);
        CASE(RU_REND_EVENT_UNBIND_WINDOW);
        CASE(RU_REND_EVENT_AIMAGE_BUFFER_REMOVED);
        default:
            die("unknown RuRendEventType(%d)", t);
    }

    #undef CASE
}

static void
ru_rend_push_event(RuRend *rend, RuRendEvent ev) {
    logd("push %s", ru_rend_event_type_to_str(ev.type));
    ru_chan_push(&rend->event_chan, &ev);
}

void
ru_rend_start(RuRend *rend, AImageReader *reader) {
    assert(reader);

    ru_rend_push_event(rend,
        (RuRendEvent) {
            .type = RU_REND_EVENT_START,
            .start = {
                .aimage_reader = reader,
            },
        });
}

void
ru_rend_stop(RuRend *rend) {
    ru_rend_push_event(rend,
        (RuRendEvent) { .type = RU_REND_EVENT_STOP, });
}


void
ru_rend_pause(RuRend *rend) {
    ru_rend_push_event(rend,
        (RuRendEvent) { .type = RU_REND_EVENT_PAUSE, });
}

void
ru_rend_unpause(RuRend *rend) {
    ru_rend_push_event(rend,
        (RuRendEvent) { .type = RU_REND_EVENT_UNPAUSE, });
}

RuRend *
ru_rend_new_s(struct ru_rend_new_args args) {
    let rend = new(RuRend);

    ru_instance_init(args.use_validation, &rend->inst);
    ru_phys_dev_init(&rend->inst, &rend->phys_dev);
    ru_device_init(&rend->phys_dev, &rend->dev);
    rend->use_ext_format = args.use_external_format;

    rend->queue_fam_index = ru_choose_queue_family(&rend->phys_dev);

    vkGetDeviceQueue(rend->dev.vk, rend->queue_fam_index, /*queueIndex*/ 0,
            &rend->queue);

    check(vkCreateCommandPool(rend->dev.vk,
        &(VkCommandPoolCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = rend->queue_fam_index,
        },
        ru_alloc_cb,
        &rend->cmd_pool));

    check(vkCreateRenderPass(rend->dev.vk,
        &(VkRenderPassCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = (VkAttachmentDescription[]) {
                {
                    .format = ru_present_format.format,
                    .samples = 1,
                    // loadOp is irrelevant because we draw the full quad
                    .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                },
            },
            .subpassCount = 1,
            .pSubpasses = (VkSubpassDescription[]) {
                {
                    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                    .colorAttachmentCount = 1,
                    .pColorAttachments = (VkAttachmentReference[]) {
                        {
                            .attachment = 0,
                            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        },
                    },
                },
            },
        },
        ru_alloc_cb,
        &rend->render_pass));

    static const uint32_t vert_spirv[] = {
        #include "quad.vert.spvnum"
    };

    check(vkCreateShaderModule(rend->dev.vk,
        &(VkShaderModuleCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = sizeof(vert_spirv),
            .pCode = vert_spirv,
        },
        ru_alloc_cb,
        &rend->vert_module));

    static const uint32_t frag_spirv[] = {
        #include "quad.frag.spvnum"
    };

    check(vkCreateShaderModule(rend->dev.vk,
        &(VkShaderModuleCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = sizeof(frag_spirv),
            .pCode = frag_spirv,
        },
        ru_alloc_cb,
        &rend->frag_module));

    rend->surf = NULL;
    rend->swapchain = NULL;
    rend->framechain = NULL;

    zero(rend->ahb_cache);

    rend->aimage_heap.aimage_reader = NULL; // invalidate

    ru_chan_init(&rend->event_chan, sizeof(RuRendEvent), 8);

    if (pthread_create(&rend->thread, NULL, ru_rend_thread, rend))
        abort();

    return rend;
}

void
ru_rend_free(RuRend *rend) {
    if (!rend)
        return;

    ru_rend_stop(rend);

    if (pthread_join(rend->thread, NULL))
        abort();

    if (rend->aimage_heap.aimage_reader) {
        AImageReader_setBufferRemovedListener(rend->aimage_heap.aimage_reader, NULL);
        ru_aimage_heap_finish(&rend->aimage_heap);
    }

    ru_ahb_cache_each_slot(&rend->ahb_cache, rahb) {
        if (rahb->ahb) {
            ru_ahb_finish(&rend->dev, rahb);
        }
    }

    vkDestroyShaderModule(rend->dev.vk, rend->vert_module, ru_alloc_cb);
    vkDestroyShaderModule(rend->dev.vk, rend->frag_module, ru_alloc_cb);
    vkDestroyRenderPass(rend->dev.vk, rend->render_pass, ru_alloc_cb);
    vkDestroyCommandPool(rend->dev.vk, rend->cmd_pool, ru_alloc_cb);
    ru_framechain_free(rend->framechain);
    ru_swapchain_free(rend->swapchain);
    ru_surface_free(rend->surf);
    ru_device_finish(&rend->dev);
    ru_phys_dev_finish(&rend->phys_dev);
    ru_instance_finish(&rend->inst);
    ru_chan_finish(&rend->event_chan);

    free(rend);
}

void
ru_rend_bind_window(RuRend *rend, ANativeWindow *window) {
    ru_rend_push_event(rend,
        (RuRendEvent) {
            .type = RU_REND_EVENT_BIND_WINDOW,
            .bind_window = {
                .window = window,
            },
        });
}

void
ru_rend_unbind_window(RuRend *rend) {
    ru_rend_push_event(rend,
        (RuRendEvent) { .type = RU_REND_EVENT_UNBIND_WINDOW, });
}

static void
on_aimage_buffer_removed(
        void *_rend,
        AImageReader *reader,
        AHardwareBuffer *ahb)
{
    RuRend *rend = _rend;

    assert(reader == rend->aimage_heap.aimage_reader);

    ru_rend_push_event(rend,
        (RuRendEvent) {
            .type = RU_REND_EVENT_AIMAGE_BUFFER_REMOVED,
            .aimage_buffer_removed  = {
                .ahb = ahb,
            },
        });
}

static void
ru_rend_present(RuRend *rend) {
    static _Atomic uint64_t seq = 0;
    logd("%s: seq=%"PRIu64, __func__, ++seq);

    RuInstance *inst = &rend->inst;

    assert(rend->surf);
    assert(!!rend->framechain == !!rend->swapchain);

    bool want_new_swapchain =
        !rend->swapchain ||
        rend->swapchain->status != VK_SUCCESS;

    if (want_new_swapchain) {
        if (rend->swapchain) {
            ru_framechain_free(rend->framechain);
            ru_swapchain_free(rend->swapchain);
        }

        rend->swapchain = ru_swapchain_new(&rend->dev, rend->surf,
                rend->queue_fam_index);
        rend->framechain = ru_framechain_new(rend->swapchain, rend->cmd_pool,
                rend->render_pass);
    }

    assert(rend->swapchain);
    assert(rend->framechain);

    RuFrame *frame = ru_rend_next_frame(rend);
    if (!frame) {
        // The framechain is finished. No more frames will arrive.
        ru_rend_stop(rend);
        return;
    }

    check(vkBeginCommandBuffer(frame->cmd_buffer,
        &(VkCommandBufferBeginInfo) {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        }));

    vkCmdPipelineBarrier(frame->cmd_buffer,
        /*srcStageMask*/ VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        /*dstStageMask*/ VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        /*dependencyFlags*/ 0,
        /*memoryBarriers*/ 0, NULL,
        /*bufferMemmoryBarriers*/ 0, NULL,
        /*imageMemmoryBarriers*/ 1, (VkImageMemoryBarrier[]) {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
                .dstQueueFamilyIndex = rend->queue_fam_index,
                .image = frame->rahb->image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
        });

    vkCmdBeginRenderPass(frame->cmd_buffer,
        &(VkRenderPassBeginInfo) {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = rend->render_pass,
            .framebuffer = frame->framebuffer,
            .renderArea = (VkRect2D) {
                .offset = { 0, 0 },
                .extent = frame->extent,
            },
            // We draw the full quad
            .clearValueCount = 0,

        },
        VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(frame->cmd_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        frame->rahb->pipeline);

    inst->vkCmdPushDescriptorSetKHR(frame->cmd_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        frame->rahb->pipeline_layout,
        /*set*/ 0,
        /*descriptorWriteCount*/ 1,
        (VkWriteDescriptorSet[]) {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                // .dstSet = ignored,
                // .dstBinding = ignored,
                // .dstArrayElement = ignored,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = (VkDescriptorImageInfo[]) {
                    {
                        .sampler = frame->rahb->sampler,
                        .imageView = frame->rahb->image_view,
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    },
                },
            },
        });

    vkCmdSetViewport(frame->cmd_buffer,
        /*first*/ 0,
        /*count*/ 1,
        (VkViewport[]) {
            {
                .x = 0.0,
                .y = 0.0,
                .width = frame->extent.width,
                .height = frame->extent.height,
                .minDepth = 0.0,
                .maxDepth = 1.0,
            },
        });

    vkCmdSetScissor(frame->cmd_buffer,
        /*first*/ 0,
        /*count*/ 1,
        (VkRect2D[]) {
            {
                .offset = { 0.0, 0.0 },
                .extent = frame->extent,
            },
        });

    vkCmdDraw(frame->cmd_buffer,
        /*vertexCount*/ 4,
        /*instanceCount*/ 1,
        /*firstVertex*/ 0,
        /*firstInstance*/ 0);

    vkCmdEndRenderPass(frame->cmd_buffer);

    vkCmdPipelineBarrier(frame->cmd_buffer,
        /*srcStageMask*/ VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        /*dstStageMask*/ VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        /*dependencyFlags*/ 0,
        /*memoryBarriers*/ 0, NULL,
        /*bufferMemmoryBarriers*/ 0, NULL,
        /*imageMemmoryBarriers*/ 1, (VkImageMemoryBarrier[]) {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .dstAccessMask = 0,
                .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = rend->queue_fam_index,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
                .image = frame->rahb->image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
        });

    check(vkEndCommandBuffer(frame->cmd_buffer));

    ru_framechain_submit(rend->framechain, frame, rend->queue);
}

static void *
ru_rend_thread(void *_rend) {
    logd("start rend thread tid=%d", gettid());

    RuRend *rend = _rend;
    bool started = false;
    bool paused = true;
    bool window_bound = false;

    for (;;) {
        RuRendEvent ev;
        bool found_event;

        if (paused) {
            ru_chan_pop_wait(&rend->event_chan, &ev);
            found_event = true;
        } else {
            found_event = ru_chan_pop_nowait(&rend->event_chan, &ev);
        }

        if (found_event) {
            logd("pop %s", ru_rend_event_type_to_str(ev.type));

            switch (ev.type) {
                case RU_REND_EVENT_START: {
                    assert(!started);
                    assert(ev.start.aimage_reader);

                    assert(!rend->aimage_heap.aimage_reader); // should be invalid
                    ru_aimage_heap_init(&rend->aimage_heap, ev.start.aimage_reader);

                    AImageReader_setBufferRemovedListener(ev.start.aimage_reader,
                        &(AImageReader_BufferRemovedListener) {
                            .context = rend,
                            .onBufferRemoved = on_aimage_buffer_removed,
                        });

                    started = true;
                    break;
                }
                case RU_REND_EVENT_STOP:
                    return NULL;
                case RU_REND_EVENT_BIND_WINDOW: {
                    assert(!window_bound);
                    assert(!rend->surf);
                    assert(!rend->swapchain);
                    assert(!rend->framechain);
                    rend->surf = ru_surface_new(&rend->phys_dev, ev.bind_window.window);
                    window_bound = true;
                    break;
                }
                case RU_REND_EVENT_UNBIND_WINDOW: {
                    assert(window_bound);

                    ru_framechain_free(rend->framechain);
                    ru_swapchain_free(rend->swapchain);
                    ru_surface_free(rend->surf);

                    rend->framechain = NULL;
                    rend->swapchain = NULL;
                    rend->surf = NULL;

                    window_bound = false;
                    break;
                }
                case RU_REND_EVENT_PAUSE:
                    assert(started);
                    paused = true;
                    break;
                case RU_REND_EVENT_UNPAUSE:
                    assert(started);
                    paused = false;
                    break;
                case RU_REND_EVENT_AIMAGE_BUFFER_REMOVED: {
                    let *ahb = ev.aimage_buffer_removed.ahb;
                    let slot = ru_ahb_cache_search(&rend->ahb_cache, ahb);
                    if (slot) {
                        // Assume that the AImageReader will not remove an AImage's AHB
                        // if we hold ownership of the AImage.
                        assert(!slot->aimage);

                        slot->in_aimage_reader = false;
                    }
                    break;
                }
            }
        }

        if (!paused && window_bound) {
            ru_rend_present(rend);
        }

        if (rend->framechain) {
            ru_framechain_collect(&rend->dev, rend->framechain);
        }

        ru_rend_purge_dead_ahbs(rend);
    }
}
