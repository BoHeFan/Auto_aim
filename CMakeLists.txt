cmake_minimum_required(VERSION 3.20)
project(AutoAim VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Build Type Configuration
# =============================================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# =============================================================================
# Dependencies - Local pre-built packages
# =============================================================================
set(DEPS_DIR "${CMAKE_SOURCE_DIR}/deps" CACHE PATH "Dependencies directory")

# --- OpenCV ---
find_package(OpenCV QUIET)
if(NOT OpenCV_FOUND)
    # Fallback to manual discovery from deps folder
    set(OpenCV_DIR "${DEPS_DIR}/opencv/build" CACHE PATH "OpenCV build directory")
    if(EXISTS "${OpenCV_DIR}/OpenCVConfig.cmake")
        find_package(OpenCV REQUIRED)
    else()
        # Try opencv_world approach - find headers and libs manually
        set(OPENCV_INCLUDE_DIRS "${DEPS_DIR}/opencv/build/include")
        set(OPENCV_LIB_DIR "${DEPS_DIR}/opencv/build/x64/vc17/lib")
        
        # Determine library name based on build type
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(OPENCV_LIB_NAME "opencv_world4100d")
        else()
            set(OPENCV_LIB_NAME "opencv_world4100")
        endif()
        
        # Search for available OpenCV world libs to auto-detect version
        file(GLOB OPENCV_LIBS "${DEPS_DIR}/opencv/build/x64/vc*/lib/opencv_world*.lib")
        if(OPENCV_LIBS)
            list(GET OPENCV_LIBS 0 _first_lib)
            get_filename_component(_lib_name "${_first_lib}" NAME)
            message(STATUS "Found OpenCV lib: ${_lib_name}")
        endif()
        
        # Also check vc16 (VS2019) if vc17 not found
        if(NOT EXISTS "${OPENCV_LIB_DIR}")
            set(OPENCV_LIB_DIR "${DEPS_DIR}/opencv/build/x64/vc16/lib")
        endif()
    endif()
endif()

if(OpenCV_FOUND)
    message(STATUS "OpenCV found: ${OpenCV_VERSION}")
else()
    message(STATUS "Using manual OpenCV configuration from: ${DEPS_DIR}/opencv")
endif()

# --- ONNX Runtime ---
set(ONNXRUNTIME_DIR "${DEPS_DIR}/onnxruntime" CACHE PATH "ONNX Runtime directory")
set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_DIR}/include" CACHE PATH "ONNX Runtime include dir")
set(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_DIR}/lib" CACHE PATH "ONNX Runtime lib dir")

# =============================================================================
# Source Files
# =============================================================================
set(SOURCES
    src/main.cpp
    src/core/config.cpp
    src/core/tracker.cpp
    src/input/fuzzy_pid.cpp
    src/input/mouse_controller.cpp
    src/capture/screen_capturer.cpp
)

set(HEADERS
    src/core/types.hpp
    src/core/config.hpp
    src/core/tracker.hpp
    src/core/detection.hpp
    src/core/aim_assistant.hpp
    src/input/fuzzy_pid.hpp
    src/input/mouse_controller.hpp
    src/capture/screen_capturer.hpp
    src/ui/control_panel.hpp
    src/utils/constants.hpp
    src/utils/key_names.hpp
    src/third_party/cvui.h
    src/third_party/hungarian.hpp
    src/third_party/InputSimulator.hpp
)

# =============================================================================
# Executable
# =============================================================================
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

# =============================================================================
# Include Directories
# =============================================================================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(OpenCV_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})
else()
    target_include_directories(${PROJECT_NAME} PRIVATE "${DEPS_DIR}/opencv/build/include")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})

# =============================================================================
# Link Libraries
# =============================================================================
# Windows system libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11
    dxgi
    Comdlg32
)

# OpenCV
if(OpenCV_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCV_LIBS})
else()
    # Manual link for opencv_world
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_libraries(${PROJECT_NAME} PRIVATE debug opencv_world4100d optimized opencv_world4100)
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE opencv_world4100)
    endif()
endif()

# ONNX Runtime
target_link_libraries(${PROJECT_NAME} PRIVATE
    debug ${ONNXRUNTIME_LIB_DIR}/onnxruntime.lib
    optimized ${ONNXRUNTIME_LIB_DIR}/onnxruntime.lib
)

# =============================================================================
# Compiler Flags (MSVC)
# =============================================================================
if(MSVC)
    # Release optimizations
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3           # Warning level 3
        /MP           # Multi-processor compilation
        /Zc:__cplusplus  # Correct __cplusplus value
        /permissive-  # Strict conformance
    )
    
    # Release-specific flags
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(${PROJECT_NAME} PRIVATE
            /O2         # Maximum speed optimization
            /Oi         # Enable intrinsic functions
            /GL         # Whole program optimization
            /DNDEBUG    # Disable debug assertions
            /D_CONSOLE  # Console app
        )
        target_link_options(${PROJECT_NAME} PRIVATE
            /LTCG       # Link-time code generation
            /OPT:REF    # Eliminate unreferenced functions/data
            /OPT:ICF    # Enable COMDAT folding
        )
    endif()
    
    # Disable min/max macros from windows.h (conflicts with std::min/max)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# =============================================================================
# Post-build: Copy DLLs
# =============================================================================
# Copy IbInputSimulator.dll
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/IbInputSimulator.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/IbInputSimulator.dll"
    COMMENT "Copying IbInputSimulator.dll"
)

# Copy ONNX Runtime DLLs
file(GLOB ONNXRUNTIME_DLLS "${ONNXRUNTIME_DIR}/lib/*.dll")
foreach(DLL_FILE ${ONNXRUNTIME_DLLS})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DLL_FILE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying ONNX Runtime DLLs"
    )
endforeach()

# Copy OpenCV DLLs
if(OpenCV_FOUND)
    # Use OpenCV runtime DLLs from the found package
    file(GLOB OPENCV_DLLS "${OpenCV_DIR}/../x64/vc*/bin/*.dll")
    foreach(DLL_FILE ${OPENCV_DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL_FILE}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
            COMMENT "Copying OpenCV DLLs"
        )
    endforeach()
else()
    file(GLOB OPENCV_DLLS "${DEPS_DIR}/opencv/build/x64/vc*/bin/*.dll")
    foreach(DLL_FILE ${OPENCV_DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL_FILE}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
            COMMENT "Copying OpenCV DLLs"
        )
    endforeach()
endif()

# =============================================================================
# Install
# =============================================================================
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

message(STATUS "")
message(STATUS "=== Build Configuration ===")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "OpenCV: ${OpenCV_FOUND} ${OpenCV_VERSION}")
message(STATUS "ONNX Runtime: ${ONNXRUNTIME_DIR}")
message(STATUS "============================")
message(STATUS "")