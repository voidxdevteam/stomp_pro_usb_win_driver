# SPDX-FileCopyrightText: 2026 Sonulab
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)

project(SonulabStompProUsbDriver VERSION 0.2.1 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Sonulab StompPRO USB Driver is a Windows-only project")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio" CACHE PATH "Path to the Steinberg ASIO SDK")
if(NOT EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
    message(FATAL_ERROR "ASIO SDK not found. Clone it into third_party/asio or set ASIO_SDK_DIR.")
endif()

add_library(SonulabStompProDriver SHARED
    src/DllMain.cpp
    src/SonulabAsio.cpp
    src/SonulabAsio.hpp
    src/WasapiBackend.cpp
    src/WasapiBackend.hpp
    src/SonulabStompProDriver.def
)

target_include_directories(SonulabStompProDriver PRIVATE "${ASIO_SDK_DIR}/common" src)
target_compile_definitions(SonulabStompProDriver PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SonulabStompProDriver PRIVATE ole32 uuid propsys avrt cfgmgr32)
set_target_properties(SonulabStompProDriver PROPERTIES
    OUTPUT_NAME "SonulabStompProDriver"
    PREFIX ""
)

add_executable(sonulab_asio_smoke tools/SmokeTest.cpp)
target_include_directories(sonulab_asio_smoke PRIVATE "${ASIO_SDK_DIR}/common" src)
target_compile_definitions(sonulab_asio_smoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(sonulab_asio_smoke PRIVATE ole32)

if(MSVC)
    set_property(TARGET SonulabStompProDriver sonulab_asio_smoke PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(SonulabStompProDriver PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(sonulab_asio_smoke PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()
