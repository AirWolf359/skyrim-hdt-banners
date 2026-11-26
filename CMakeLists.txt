cmake_minimum_required(VERSION 3.22)

# Version tracks jpsteel's upstream release in the first three parts; the fourth counts releases of this fork.
project(BannerPhysics VERSION 1.3.0.0 LANGUAGES C CXX)

if(NOT DEFINED OUTPUT_FOLDER)
    if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
        set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data")
    endif()

    if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
        set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
    endif()
endif()

set(COMMONLIBSSE_NG_ROOT "" CACHE PATH "Path to CommonLibSSE-NG")
set(COMMONLIB_LIBRARY "" CACHE FILEPATH "Optional matching prebuilt CommonLibSSE library")
set(MINHOOK_ROOT "" CACHE PATH "Path to MinHook")

if(NOT COMMONLIBSSE_NG_ROOT AND DEFINED ENV{COMMONLIBSSE_NG_ROOT})
    set(COMMONLIBSSE_NG_ROOT "$ENV{COMMONLIBSSE_NG_ROOT}")
endif()

if(NOT MINHOOK_ROOT AND DEFINED ENV{MINHOOK_ROOT})
    set(MINHOOK_ROOT "$ENV{MINHOOK_ROOT}")
endif()

# Default to the pinned submodules (git submodule update --init --recursive).
if(NOT COMMONLIBSSE_NG_ROOT)
    set(COMMONLIBSSE_NG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibVR")
endif()

if(NOT MINHOOK_ROOT)
    set(MINHOOK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook")
endif()

if(NOT EXISTS "${COMMONLIBSSE_NG_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "Set COMMONLIBSSE_NG_ROOT to your CommonLibSSE-NG directory")
endif()

if(NOT EXISTS "${MINHOOK_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "Set MINHOOK_ROOT to your MinHook directory")
endif()

find_package(Bullet CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
find_path(XBYAK_INCLUDE_DIR xbyak/xbyak.h REQUIRED)

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COMMONLIB_ENABLE_IPO OFF CACHE BOOL "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(FETCHCONTENT_SOURCE_DIR_HDE64 "${MINHOOK_ROOT}" CACHE PATH "MinHook instruction decoder")

# Record source paths relative to the repo (/pathmap, which MSVC only honors with /experimental:deterministic)
# and only the PDB's file name (/PDBALTPATH below), so the DLL and PDB don't carry the build machine's local
# paths. Set before add_subdirectory so CommonLib and MinHook get it too.
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" BP_SOURCE_DIR_NATIVE)
add_compile_options(/experimental:deterministic "/pathmap:${BP_SOURCE_DIR_NATIVE}=.")

if(COMMONLIB_LIBRARY)
    add_library(CommonLibSSE STATIC IMPORTED)
    set_target_properties(CommonLibSSE PROPERTIES IMPORTED_LOCATION "${COMMONLIB_LIBRARY}")
else()
    add_subdirectory(
        "${COMMONLIBSSE_NG_ROOT}"
        "${CMAKE_CURRENT_BINARY_DIR}/commonlib"
    )
endif()

add_subdirectory("${MINHOOK_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/minhook")

set(SOURCES
    src/main.cpp
    src/Utility.cpp
    src/FSMPBridge.cpp
    src/BannerRuntime.cpp
    src/BannerCollision.cpp
    src/BannerForces.cpp
    src/BannerDegradation.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        src
        fsmp-abi
        "${COMMONLIBSSE_NG_ROOT}/include"
        "${COMMONLIBSSE_NG_ROOT}/extern/openvr/headers"
        "${BULLET_INCLUDE_DIRS}"
        "${XBYAK_INCLUDE_DIR}"
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        BT_USE_SSE_IN_API
        NOMINMAX
        WINVER=0x0601
        _WIN32_WINNT=0x0601
        ENABLE_SKYRIM_SE=1
        ENABLE_SKYRIM_AE=1
        ENABLE_SKYRIM_VR=1
        HAS_SKYRIM_MULTI_TARGETING=1
        SKSE_SUPPORT_XBYAK=1
        SKSE_SUPPORT_PATCH_SAFETY=1
        _SILENCE_AMP_DEPRECATION_WARNINGS
        BP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        BP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        BP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        BP_VERSION_TWEAK=${PROJECT_VERSION_TWEAK}
)

target_compile_options(${PROJECT_NAME} PRIVATE /Zc:preprocessor /permissive- /utf-8 /Z7 /W4 /wd4201)
target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF /MAP /INCREMENTAL:NO "/PDBALTPATH:%_PDB%"
                    /EXPERIMENTAL:DETERMINISTIC "/PATHMAP:${BP_SOURCE_DIR_NATIVE}=.")

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        CommonLibSSE
        ${BULLET_LIBRARIES}
        TBB::tbb
        spdlog::spdlog
        Microsoft::DirectXTK
        minhook
        d3d11
        d3dcompiler
        bcrypt
        version
        dbghelp
        dxgi
)

file(GLOB_RECURSE LOCAL_HEADERS CONFIGURE_DEPENDS src/*.h fsmp-abi/*.h)
set_source_files_properties(${SOURCES} PROPERTIES OBJECT_DEPENDS "${LOCAL_HEADERS}")

if(OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
            VERBATIM
        )
    endif()
endif()
