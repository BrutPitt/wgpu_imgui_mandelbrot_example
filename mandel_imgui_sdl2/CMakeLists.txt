# Building for desktop (WebGPU-native) with Dawn:
#  1. git clone https://github.com/google/dawn dawn
#  2. cmake -B build -DCURRENT_DAWN_DIR=dawn
#  3. cmake --build build

# Building for Emscripten:
#  1. Install Emscripten SDK following the instructions: https://emscripten.org/docs/getting_started/downloads.html
#  2. Install Ninja build system
#  3. emcmake cmake -G Ninja -B build
#  3. cmake --build build
#  4. emrun build/wgpu_mandelbrot.html
# or
#  4. python -m http.server ... then open WGPU browser with url: http://localhost:8000/wgpu_mandelbrot.html

cmake_minimum_required(VERSION 3.16) #DAWN required
project(wgpu_mandelbrot_example)

set(APP_NAME wgpu_mandelbrot)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)

# Dear ImGui
set(IMGUI_DIR ../imgui)

# Libraries
if(EMSCRIPTEN)
  set(ENABLE_EMSCRIPTEN ON)
  add_compile_options(-sDISABLE_EXCEPTION_CATCHING=1)
else()
  # Dawn wgpu desktop
  set(DAWN_FETCH_DEPENDENCIES ON)
  set(CURRENT_DAWN_DIR CACHE PATH "Path to Dawn repository")
  if (NOT CURRENT_DAWN_DIR)
    message(FATAL_ERROR "Please specify the Dawn repository by setting CURRENT_DAWN_DIR")
  endif()

  option(DAWN_FETCH_DEPENDENCIES "Use fetch_dawn_dependencies.py as an alternative to using depot_tools" ON)

  # Dawn builds many things by default - disable things we don't need
  option(DAWN_BUILD_SAMPLES "Enables building Dawn's samples" OFF)
  option(TINT_BUILD_CMD_TOOLS "Build the Tint command line tools" OFF)
  option(TINT_BUILD_DOCS "Build documentation" OFF)
  option(TINT_BUILD_TESTS "Build tests" OFF)
  if (NOT APPLE)
    option(TINT_BUILD_MSL_WRITER "Build the MSL output writer" OFF)
  endif()
  if(WIN32)
    option(TINT_BUILD_SPV_READER "Build the SPIR-V input reader" OFF)
    option(TINT_BUILD_WGSL_READER "Build the WGSL input reader" ON)
    option(TINT_BUILD_GLSL_WRITER "Build the GLSL output writer" OFF)
    option(TINT_BUILD_GLSL_VALIDATOR "Build the GLSL output validator" OFF)
    option(TINT_BUILD_SPV_WRITER "Build the SPIR-V output writer" OFF)
    option(TINT_BUILD_WGSL_WRITER "Build the WGSL output writer" ON)
  endif()

  # check if WAYLAND is the current Session Type and enable DAWN_USE_WAYLAND Wayland option @compile time
  # You can override this using: cmake -DDAWN_USE_WAYLAND=X (X = ON | OFF)
  if(UNIX)
    if ($ENV{XDG_SESSION_TYPE} MATCHES wayland)
      option(DAWN_USE_WAYLAND "Enable support for Wayland surface" ON)
    endif()
  endif()

  set(TARGET_DAWN_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/dawn CACHE STRING "Directory where to build DAWN")
  add_subdirectory("${CURRENT_DAWN_DIR}" "${TARGET_DAWN_DIRECTORY}" EXCLUDE_FROM_ALL)

  find_package(SDL2 REQUIRED)

  set(LIBRARIES webgpu_dawn webgpu_cpp ${SDL2_LIBRARIES})
endif()

add_executable(${APP_NAME}
  main.cpp
  ../sdl2wgpu.cpp
  # backend files
  ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
  ${IMGUI_DIR}/backends/imgui_impl_wgpu.cpp
  # Dear ImGui files
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
)

target_include_directories(${APP_NAME} PUBLIC ${SDL2_INCLUDE_DIRS})
target_include_directories(${APP_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/..)
target_include_directories(${APP_NAME} PUBLIC
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)

IF(NOT EMSCRIPTEN) # it's necessary for IMGUI
  target_compile_definitions(${APP_NAME} PUBLIC "IMGUI_IMPL_WEBGPU_BACKEND_DAWN")
endif()

target_link_libraries(${APP_NAME} LINK_PUBLIC ${LIBRARIES})

# Emscripten settings
if(EMSCRIPTEN)
  set(CMAKE_EXECUTABLE_SUFFIX ".html")

  target_compile_options(${APP_NAME} PUBLIC "-sUSE_SDL=2" )
  target_link_options(${APP_NAME} PRIVATE
    "-sUSE_WEBGPU=1"
    "-sWASM=1"
    "-sASYNCIFY"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sNO_EXIT_RUNTIME=0"
    "-sASSERTIONS=1"
    "-sDISABLE_EXCEPTION_CATCHING=1"
    "-sNO_FILESYSTEM=1"
    "-sUSE_SDL=2"
    "--shell-file=${CMAKE_SOURCE_DIR}/../veryMinimal.html"
  )
endif()
