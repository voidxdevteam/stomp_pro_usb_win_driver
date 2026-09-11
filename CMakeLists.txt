cmake_minimum_required(VERSION 3.24)

project(SonulabASIO VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Sonulab ASIO is a Windows-only project")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio" CACHE PATH "Path to the Steinberg ASIO SDK")
if(NOT EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
    message(FATAL_ERROR "ASIO SDK not found. Clone it into third_party/asio or set ASIO_SDK_DIR.")
endif()

add_library(SonulabASIO SHARED
    src/DllMain.cpp
    src/SonulabAsio.cpp
    src/SonulabAsio.hpp
    src/WasapiBackend.cpp
    src/WasapiBackend.hpp
    src/SonulabASIO.def
)

target_include_directories(SonulabASIO PRIVATE "${ASIO_SDK_DIR}/common" src)
target_compile_definitions(SonulabASIO PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SonulabASIO PRIVATE ole32 uuid propsys avrt cfgmgr32)
set_target_properties(SonulabASIO PROPERTIES
    OUTPUT_NAME "SonulabASIO"
    PREFIX ""
)

add_executable(sonulab_asio_smoke tools/SmokeTest.cpp)
target_include_directories(sonulab_asio_smoke PRIVATE "${ASIO_SDK_DIR}/common" src)
target_compile_definitions(sonulab_asio_smoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(sonulab_asio_smoke PRIVATE ole32)

if(MSVC)
    set_property(TARGET SonulabASIO sonulab_asio_smoke PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(SonulabASIO PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(sonulab_asio_smoke PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()
