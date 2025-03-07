# WGPU / WebGPU ImGui Mandelbrot example

This is a very short example that use **WebGPU** as graphics backend and (also) **ImGui** as User Interface.
It was written in C++ and can be compiled in native mode (for a standalone desktop application) or with EMSCRIPTEN to perform it via the web browser

<table style="text-align: center; float:center;  width:100%; table-layout: fixed; ">
<tr>
<td align="center">
<img style="height: 400px; width=auto;" src="https://brutpitt.github.io/myRepos/wgpu_examples/screenshots/Screenshot_20250313_052144.png"/></a>
</td>


<td align="center">
<img style="height: 400px; width=auto;" src="https://brutpitt.github.io/myRepos/wgpu_examples/screenshots/Screenshot_20250313_052339.png"/></a>
</td>
</tr> 
</table>


**Live online WebGPU Mandelbrot example** (with and w/o ImGui user interface):
- [wgpu - Mandelbror - GLFW example](https://brutpitt.github.io/myRepos/wgpu_examples/mandel_GLFW/wgpu_mandelbrot.html) 
- [wgpu - Mandelbror - SDL2 example](https://brutpitt.github.io/myRepos/wgpu_examples/mandel_SDL2/wgpu_mandelbrot.html)
- [wgpu+imgui - Mandelbror - GLFW example](https://brutpitt.github.io/myRepos/wgpu_examples/mandel_imgui_GLFW/wgpu_mandelbrot.html) 
- [wgpu+imgui - Mandelbror - SDL2 example](https://brutpitt.github.io/myRepos/wgpu_examples/mandel_imgui_SDL2/wgpu_mandelbrot.html)


Simply: **L_Button-down** - ZoomIn, **R_Button-down** - ZoomOut
**Obviously is necessary to use a brows with WebGPU capability: e.g. Chrome-Canary, FireFox Nightly, Safari Technology Preview ...*

These examples use Google DAWN (as WGPU SDK) to build native executable (CMakeLists.txt). 


WGPU /WebGPU is still in the experimental phase and in continuous development, you can view the current [status of these examples](https://github.com/ocornut/imgui/pull/8381#issuecomment-2696124647): what does it work and what it doesn't work, and also in which O.S. it was tested and with wath backends (Vulkan / DX / Metal)   

Anyway feel free to open a issue

## How to Build

Clone using `git clone --recursive https://github.com/BrutPitt/wgpu_imgui_mandelbrot_example`

It's necessary to have installed **SDL2** and / or **GLFW** (development package)

### Native - Desktop application

- clone Google DAWN (WGPU SDK): `git clone https://dawn.googlesource.com/dawn`
- Install Ninja build system (DAWN requires)
- from any folder example type: `cmake -B build -DIMGUI_DAWN_DIR=path/where/cloned/dawn` (absolute or relative path) 
- then `cmake --build build`

### Emscripten - Web Browser application (WASM)

- Install Emscripten SDK following the instructions: https://emscripten.org/docs/getting_started/downloads.html
- Install Ninja build system 
- `emcmake cmake -G Ninja -B build`
- `cmake --build build`

then

- `emrun build/wgpu_mandelbrot.html`

or

- `python -m http.server` (in a `build` folder)... then open WGPU browser with url: http://localhost:8000/wgpu_mandelbrot.html

### *notes*

Any folder has two files `main_js_inline.cpp` and `main_oldStyle.cpp`: they do the same thing in Emscripten, but with two different techniques. (no differences in wgpu native)
- `main_js_inline.cpp`: acquire `Adapter` and `Device` using a JS calls (via `EM_ASYNC_JS` macro) 
- `main_oldStyle.cpp` : acquire `Adapter` and `Device` using old callbacks alredy mofdified in wgpu native, but not yet in EMSCRIPTEN

Indeed EMSCRIPTEN still uses some old functions (already changed in DAWN/WGPU native) and Google DAWN maintain a private fork of the Emscripten WebGPU bindings **emdawnwebgpu** (a step forward) to speedup the WebGPU evolution ([emdawnwebgpu is available in DAWN repo](https://dawn.googlesource.com/dawn/+/refs/heads/main/src/emdawnwebgpu/)) 
**waiting to know how to use it for external (no DAWN internal) examples*


"default" (not offcial) WGPU **ImGui** examples, currently in PullRequest, and available here: 
- [imgui_example_glfw_wgpu](https://github.com/BrutPitt/imgui/tree/master/examples/example_glfw_wgpu) ==> [Live demo](https://brutpitt.github.io/myRepos/imgui/example_glfw_wgpu/index.html)
- [imgui_example_sdl2_wgpu](https://github.com/BrutPitt/imgui/tree/master/examples/example_sdl2_wgpu) ==> [Live demo](https://brutpitt.github.io/myRepos/imgui/example_sdl2_wgpu/index.html)


**they are not yet available in official ImGui branch*


You can follow [ImGui WebGPU examples Pull Request](https://github.com/ocornut/imgui/pull/8381)










