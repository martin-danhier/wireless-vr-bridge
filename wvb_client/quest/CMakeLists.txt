cmake_minimum_required(VERSION 3.16)
project(xr_client VERSION 0.1.0)

# Define C and C++ versions
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

# Enable find_path
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

set(CMAKE_ANDROID_ARM_MODE arm)
set(CMAKE_ANDROID_ARM_NEON true)

add_compile_definitions(
    #TEST_MODE
)

##################################################################
###                          SOURCES                           ###
##################################################################

# Source files
set(wvb_client_sources
        "src/main.cpp"
        )

##################################################################
###                      CREATE EXECUTABLE                     ###
##################################################################

add_library(wvb_xr_client SHARED ${wvb_client_sources})
target_sources(wvb_xr_client PRIVATE ${wvb_client_sources})

##################################################################
###                       DEPENDENCIES                         ###
##################################################################

# Include root CMake project
get_filename_component(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../ ABSOLUTE)
message("Root dir: ${ROOT_DIR}")
add_subdirectory(${ROOT_DIR} ${CMAKE_CURRENT_BINARY_DIR}/root)

# Include Quest SDK
set(QUEST_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/ovr_openxr_mobile_sdk)

# --- android_native_app_glue ---
add_library(android_native_app_glue STATIC)
target_include_directories(android_native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)
target_sources(android_native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_link_libraries(android_native_app_glue android)

# --- OpenXR loader ---
#add_library(openxr_loader INTERFACE)
#target_link_directories(openxr_loader INTERFACE ${QUEST_SDK_DIR}/OpenXR/Libs/Android/${ANDROID_ABI}/$<IF:$<CONFIG:Debug>,Debug,Release>)
#target_link_libraries(openxr_loader INTERFACE openxr_loader)
#target_include_directories(openxr_loader INTERFACE ${QUEST_SDK_DIR}/OpenXR/Include)

add_library(openxr_loader SHARED IMPORTED)
if ($OVR_DEBUG EQUAL 1)
    set(BUILD_TYPE Debug)
else()
    set(BUILD_TYPE Release)
endif()
set_target_properties(openxr_loader PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${QUEST_SDK_DIR}/OpenXR/Include
        IMPORTED_LOCATION ${QUEST_SDK_DIR}/OpenXR/Libs/Android/${ANDROID_ABI}/${BUILD_TYPE}/libopenxr_loader.so
        )


add_library(openxr INTERFACE)
target_include_directories(openxr INTERFACE
        ${QUEST_SDK_DIR}/3rdParty/khronos/openxr/OpenXR-SDK/include
        ${QUEST_SDK_DIR}/3rdParty/khronos/openxr/OpenXR-SDK/src/common
        )

# --- Minizip ---
add_library(minizip_lib INTERFACE)
target_include_directories(minizip_lib INTERFACE ${QUEST_SDK_DIR}/3rdParty/minizip/src)
target_link_directories(minizip_lib INTERFACE ${QUEST_SDK_DIR}/3rdParty/minizip/lib/android/${ANDROID_ABI})
target_link_libraries(minizip_lib INTERFACE minizip z)

# --- 1stParty ---
add_library(firstparty INTERFACE)
target_include_directories(firstparty INTERFACE
        ${QUEST_SDK_DIR}/1stParty/OVR/Include
        ${QUEST_SDK_DIR}/1stParty/utilities/include
        )

# --- ktx ---
add_library(ktx SHARED IMPORTED)
set_target_properties(ktx PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${QUEST_SDK_DIR}/3rdParty/khronos/ktx/include
        IMPORTED_LOCATION ${QUEST_SDK_DIR}/3rdParty/khronos/ktx/lib/android/${ANDROID_ABI}/libktx.so
        )

# --- samplecommon ---
add_library(samplecommon STATIC)
target_sources(samplecommon PRIVATE
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/ActionComponents.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/AnimComponents.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/CollisionPrimitive.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/DefaultComponent.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/Fader.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/GazeCursor.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/GuiSys.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/MetaDataManager.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/Reflection.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/ReflectionData.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/SoundLimiter.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/VRMenu.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/VRMenuComponent.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/VRMenuEvent.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/VRMenuEventHandler.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/VRMenuMgr.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/GUI/VRMenuObject.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Input/ArmModel.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Input/AxisRenderer.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Input/ControllerRenderer.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Input/Skeleton.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Input/SkeletonRenderer.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Input/TinyUI.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Locale/OVR_Locale.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Locale/tinyxml2.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Misc/Log.c
        ${QUEST_SDK_DIR}/SampleCommon/Src/Model/ModelAnimationUtils.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Model/ModelCollision.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Model/ModelFile_glTF.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Model/ModelFile_OvrScene.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Model/ModelFile.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Model/ModelRender.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Model/ModelTrace.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Model/SceneView.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/OVR_BinaryFile2.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/OVR_FileSys.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/OVR_Lexer2.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/OVR_MappedFile.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/OVR_Stream.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/OVR_Uri.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/OVR_UTF8Util.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/PackageFiles.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/BeamRenderer.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/BitmapFont.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/DebugLines.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/EaseFunctions.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/Egl.c
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/GeometryBuilder.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/GeometryRenderer.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/GlBuffer.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/GlGeometry.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/GlProgram.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/GlTexture.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/PanelRenderer.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/ParticleSystem.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/PointList.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/Ribbon.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/SurfaceRender.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/SurfaceTexture.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/TextureAtlas.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/Render/TextureManager.cpp
        ${QUEST_SDK_DIR}/SampleCommon/Src/System.cpp
        )
target_include_directories(samplecommon PUBLIC
        ${QUEST_SDK_DIR}/SampleCommon/Src
        )
target_link_libraries(samplecommon
        firstparty
        stb_image
        stb_image_write
        minizip_lib
        ktx
        )

# --- sample framework ---
add_library(samplexrframework STATIC)
target_sources(samplexrframework PRIVATE
        ${QUEST_SDK_DIR}/SampleXrFramework/Src/XrApp.cpp
        ${QUEST_SDK_DIR}/SampleXrFramework/Src/Input/HandMaskRenderer.cpp
        ${QUEST_SDK_DIR}/SampleXrFramework/Src/Input/HandRenderer.cpp
        ${QUEST_SDK_DIR}/SampleXrFramework/Src/Render/Framebuffer.cpp
        )
target_include_directories(samplexrframework PUBLIC ${QUEST_SDK_DIR}/SampleXrFramework/Src)
target_link_libraries(samplexrframework
        samplecommon
        openxr_loader
        android_native_app_glue
        openxr)

##################################################################
###                         LINK LIBS                          ###
##################################################################

target_link_libraries(wvb_client_lib
        samplecommon
        samplexrframework
        ffmpeg_lib
        log
        GLESv3
        EGL
        android_native_app_glue
        plugins
        )


target_link_libraries(wvb_xr_client
        wvb_client_lib
        )

##################################################################
###                              TESTS                         ###
##################################################################

