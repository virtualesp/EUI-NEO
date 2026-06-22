#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <GL/gl.h>
#include <wrl/client.h>

#if defined(EUI_PROBE_HAS_VULKAN)
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>
#endif

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

bool hasName(const std::vector<std::string>& names, const char* target) {
    return std::find(names.begin(), names.end(), target) != names.end();
}

template <typename T>
bool queryInterface(IUnknown* object, ComPtr<T>& out) {
    return object && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&out)));
}

struct D3DProbeResult {
    bool device = false;
    bool factory2 = false;
    bool sharedTexture = false;
    bool ntSharedTexture = false;
    bool ntSharedHandle = false;
    bool legacySharedTexture = false;
    bool legacySharedHandle = false;
};

D3DProbeResult probeD3D() {
    D3DProbeResult result;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL actualLevel{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr,
                                 D3D_DRIVER_TYPE_HARDWARE,
                                 nullptr,
                                 flags,
                                 levels,
                                 ARRAYSIZE(levels),
                                 D3D11_SDK_VERSION,
                                 &device,
                                 &actualLevel,
                                 &context))) {
        return result;
    }
    result.device = true;

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory2;
    if (SUCCEEDED(device.As(&dxgiDevice)) &&
        SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory2)))) {
        result.factory2 = true;
    }

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = 256;
    textureDesc.Height = 256;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    ComPtr<ID3D11Texture2D> ntTexture;
    if (SUCCEEDED(device->CreateTexture2D(&textureDesc, nullptr, &ntTexture))) {
        result.sharedTexture = true;
        result.ntSharedTexture = true;
    }

    ComPtr<IDXGIResource1> resource1;
    if (queryInterface(ntTexture.Get(), resource1)) {
        HANDLE handle = nullptr;
        if (SUCCEEDED(resource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle))) {
            result.ntSharedHandle = true;
            CloseHandle(handle);
        }
    }

    textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    ComPtr<ID3D11Texture2D> legacyTexture;
    if (SUCCEEDED(device->CreateTexture2D(&textureDesc, nullptr, &legacyTexture))) {
        result.sharedTexture = true;
        result.legacySharedTexture = true;
        ComPtr<IDXGIResource> resource;
        HANDLE handle = nullptr;
        if (queryInterface(legacyTexture.Get(), resource) && SUCCEEDED(resource->GetSharedHandle(&handle)) && handle) {
            result.legacySharedHandle = true;
        }
    }
    return result;
}

#if defined(EUI_PROBE_HAS_VULKAN)
std::vector<std::string> instanceExtensionNames() {
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> properties(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, properties.data());
    std::vector<std::string> names;
    names.reserve(properties.size());
    for (const auto& property : properties) {
        names.emplace_back(property.extensionName);
    }
    return names;
}

std::vector<std::string> deviceExtensionNames(VkPhysicalDevice physicalDevice) {
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> properties(count);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, properties.data());
    std::vector<std::string> names;
    names.reserve(properties.size());
    for (const auto& property : properties) {
        names.emplace_back(property.extensionName);
    }
    return names;
}

struct VulkanProbeResult {
    bool sdk = true;
    bool instanceExternalMemoryCaps = false;
    bool deviceExternalMemory = false;
    bool deviceExternalMemoryWin32 = false;
    bool d3d11TextureImport = false;
    bool d3d11TextureKmtImport = false;
    bool opaqueWin32Import = false;
    bool opaqueWin32KmtImport = false;
    std::string deviceName;
};

bool queryExternalImage(VkPhysicalDevice physicalDevice, VkExternalMemoryHandleTypeFlagBits handleType) {
    VkPhysicalDeviceExternalImageFormatInfo externalInfo{};
    externalInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    externalInfo.handleType = handleType;

    VkPhysicalDeviceImageFormatInfo2 formatInfo{};
    formatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    formatInfo.pNext = &externalInfo;
    formatInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    formatInfo.type = VK_IMAGE_TYPE_2D;
    formatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    formatInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    formatInfo.flags = 0;

    VkExternalImageFormatProperties externalProperties{};
    externalProperties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    VkImageFormatProperties2 formatProperties{};
    formatProperties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    formatProperties.pNext = &externalProperties;

    const VkResult queryResult = vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, &formatInfo, &formatProperties);
    const VkExternalMemoryFeatureFlags features = externalProperties.externalMemoryProperties.externalMemoryFeatures;
    return queryResult == VK_SUCCESS &&
           (features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0 &&
           (features & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) != 0;
}

VulkanProbeResult probeVulkan() {
    VulkanProbeResult result;
    const std::vector<std::string> instanceExtensions = instanceExtensionNames();
    result.instanceExternalMemoryCaps = hasName(instanceExtensions, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
    if (!result.instanceExternalMemoryCaps) {
        return result;
    }

    const char* enabledExtensions[] = {
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME
    };
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "dxgi_present_bridge_probe";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = 1;
    instanceInfo.ppEnabledExtensionNames = enabledExtensions;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
        return result;
    }

    std::uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    for (VkPhysicalDevice physicalDevice : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        const std::vector<std::string> extensions = deviceExtensionNames(physicalDevice);
        const bool hasExternalMemory = hasName(extensions, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
        const bool hasExternalMemoryWin32 = hasName(extensions, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
        result.deviceExternalMemory = result.deviceExternalMemory || hasExternalMemory;
        result.deviceExternalMemoryWin32 = result.deviceExternalMemoryWin32 || hasExternalMemoryWin32;
        if (result.deviceName.empty()) {
            result.deviceName = properties.deviceName;
        }
        if (!hasExternalMemory || !hasExternalMemoryWin32) {
            continue;
        }

        result.d3d11TextureImport = result.d3d11TextureImport ||
            queryExternalImage(physicalDevice, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT);
        result.d3d11TextureKmtImport = result.d3d11TextureKmtImport ||
            queryExternalImage(physicalDevice, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT);
        result.opaqueWin32Import = result.opaqueWin32Import ||
            queryExternalImage(physicalDevice, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT);
        result.opaqueWin32KmtImport = result.opaqueWin32KmtImport ||
            queryExternalImage(physicalDevice, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT);
        if (result.d3d11TextureImport || result.d3d11TextureKmtImport ||
            result.opaqueWin32Import || result.opaqueWin32KmtImport) {
            result.deviceName = properties.deviceName;
        }
    }

    vkDestroyInstance(instance, nullptr);
    return result;
}
#else
struct VulkanProbeResult {
    bool sdk = false;
    bool instanceExternalMemoryCaps = false;
    bool deviceExternalMemory = false;
    bool deviceExternalMemoryWin32 = false;
    bool d3d11TextureImport = false;
    bool d3d11TextureKmtImport = false;
    bool opaqueWin32Import = false;
    bool opaqueWin32KmtImport = false;
    std::string deviceName;
};

VulkanProbeResult probeVulkan() {
    return {};
}
#endif

using WglGetExtensionsStringARB = const char* (WINAPI*)(HDC);

struct OpenGLProbeResult {
    bool context = false;
    bool wglExtensionsString = false;
    bool nvDxInterop = false;
    bool nvDxInterop2 = false;
    bool dxInteropEntryPoints = false;
};

OpenGLProbeResult probeOpenGL() {
    OpenGLProbeResult result;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = L"EuiWglBridgeProbe";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 32, 32, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        return result;
    }
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = ChoosePixelFormat(dc, &pfd);
    if (format > 0 && SetPixelFormat(dc, format, &pfd)) {
        HGLRC context = wglCreateContext(dc);
        if (context && wglMakeCurrent(dc, context)) {
            result.context = true;
            auto getExtensions = reinterpret_cast<WglGetExtensionsStringARB>(wglGetProcAddress("wglGetExtensionsStringARB"));
            if (getExtensions) {
                const char* extensions = getExtensions(dc);
                if (extensions) {
                    result.wglExtensionsString = true;
                    const std::string extensionText = extensions;
                    result.nvDxInterop = extensionText.find("WGL_NV_DX_interop") != std::string::npos;
                    result.nvDxInterop2 = extensionText.find("WGL_NV_DX_interop2") != std::string::npos;
                }
            }
            const bool hasOpen = wglGetProcAddress("wglDXOpenDeviceNV") != nullptr;
            const bool hasRegister = wglGetProcAddress("wglDXRegisterObjectNV") != nullptr;
            const bool hasLock = wglGetProcAddress("wglDXLockObjectsNV") != nullptr;
            result.dxInteropEntryPoints = hasOpen && hasRegister && hasLock;
            wglMakeCurrent(nullptr, nullptr);
        }
        if (context) {
            wglDeleteContext(context);
        }
    }
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    return result;
}

void printBool(const char* label, bool value) {
    std::cout << "  " << label << ": " << (value ? "yes" : "no") << '\n';
}

} // namespace

int main() {
    const D3DProbeResult d3d = probeD3D();
    const VulkanProbeResult vulkan = probeVulkan();
    const OpenGLProbeResult opengl = probeOpenGL();

    std::cout << "DXGI present bridge capability probe\n";
    std::cout << "\nD3D/DXGI\n";
    printBool("D3D11 device", d3d.device);
    printBool("IDXGIFactory2 / Present1 path", d3d.factory2);
    printBool("D3D shared texture", d3d.sharedTexture);
    printBool("D3D NT shared texture", d3d.ntSharedTexture);
    printBool("D3D NT shared handle", d3d.ntSharedHandle);
    printBool("D3D legacy shared texture", d3d.legacySharedTexture);
    printBool("D3D legacy shared handle", d3d.legacySharedHandle);

    std::cout << "\nVulkan -> DXGI bridge\n";
    printBool("Vulkan SDK linked", vulkan.sdk);
    printBool("VK_KHR_external_memory_capabilities", vulkan.instanceExternalMemoryCaps);
    printBool("VK_KHR_external_memory", vulkan.deviceExternalMemory);
    printBool("VK_KHR_external_memory_win32", vulkan.deviceExternalMemoryWin32);
    printBool("D3D11 texture import/export format", vulkan.d3d11TextureImport);
    printBool("D3D11 texture KMT import/export format", vulkan.d3d11TextureKmtImport);
    printBool("opaque Win32 import/export format", vulkan.opaqueWin32Import);
    printBool("opaque Win32 KMT import/export format", vulkan.opaqueWin32KmtImport);
    if (!vulkan.deviceName.empty()) {
        std::cout << "  candidate device: " << vulkan.deviceName << '\n';
    }

    std::cout << "\nOpenGL -> DXGI bridge\n";
    printBool("WGL context", opengl.context);
    printBool("WGL extension string", opengl.wglExtensionsString);
    printBool("WGL_NV_DX_interop", opengl.nvDxInterop);
    printBool("WGL_NV_DX_interop2", opengl.nvDxInterop2);
    printBool("DX interop entry points", opengl.dxInteropEntryPoints);

    std::cout << "\nConclusion\n";
    const bool d3dCanShare = d3d.ntSharedHandle || d3d.legacySharedHandle;
    const bool vulkanCanShare = vulkan.d3d11TextureImport || vulkan.d3d11TextureKmtImport ||
                                vulkan.opaqueWin32Import || vulkan.opaqueWin32KmtImport;
    if (d3d.factory2 && d3dCanShare && vulkanCanShare) {
        std::cout << "  Vulkan -> D3D/DXGI dirty present bridge looks feasible on this machine.\n";
    } else {
        std::cout << "  Vulkan -> D3D/DXGI bridge is missing at least one required capability.\n";
    }
    if (opengl.nvDxInterop && opengl.dxInteropEntryPoints) {
        std::cout << "  OpenGL -> D3D/DXGI bridge may be feasible through WGL_NV_DX_interop.\n";
    } else {
        std::cout << "  OpenGL -> D3D/DXGI bridge is not exposed through native WGL here; ANGLE/EGL would be the cleaner route.\n";
    }
    return 0;
}
#else
#include <iostream>

int main() {
    std::cout << "dxgi_present_bridge_probe is only available on Windows.\n";
    return 0;
}
#endif
