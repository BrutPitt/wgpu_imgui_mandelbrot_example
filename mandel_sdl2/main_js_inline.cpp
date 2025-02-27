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

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>
#endif

#define SDL_MAIN_HANDLED
#include "sdl2wgpu.h"
#include <webgpu/webgpu.h>
#include <webgpu/webgpu_cpp.h>

#if defined (__linux__) || defined (__APPLE__)  // POSIX
#include <unistd.h>
    void waitFor(uint32_t usec = 16000) { usleep(usec); }
#elif defined(_WIN32) || defined(_WIN64)        // WINDOWS
    void waitFor(uint32_t usec = 16000)  Sleep(static_cast<uint32_t>(usecs / 1000));
#elif defined(__EMSCRIPTEN__)
#else
    void waitFor(uint32_t usec = 16000) {}
    #warning "No sleep timer!"
#endif

// Initial App state
static const uint32_t initialWindowWidth {512};
static const uint32_t initialWindowHeight {512};
static const char *appTitle {"wgpu - Mandelbrot - SDL2 example"};

// Global WebGPU required
wgpu::Instance              instance;
wgpu::Device                device;
wgpu::Surface               surface;
wgpu::Queue                 queue;
wgpu::TextureFormat         preferredFormat { wgpu::TextureFormat::Undefined };  // current undefined, but set from SurfaceCapabilities
wgpu::SurfaceConfiguration  surfaceConfig;

// Pipeline related objs
wgpu::RenderPipeline pipeline;
wgpu::Buffer ubo;
wgpu::BindGroup bindGroup;
wgpu::PipelineLayout pipelineLayout;
wgpu::BindGroupLayout bindGroupLayout;

// Forward declarations
static void updateUniformBuffer();

// GLFW main framework window
SDL_Window* fwWindow;

// Mandelbrot data
struct alignas(16) shaderData_ {
    float mScaleX = 1.5, mScaleY = 1.5;
    float mTranspX = -.75, mTranspY = 0.0;
    float wSizeX = initialWindowWidth, wSizeY = initialWindowHeight;
    int32_t iterations = 256, nColors = 256;
    float shift = 0.0;
} shaderData;
const float zoomFactor = .05;

auto shader = R"(
    struct shaderData {
        mScale      : vec2f,
        mTransp     : vec2f,
        wSize       : vec2f,
        iterations  : i32,
        nColors     : i32,
        shift       : f32,
    };
    @group(0) @binding(0) var<uniform> sd : shaderData;

    @vertex fn vs(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4f
    {
        // use "in-place" position (w/o vetrex buffer): 4 vetex / triangleStrip
        var pos = array( vec2f(-1.0,  1.0),
                         vec2f(-1.0, -1.0),
                         vec2f( 1.0,  1.0),
                         vec2f( 1.0, -1.0)  );
        return vec4f(pos[VertexIndex], 0, 1);
    }

    fn hsl2rgb(hsl: vec3f) -> vec3f
    {
        let H: f32 = fract(hsl.x);
        let rgb: vec3f = clamp(vec3f(abs(H * 6. - 3.) - 1., 2. - abs(H * 6. - 2.), 2. - abs(H * 6. - 4.)), vec3f(0.0), vec3f(1.0));
        let C: f32 = (1. - abs(2. * hsl.z - 1.)) * hsl.y;
        return (rgb - 0.5) * C + hsl.z;
    }

    @fragment fn fs(@builtin(position) position: vec4f) -> @location(0) vec4f
    {
        let c: vec2f = sd.mTransp - sd.mScale + position.xy / sd.wSize * (sd.mScale * 2.);
        var z: vec2f = vec2f(0.);
        var clr: f32 = 0.;

        for (var i: i32 = 1; i < sd.iterations; i = i + 1) {
            z = vec2f(z.x * z.x - z.y * z.y, 2. * z.x * z.y) + c;
            if (dot(z, z) > 16.) {
                clr = f32(i) / f32(sd.nColors);
                break;
            }
        }

        if (clr > 0.0) { return vec4f(hsl2rgb(vec3f(sd.shift + clr, 1., 0.5)), 1.); }
        else           { return vec4f(0.); }
    }
)";


// Mandelbrot implementation functions
void zoom(float scale) // Mandel Zoom func
{
    int x, y; SDL_GetMouseState(&x, &y);
    int w, h; SDL_GetWindowSize(fwWindow, &w, &h);

    shaderData.mScaleX  *=  float(1.0)+scale;
    shaderData.mScaleY  *=  float(1.0)+scale;
    shaderData.mTranspX += (float(w)*float(.5) - float(x))/(float(w)*float(.5)) * scale * shaderData.mScaleX;
    shaderData.mTranspY += (float(h)*float(.5) - float(y))/(float(h)*float(.5)) * scale * shaderData.mScaleY;
    updateUniformBuffer();
}

void checkMouseButtonAction()
{
  int x, y;
    if(SDL_GetMouseState(&x, &y) == SDL_BUTTON(SDL_BUTTON_LEFT))
        zoom(-zoomFactor);
    else if(SDL_GetMouseState(&x, &y) == SDL_BUTTON(SDL_BUTTON_RIGHT))
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
    SDL_GetWindowSize(fwWindow, &w, &h);
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

    surface = wgpu::Surface(SDL_getWGPUSurface(instance.Get(), fwWindow));
    assert(surface != nullptr && "Error creating the Surface");

    // Configure the surface.
    wgpu::SurfaceCapabilities capabilities;
    surface.GetCapabilities(localAdapter, &capabilities);
    preferredFormat = capabilities.formats[0];

    surfaceConfig.width       = initialWindowWidth;
    surfaceConfig.height      = initialWindowHeight;
    surfaceConfig.device      = device;
    surfaceConfig.format      = preferredFormat;

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

    wgpu::RenderPipelineDescriptor pipelineDesc;
    pipelineDesc.layout = nullptr;
    pipelineDesc.vertex.module = module;
    pipelineDesc.vertex.bufferCount = 0;

    // Set primitive state
    pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleStrip;
    pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
    pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    pipelineDesc.primitive.cullMode = wgpu::CullMode::None;

    // struct BlendComponent already with default set: src=One, dst=Zero, op=Add
    wgpu::BlendComponent blendComponent;
    wgpu::BlendState blend;
    blend.color = blendComponent;
    blend.alpha = blendComponent;

    // color target attribs
    wgpu::ColorTargetState colorTarget;
    colorTarget.format = wgpu::TextureFormat(preferredFormat);
    colorTarget.blend = &blend;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    // Fragment Shader
    wgpu::FragmentState fragment;
    fragment.module = module;
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    pipelineDesc.fragment = &fragment;

    // Uniform Buffer
    wgpu::BufferDescriptor bufferDesc;
    bufferDesc.size = sizeof(shaderData_);
    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
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
    pipelineLayout = device.CreatePipelineLayout(&layoutDesc);
    pipelineDesc.layout = pipelineLayout;

    // Create Render Pipeline
    pipeline = device.CreateRenderPipeline(&pipelineDesc);
}

static void updateUniformBuffer() {
    queue.WriteBuffer( ubo, 0, &shaderData, sizeof( shaderData_ ) );
}

void resizeSurface(const uint32_t width, const uint32_t height)
{
    surfaceConfig.width  = width;
    surfaceConfig.height = height;

    surface.Configure(&surfaceConfig);
}

void mainLoop()
{
    // check for click: Mandelbrot zoomIn / zoomOut
    checkMouseButtonAction();

    // React to changes in screen size
    int width, height;
    SDL_GetWindowSize(fwWindow, &width, &height);
    if (width != surfaceConfig.width || height != surfaceConfig.height)
    {
        resizeSurface(width, height);
        appResizeArea(width, height); // re-adjust Mandelbrot aspect-ratio
    }

#ifndef __EMSCRIPTEN__
    // Tick needs to be called in Dawn to display validation errors
    device.Tick();
#endif
    wgpu::SurfaceTexture surfaceTexture;
    surface.GetCurrentTexture(&surfaceTexture);

    wgpu::TextureViewDescriptor viewDescriptor;
    viewDescriptor.format          = preferredFormat;
    viewDescriptor.dimension       = wgpu::TextureViewDimension::e2D;

    wgpu::RenderPassColorAttachment color_attachments;
    color_attachments.loadOp     = wgpu::LoadOp::Clear;
    color_attachments.storeOp    = wgpu::StoreOp::Store;
    color_attachments.clearValue = {};
    color_attachments.view       = surfaceTexture.texture.CreateView(&viewDescriptor);

    wgpu::RenderPassDescriptor render_pass_desc;
    render_pass_desc.colorAttachmentCount   = 1;
    render_pass_desc.colorAttachments       = &color_attachments;
    render_pass_desc.depthStencilAttachment = nullptr;

    wgpu::CommandEncoderDescriptor enc_desc;
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder(&enc_desc);

    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&render_pass_desc);
    pass.SetPipeline(pipeline);

    // Bind the uniform buffer.
    wgpu::BindGroupEntry bindingEntry;
    bindingEntry.binding = 0;
    bindingEntry.buffer  = ubo;
    bindingEntry.offset  = 0;
    bindingEntry.size    = sizeof( shaderData_ );

    wgpu::BindGroupDescriptor bindGroupDescriptor;
    bindGroupDescriptor.layout     = bindGroupLayout;
    bindGroupDescriptor.entryCount = 1;
    bindGroupDescriptor.entries    = &bindingEntry;
    wgpu::BindGroup bindGroup        = device.CreateBindGroup(&bindGroupDescriptor );

    pass.SetBindGroup(0, bindGroup, 0, nullptr );

    pass.Draw(4, 1, 0, 0);

    pass.End();

    wgpu::CommandBufferDescriptor cmd_buffer_desc;
    wgpu::CommandBuffer cmd_buffer = encoder.Finish(&cmd_buffer_desc);
    queue.Submit(1, &cmd_buffer);
}

// Main code
int main(int, char**)
{
#if !defined(__EMSCRIPTEN__)
    #if defined(__linux__)
    #warning "LINUX USER: Please read here..."
    // Currently sdl2wgpu works only X11 on Xorg or/and wayland on wayland: X11 on wayland crash with seg-fault (x11->xGetWindowAttributes)
    // if not specified SDL_getWGPUSurface prefers always X11 also on Wayland, uncomment to force to use Wayland
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "wayland");  // or (outside code) export SDL_VIDEODRIVER=wayland environment variable
    #endif                                            // or    "      "    export SDL_VIDEODRIVER=$XDG_SESSION_TYPE to get the current session type
#endif

    // Init SDL
    SDL_Init(SDL_INIT_VIDEO);
    fwWindow = SDL_CreateWindow(appTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, initialWindowWidth, initialWindowHeight, SDL_WINDOW_RESIZABLE);

    initWGPU();

    queue = device.GetQueue();
    initRenderPipeline();
    initMandel();

#ifdef __EMSCRIPTEN__
    // Main loop
    emscripten_set_main_loop([]() { mainLoop(); }, 0, false);
#else
    SDL_Event event;
    bool canCloseWindow = false;
    // Main loop
    while (!canCloseWindow) {
        while (SDL_PollEvent(&event)) // Poll and handle events (inputs, window resize, etc.)
        {
            if (event.type == SDL_QUIT ||
               (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(fwWindow)))
                canCloseWindow = true;
        }
        mainLoop();
        surface.Present();
        // this is really a "SLEEP timer" (16ms = 60 FPS), it's present in all DAWN examples
        waitFor(16000);     // w/o (sometime) you get "Device Lost" error
    }
#endif

    // All class destructors release the own object
    // As in DAWN (internal) examples and contrarily to how they occur in wgpu_glfw_example (where they are released the created objects)
    // here don't is called glfwTerminate and glfwQuit, because all objects are released after the "return"
    return 0;
}
