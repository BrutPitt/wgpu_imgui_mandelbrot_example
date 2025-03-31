//------------------------------------------------------------------------------
//  Copyright (c) 2025 Michele Morrone
//  All rights reserved.
//
//  https://michelemorrone.eu - https://brutpitt.com
//
//  X: https://x.com/BrutPitt - GitHub: https://github.com/BrutPitt
//
//  direct mail: brutpitt(at)gmail.com - me(at)michelemorrone.eu
//
//  This software is distributed under the terms of the BSD 2-Clause license
//------------------------------------------------------------------------------
#include <cstdio>
#include <cassert>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_wgpu.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>
#include <GLFW/emscripten_glfw3.h>

#include <webgpu/webgpu_cpp.h>
#else
#include <webgpu/webgpu_glfw.h>
#include <GLFW/glfw3.h>
#endif

// Initial App state
static const uint32_t initialWindowWidth {768};
static const uint32_t initialWindowHeight {768};
static const char *appTitle {"wgpu - imgui - Mandelbrot - GLFW example"};

// Mandelbrot data
struct alignas(16) shaderData_ {
    float mScaleX = 1.5, mScaleY = 1.5;
    float mTranspX = -.75, mTranspY = 0.0;
    float wSizeX = initialWindowWidth, wSizeY = initialWindowHeight;
    int32_t iterations = 256, nColors = 256;
    float shift = 0.0;
} shaderData;
const float zoomFactor = .05;

const char *shader  = {
    #include "../mandel.wgsl"
};

// Global WebGPU required
wgpu::Instance              instance;
wgpu::Device                device;
wgpu::Surface               surface;
wgpu::TextureFormat         preferredFormat { wgpu::TextureFormat::Undefined };  // current undefined, but set from SurfaceCapabilities
wgpu::SurfaceConfiguration  surfaceConfig;

// Pipeline related objs
wgpu::RenderPipeline pipeline;
wgpu::Buffer ubo;
wgpu::BindGroupLayout bindGroupLayout;

// Forward declarations
static void updateUniformBuffer();

// GLFW main framework window
GLFWwindow* fwWindow;


// Mandelbrot implementation functions
void zoom(float scale) // Mandel Zoom func
{
    double x, y; glfwGetCursorPos(fwWindow, &x, &y);
    int w, h;    glfwGetFramebufferSize(fwWindow, &w, &h);

    shaderData.mScaleX  *=  float(1.0)+scale;
    shaderData.mScaleY  *=  float(1.0)+scale;
    shaderData.mTranspX += (float(w)*float(.5) - float(x))/(float(w)*float(.5)) * scale * shaderData.mScaleX;
    shaderData.mTranspY += (float(h)*float(.5) - float(y))/(float(h)*float(.5)) * scale * shaderData.mScaleY;
    updateUniformBuffer();
}

void checkMouseButtonAction()
{
    bool mouseCaptured = ImGui::GetIO().WantCaptureMouse;
    // zoom only if the click is not captured from imgui window
    if(!mouseCaptured && glfwGetMouseButton(fwWindow, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS)
        zoom(-zoomFactor);
    else if(!mouseCaptured && glfwGetMouseButton(fwWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
        zoom(zoomFactor);
}

void appResizeArea(const  uint32_t w, const uint32_t h) // re-adjust aspect-ratio
{
    static int width = w, height = h;
    shaderData.mScaleY+=float(h-float(height)) * shaderData.mScaleY/float(height);
    shaderData.mScaleX+=float(w-float(width))  * shaderData.mScaleX/float(width) ;
    shaderData.wSizeX = width = w; shaderData.wSizeY = height = h;
    updateUniformBuffer();
}

void initMandel()
{
#if defined(__EMSCRIPTEN__)
    // Is necessary to get canvas size, and not default starting windows size
    int w, h;
    glfwGetFramebufferSize((GLFWwindow*)fwWindow, &w, &h);
    uint32_t size = std::min(w, h);
    appResizeArea(size, size);
#else
    appResizeArea(surfaceConfig.width, surfaceConfig.height);
#endif
}

// WGPU VL callbacks
#if !defined(__EMSCRIPTEN__)
static void wgpu_device_lost_callback(const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message)
{
    const char* reasonName = "";
    switch (reason) {
        case wgpu::DeviceLostReason::Unknown:         reasonName = "Unknown";         break;
        case wgpu::DeviceLostReason::Destroyed:       reasonName = "Destroyed";       break;
        case wgpu::DeviceLostReason::InstanceDropped: reasonName = "InstanceDropped"; break;
        case wgpu::DeviceLostReason::FailedCreation:  reasonName = "FailedCreation";  break;
        default:                                      reasonName = "UNREACHABLE";     break;
    }
    printf("%s device message: %s\n", reasonName, message.data);
}

static void wgpu_error_callback(const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message)
{
    const char* errorTypeName = "";
    switch (type) {
        case wgpu::ErrorType::Validation:  errorTypeName = "Validation";      break;
        case wgpu::ErrorType::OutOfMemory: errorTypeName = "Out of memory";   break;
        case wgpu::ErrorType::Unknown:     errorTypeName = "Unknown";         break;
        case wgpu::ErrorType::Internal:    errorTypeName = "Internal";        break;
        default:                           errorTypeName = "UNREACHABLE";     break;
    }
    printf("%s error: %s\n", errorTypeName, message.data);
}

void initWGPU()
{
    wgpu::InstanceDescriptor instanceDescriptor;
    instanceDescriptor.capabilities.timedWaitAnyEnable = true;
    instance = wgpu::CreateInstance(&instanceDescriptor);

    static wgpu::Adapter localAdapter;
    wgpu::RequestAdapterOptions adapterOptions;

    // uncomment to force backend Vulkan (e.g. instead of Metal on MacOS)
    //adapterOptions.backendType = wgpu::BackendType::Vulkan;
#if defined(_WIN32) || defined(WIN32)
    // Windows users: uncomment to force DirectX backend instead of Vulkan
    // adapterOptions.backendType = wgpu::BackendType::D3D12; // to use D3D12 backend in W10/W11
    // adapterOptions.backendType = wgpu::BackendType::D3D11; // to use D3D11 backend in W10/W11
#endif

    auto onRequestAdapter = [](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
        if (status != wgpu::RequestAdapterStatus::Success) {
            printf("Failed to get an adapter: %s\n", message.data);
            return;
        }
        localAdapter = std::move(adapter);
    };

    // Synchronously (wait until) acquire Adapter
    auto waitedAdapterFunc { instance.RequestAdapter(&adapterOptions, wgpu::CallbackMode::WaitAnyOnly, onRequestAdapter) };
    auto waitStatus = instance.WaitAny(waitedAdapterFunc, UINT64_MAX);
    assert(localAdapter != nullptr && waitStatus == wgpu::WaitStatus::Success && "Error on Adapter request");

#ifndef NDEBUG
    wgpu::AdapterInfo info;
    localAdapter.GetInfo(&info);
    printf("Using adapter: \" %s \"\n", info.device.data);
#endif

    // Set device callback functions
    wgpu::DeviceDescriptor deviceDesc;
    deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous, wgpu_device_lost_callback);
    deviceDesc.SetUncapturedErrorCallback(wgpu_error_callback);

    // get device Synchronously
    device = localAdapter.CreateDevice(&deviceDesc);
    assert(device != nullptr && "Error creating the Device");

    surface = wgpu::glfw::CreateSurfaceForWindow(instance, fwWindow);
    assert(surface != nullptr && "Error creating the Surface");

    // Configure the surface.
    wgpu::SurfaceCapabilities capabilities;
    surface.GetCapabilities(localAdapter, &capabilities);
    preferredFormat = capabilities.formats[0];

    surfaceConfig.device          = device;
    surfaceConfig.format          = preferredFormat;
    surfaceConfig.usage           = wgpu::TextureUsage::RenderAttachment;
    surfaceConfig.width           = initialWindowWidth;
    surfaceConfig.height          = initialWindowHeight;
    surfaceConfig.alphaMode       = wgpu::CompositeAlphaMode::Auto;
    surfaceConfig.presentMode     = wgpu::PresentMode::Fifo;

    surface.Configure(&surfaceConfig);
}
#else
// Adapter and device initialization via JS
EM_ASYNC_JS( void, getAdapterAndDeviceViaJS, (),
{
    if (!navigator.gpu) throw Error("WebGPU not supported.");

    const adapter = await navigator.gpu.requestAdapter();
    const device = await adapter.requestDevice();
    Module.preinitializedWebGPUDevice = device;
} );

void initWGPU()
{
    getAdapterAndDeviceViaJS();

    instance = wgpu::CreateInstance(nullptr);
    device   = wgpu::Device(emscripten_webgpu_get_device());
    assert(device != nullptr && "Error creating the Device");

    wgpu::SurfaceDescriptorFromCanvasHTMLSelector html_surface_desc;
    html_surface_desc.selector = "#canvas";
    wgpu::SurfaceDescriptor surface_desc;
    surface_desc.nextInChain   = &html_surface_desc;

    surface         = instance.CreateSurface(&surface_desc);
    preferredFormat = surface.GetPreferredFormat({} /* adapter */);

    surfaceConfig.device = device;
    surfaceConfig.format = preferredFormat;

    //emscripten_glfw_make_canvas_resizable(fwWindow, "window", nullptr);
}
#endif

// Initialize render pipeline
void initRenderPipeline()
{
#if defined(__EMSCRIPTEN__)
    wgpu::ShaderModuleWGSLDescriptor wgslDesc;
    wgslDesc.code = shader;
#else
    wgpu::ShaderSourceWGSL wgslDesc;
    wgslDesc.code = { shader, WGPU_STRLEN };
#endif
    wgpu::ShaderModuleDescriptor shaderDescriptor;
    shaderDescriptor.nextInChain = &wgslDesc;
    wgpu::ShaderModule module = device.CreateShaderModule(&shaderDescriptor);

    wgpu::RenderPipelineDescriptor descPipeline;
    descPipeline.layout = nullptr;
    descPipeline.vertex.module = module;
    descPipeline.vertex.bufferCount = 0;

    // Set primitive state
    descPipeline.primitive.topology         = wgpu::PrimitiveTopology::TriangleStrip;
    descPipeline.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
    descPipeline.primitive.frontFace        = wgpu::FrontFace::CCW;
    descPipeline.primitive.cullMode         = wgpu::CullMode::None;

    // BlendComponent
    wgpu::BlendComponent blendComponent {
        .operation = wgpu::BlendOperation::Add,
        .srcFactor = wgpu::BlendFactor::One,
        .dstFactor = wgpu::BlendFactor::Zero,
    };
    // Blend
    wgpu::BlendState blend {
        .color = blendComponent,
        .alpha = blendComponent,
    };
    // color target attribs
    wgpu::ColorTargetState colorTarget {
        .nextInChain = nullptr,
        .format      = wgpu::TextureFormat(preferredFormat),
        .blend       = &blend,
        .writeMask   = wgpu::ColorWriteMask::All,
    };

    // Fragment Shader
    wgpu::FragmentState fragment;
    fragment.module = module;
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    descPipeline.fragment = &fragment;

    // Uniform Buffer
    wgpu::BufferDescriptor bufferDesc {
        .nextInChain      = nullptr,
        .label            = "uboData",
        .usage            = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform,
        .size             = sizeof(shaderData_),
        .mappedAtCreation = false,
    };
    ubo = device.CreateBuffer(&bufferDesc);

    // @group(0) @binding(0) var<uniform> shaderData
    wgpu::BindGroupLayoutEntry bindGroupLayoutEntry;
    bindGroupLayoutEntry.binding               = 0;
    bindGroupLayoutEntry.visibility            = wgpu::ShaderStage::Fragment;
    bindGroupLayoutEntry.buffer.type           = wgpu::BufferBindingType::Uniform;
    bindGroupLayoutEntry.buffer.minBindingSize = sizeof(shaderData_);

    // BindGroupLayout
    wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc;
    bindGroupLayoutDesc.entryCount = 1;
    bindGroupLayoutDesc.entries = &bindGroupLayoutEntry;
    bindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);

    // pipelineLayout
    wgpu::PipelineLayoutDescriptor layoutDesc;
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &bindGroupLayout;
    wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&layoutDesc);
    descPipeline.layout = pipelineLayout;

    // Create Render Pipeline
    pipeline = device.CreateRenderPipeline(&descPipeline);
}

static void updateUniformBuffer() {
    device.GetQueue().WriteBuffer( ubo, 0, &shaderData, sizeof( shaderData_ ) );
}

void resizeSurface(const uint32_t width, const uint32_t height)
{
    surfaceConfig.width  = width;
    surfaceConfig.height = height;

    ImGui_ImplWGPU_InvalidateDeviceObjects();

    surface.Configure(&surfaceConfig);

    ImGui_ImplWGPU_CreateDeviceObjects();
}

WGPUTexture checkTextureStatus()
{
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface.Get(), &surfaceTexture);

    switch ( surfaceTexture.status ) {
#if !defined(__EMSCRIPTEN__)
        case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
            break;
        case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
#else
        case WGPUSurfaceGetCurrentTextureStatus_Success:
            break;
#endif
        case WGPUSurfaceGetCurrentTextureStatus_Timeout:
        case WGPUSurfaceGetCurrentTextureStatus_Outdated:
        case WGPUSurfaceGetCurrentTextureStatus_Lost:
        // if the status is NOT Optimal let's try to reconfigure the surface
        {
            if (surfaceTexture.texture)
                wgpuTextureRelease(surfaceTexture.texture);
#ifndef NDEBUG
            printf("Surface texture reconfigure: status %d\n", surfaceTexture.status);
#endif
            int width, height;
            glfwGetFramebufferSize(fwWindow, &width, &height);
            if ( width > 0 && height > 0 )
            {
                surfaceConfig.width  = width;
                surfaceConfig.height = height;

                surface.Configure(&surfaceConfig);
            }
            return nullptr;
        }
        default:
            assert(!"Unexpected Surface Texture status error\n");
            return nullptr;
    }
    return surfaceTexture.texture;
}

void renderImGui()
{
    // Start the Dear ImGui frame
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // ImGui Windows
    ImGui::SetNextWindowSize(ImVec2(270, 130), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_FirstUseEver);
    bool isVisible = true;
    bool isModified = false;
    if(ImGui::Begin("wgpuMandel", &isVisible)) {
        ImGui::BeginGroup(); {
            isModified |= ImGui::SliderInt("Iterations",&shaderData.iterations,8,2'000);
            isModified |= ImGui::SliderInt("HSL shades",&shaderData.nColors,2,3'000);
            isModified |= ImGui::SliderFloat("HSL shift",&shaderData.shift,0.0,1.0);
            ImGui::Text("average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

        } ImGui::EndGroup();
    } ImGui::End();

    // Rendering
    ImGui::Render();

    if(isModified) updateUniformBuffer(); // if data are changed, is necessary to update UBO
}

void mainLoop()
{

    // check for click: Mandelbrot zoomIn / zoomOut
    checkMouseButtonAction();

    // React to changes in screen size
    int width, height;
    glfwGetFramebufferSize(fwWindow, &width, &height);
    if (width != surfaceConfig.width || height != surfaceConfig.height)  {
        resizeSurface(width, height);
        appResizeArea(width, height); // re-adjust Mandelbrot aspect-ratio
    }

    wgpu::Texture texture = checkTextureStatus();
    if(!texture) return;

    renderImGui();

    // TextureViewDescriptor
    wgpu::TextureViewDescriptor descTextureView = {
        .nextInChain     = nullptr,
        .label           = "appTextureViewDescriptor",
        .format          = preferredFormat,
        .dimension       = wgpu::TextureViewDimension::e2D,
        .baseMipLevel    = 0,
        .mipLevelCount   = 1,
        .baseArrayLayer  = 0,
        .arrayLayerCount = 1,
        .aspect          = wgpu::TextureAspect::Undefined,
#if !defined(__EMSCRIPTEN__)
        .usage           = wgpu::TextureUsage::RenderAttachment,
#endif
    };
    // colorAttachments
    wgpu::RenderPassColorAttachment colorAttachments {
        .nextInChain     = nullptr,
        .view            = texture.CreateView(&descTextureView),
        .depthSlice      = wgpu::kDepthSliceUndefined,
        .resolveTarget   = nullptr,
        .loadOp          = wgpu::LoadOp::Clear,
        .storeOp         = wgpu::StoreOp::Store,
        .clearValue      = {},
    };
    // RenderPassDescriptor
    wgpu::RenderPassDescriptor descRenderPass {
        .nextInChain            = nullptr,
        .label                  = "appRenderPassDescriptor",
        .colorAttachmentCount   = 1,
        .colorAttachments       = &colorAttachments,
        .depthStencilAttachment = nullptr,
        .occlusionQuerySet      = nullptr,
        .timestampWrites        = nullptr,
    };
    // CommandEncoder
    wgpu::CommandEncoderDescriptor descEncoder;
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder(&descEncoder);

    // RenderPassEncoder
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&descRenderPass);
    pass.SetPipeline(pipeline);

    // Bind the uniform buffer.
    wgpu::BindGroupEntry entryBindingGroup {
        .nextInChain    = nullptr,
        .binding        = 0,
        .buffer         = ubo,
        .offset         = 0,
        .size           = sizeof( shaderData_ ),
    };
    // BindGroup descriptor
    wgpu::BindGroupDescriptor descBindGroup {
        .nextInChain    = nullptr,
        .label          = nullptr,
        .layout         = bindGroupLayout,
        .entryCount     = 1,
        .entries        = &entryBindingGroup,
    };
    // BindGroup
    wgpu::BindGroup bindGroup  = device.CreateBindGroup(&descBindGroup);
    pass.SetBindGroup(0, bindGroup, 0, nullptr );

    pass.Draw(4, 1, 0, 0);

    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass.Get()); // add Imgui RenderPass data
    pass.End();

    wgpu::CommandBufferDescriptor cmd_buffer_desc;
    wgpu::CommandBuffer cmd_buffer = encoder.Finish(&cmd_buffer_desc);
    device.GetQueue().Submit(1, &cmd_buffer);

#if !defined(__EMSCRIPTEN__)
    surface.Present();
    // Tick needs to be called in Dawn to display validation errors
    device.Tick();
#endif
}

void initImGui()
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOther(fwWindow, true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(fwWindow, "#canvas");
#endif
    ImGui_ImplWGPU_InitInfo init_info;
    init_info.Device = device.Get();
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = (WGPUTextureFormat) preferredFormat;
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&init_info);

#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
#endif
}

// Main code
int main(int, char**)
{
    glfwSetErrorCallback([](int code, const char* message) { printf("GLFW Error %d: %s\n", code, message); });
    if (!glfwInit()) return -1;

    // Make sure GLFW does not initialize any graphics context.
    // This needs to be done explicitly later.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    fwWindow = glfwCreateWindow(initialWindowWidth, initialWindowHeight, appTitle, nullptr, nullptr);
    if (fwWindow == nullptr) return -2;

    initWGPU();

    glfwShowWindow(fwWindow);

    initImGui();

    initRenderPipeline();
    initMandel();

#ifdef __EMSCRIPTEN__
    // Main loop
    emscripten_set_main_loop([] {
        glfwPollEvents();   // Poll and handle events (inputs, window resize, etc.)
        mainLoop();
    }, 0, false);
#else
    // Main loop
    while (!glfwWindowShouldClose(fwWindow)) {
        glfwPollEvents();   // Poll and handle events (inputs, window resize, etc.)
        if (glfwGetWindowAttrib(fwWindow, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            return -3;
        }
        mainLoop();
    }
    // Cleanup
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

#endif

    // All WGPU class destructors release the own object
    glfwDestroyWindow(fwWindow);
    glfwTerminate();
    return 0;
}
