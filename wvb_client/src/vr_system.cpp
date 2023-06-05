#include "wvb_client/vr_system.h"

#include "wvb_common/module.h"
#include <wvb_common/benchmark.h>
#include <wvb_common/global.h>
#include <wvb_common/rtp.h>
#include <wvb_common/video_encoder.h>

#include <deque>

#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_USE_PLATFORM_ANDROID       1
#define XR_USE_TIMESPEC               1
#define DESIRED_REFRESH_RATE          90.f

// Frame drop catch-up: when a frame is dropped (decoder doesn't output a frame and the previous one is re-used),
// try to mitigate the accumulated delay by pulling 2 frames the next time.
#define ENABLE_FRAME_DROP_CATCHUP 1
// Large queue catch-up: when the queue size exceeds a certain threshold, pull multiple frames.
// Queue size is checked after a first pull, so 0=subframe, 1 = 1 frame delay, etc
#define ENABLE_LARGE_QUEUE_CATCHUP 1
#define LARGE_QUEUE_CATCHUP_THRESHOLD 0

#include <android/native_window.h>
#include <EGL/egl.h>
#include <fstream>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <stb_image_write.h>
#include <string>
#include <utility>
#include <vector>
// Must be included after EGL
#include <GLES2/gl2ext.h>
#include <openxr/openxr_platform.h>

namespace wvb::client
{
    // =======================================================================================
    // =                                       Structs                                       =
    // =======================================================================================

#define FORM_FACTOR             XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY
#define VIEW_CONFIGURATION_TYPE XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO
    constexpr XrPosef XR_POSE_IDENTITY = {{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}};
#define ADDITIONAL_LATENCY_US     std::chrono::microseconds(4000)
#define TRACKING_STATE_CACHE_SIZE 100
#define CPU_PERFORMANCE_MODE      XR_PERF_SETTINGS_LEVEL_BOOST_EXT
#define GPU_PERFORMANCE_MODE      XR_PERF_SETTINGS_LEVEL_BOOST_EXT

#ifdef USE_OPENXR_VALIDATION_LAYERS
    PFN_xrCreateDebugUtilsMessengerEXT  xrCreateDebugUtilsMessengerEXT  = nullptr;
    PFN_xrDestroyDebugUtilsMessengerEXT xrDestroyDebugUtilsMessengerEXT = nullptr;
#endif
    PFN_xrConvertTimeToTimespecTimeKHR       xrConvertTimeToTimespecTimeKHR       = nullptr;
    PFN_xrConvertTimespecTimeToTimeKHR       xrConvertTimespecTimeToTimeKHR       = nullptr;
    PFN_xrPerfSettingsSetPerformanceLevelEXT xrPerfSettingsSetPerformanceLevelEXT = nullptr;
    PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC  glFramebufferTextureMultiviewOVR     = nullptr;
    PFN_xrEnumerateDisplayRefreshRatesFB     xrEnumerateDisplayRefreshRatesFB     = nullptr;
    PFN_xrGetDisplayRefreshRateFB            xrGetDisplayRefreshRateFB            = nullptr;
    PFN_xrRequestDisplayRefreshRateFB        xrRequestDisplayRefreshRateFB        = nullptr;

    struct EGL
    {
        EGLint     version_major = 0;
        EGLint     version_minor = 0;
        EGLDisplay display       = EGL_NO_DISPLAY;
        EGLConfig  config        = nullptr;
        EGLContext context       = EGL_NO_CONTEXT;
        EGLSurface surface       = EGL_NO_SURFACE;
    };

    struct FrameInfo
    {
        uint32_t frame_id                       = 0;
        bool     end_of_stream                  = false;
        uint32_t pose_timestamp                 = 0;
        uint32_t push_timestamp                 = 0;
        uint32_t last_packet_received_timestamp = 0;
        size_t   frame_size                     = 0;
        bool     should_save_frame              = false;
    };

    struct GLFramebufferImage
    {
        GLuint framebuffer   = 0;
        GLuint color_texture = 0;
        // No depth buffer for now
    };

    struct GLFramebuffer
    {
        std::vector<GLFramebufferImage> images;
    };

    struct PoseCacheEntry
    {
        uint32_t pose_timestamp   = 0;
        uint32_t sample_timestamp = 0;
        XrTime   xr_time          = 0;
        XrPosef  poses[NB_EYES] {};
        XrFovf   fovs[NB_EYES] {};
    };

    struct VertexShaderSettings
    {
        bool flip_y        = true;
        bool split_texture = true;
    };

    struct VRSystem::Data
    {
        // Benchmark
        std::shared_ptr<ClientMeasurementBucket> measurements_bucket;

        // Decoding
        std::shared_ptr<IVideoDecoder> video_decoder = nullptr;
        std::deque<FrameInfo>          frame_info_queue;
        bool                           video_decoder_initialized = false;

        VRSystemSpecs specs {};

        // OpenXR
        XrInstance xr_instance = XR_NULL_HANDLE;
#ifdef USE_OPENXR_VALIDATION_LAYERS
        XrDebugUtilsMessengerEXT xr_debug_messenger = XR_NULL_HANDLE;
#endif
        XrSystemId              xr_system_id                         = XR_NULL_SYSTEM_ID;
        XrSession               xr_session                           = XR_NULL_HANDLE;
        XrSpace                 xr_world_space                       = XR_NULL_HANDLE;
        XrSpace                 xr_head_space                        = XR_NULL_HANDLE;
        XrViewConfigurationView xr_view_configuration_views[NB_EYES] = {
            {XR_TYPE_VIEW_CONFIGURATION_VIEW},
            {XR_TYPE_VIEW_CONFIGURATION_VIEW},
        };
        uint32_t                  latest_frame_pose_timestamp = 0;
        uint32_t                  frames_in_advance           = 3;
        rtp::RTPClock::time_point last_tracking_time;
        XrSwapchain               xr_swapchain = XR_NULL_HANDLE;

        // Pose cache: write poses to the index, this gives access to TRACKING_STATE_CACHE_SIZE past
        // poses. When looking for a pose, start from the most recent one until a pose with a matching
        // timestamp is found.
        PoseCacheEntry pose_cache[TRACKING_STATE_CACHE_SIZE] = {0};
        int32_t        pose_cache_index                      = 0;
        XrFrameState   xr_frame_state {XR_TYPE_FRAME_STATE};

        PoseCacheEntry debug_pose = {};

        // EGL
        android_app   *app               = nullptr;
        ANativeWindow *native_window     = nullptr;
        EGL            egl_context       = {};
        uint8_t        accumulated_delay = 0;

        // GLES
        GLFramebuffer swapchain_framebuffer;
        // Simple triangle with hardcoded vertices in the shader
        GLuint                        gl_shader_program_nv12 = 0;
        GLuint                        gl_shader_program_rgba = 0;
        GLuint                        gl_shader_program_bgra = 0;
        std::optional<GLFrameTexture> gl_last_frame_texture  = std::nullopt;
        std::optional<FrameInfo>      last_frame_info        = std::nullopt;

        bool                           session_running = false;
        bool                           app_running     = false;
        bool                           should_exit     = false;
        uint64_t                       frame_index     = 0;
        std::shared_ptr<rtp::RTPClock> rtp_clock;

        void                                                init_engine();
        void                                                init_egl();
        void                                                init_gles();
        bool                                                init_decoder();
        void                                                cleanup_engine();
        void                                                cleanup_egl();
        void                                                cleanup_gles();
        bool                                                new_frame(ClientFrameTimeMeasurements &frame_execution_time);
        void                                                render_frame(ClientFrameTimeMeasurements &frame_execution_time);
        void                                                handle_events();
        [[nodiscard]] std::optional<const PoseCacheEntry *> find_old_pose(uint32_t rtp_timestamp) const;
        [[nodiscard]] XrTime                                to_xr_time(uint32_t rtp_timestamp) const;
        [[nodiscard]] rtp::RTPClock::time_point             to_rtp_time_point(XrTime xr_time) const;
        [[nodiscard]] uint32_t                              to_rtp_timestamp(XrTime xr_time) const;
        [[nodiscard]] inline uint64_t                       ntp_epoch() const { return rtp_clock->ntp_epoch(); }
        void                                                begin_session();
        void                                                end_session();
        void                                                soft_shutdown();
        bool get_frame_from_decoder(std::optional<GLFrameTexture> &frame, std::optional<FrameInfo> &frame_info);
        bool draw_frame(const std::optional<GLFrameTexture> &frame,
                        GLuint                               framebuffer,
                        Extent2D                             extent   = {0, 0},
                        VertexShaderSettings                 settings = {true, true}) const;

        bool save_frame_if_needed(IOBuffer &image);
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    // --- Utils ---

    void check(bool result, const std::string &error_message)
    {
        if (!result)
        {
            if (error_message.empty())
            {
                LOGE("[Error] Aborting.");
            }
            else
            {
                LOGE("[Error] %s Aborting.", error_message.c_str());
            }

            // Completely halt the program
            // TODO maybe recoverable ?
            exit(1);
        }
    }

    std::string xr_result_to_string(XrResult result)
    {
        switch (result)
        {
            case XR_SUCCESS: return "XR_SUCCESS";
            case XR_ERROR_API_VERSION_UNSUPPORTED: return "XR_ERROR_API_VERSION_UNSUPPORTED";
            case XR_ERROR_FUNCTION_UNSUPPORTED: return "XR_ERROR_FUNCTION_UNSUPPORTED";
            case XR_ERROR_GRAPHICS_DEVICE_INVALID: return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
            case XR_ERROR_SESSION_NOT_RUNNING: return "XR_ERROR_SESSION_NOT_RUNNING";
            case XR_ERROR_HANDLE_INVALID: return "XR_ERROR_HANDLE_INVALID";
            case XR_ERROR_RUNTIME_UNAVAILABLE: return "XR_ERROR_RUNTIME_UNAVAILABLE";
            case XR_ERROR_SESSION_NOT_READY: return "XR_ERROR_SESSION_NOT_READY";
            case XR_ERROR_TIME_INVALID: return "XR_ERROR_TIME_INVALID";
            case XR_ERROR_SESSION_NOT_STOPPING: return "XR_ERROR_SESSION_NOT_STOPPING";
            default: return std::to_string(result);
        }
    }

    bool xr_check(XrResult result, const std::string &error_message = "")
    {
        bool is_error         = false;
        bool is_unrecoverable = false;

        // Warnings
        // Errors
        if (result == XR_ERROR_RUNTIME_UNAVAILABLE)
        {
            LOGE("[OpenXR Error] No OpenXR runtime found. Please install an OpenXR runtime (e.g. SteamVR).");
            is_error         = true;
            is_unrecoverable = true;
        }
        else if (result != XR_SUCCESS)
        {
            // Pretty print error
            LOGE("[OpenXR Error] An OpenXR function call returned XrResult = %s", xr_result_to_string(result).c_str());
            is_error = true;
        }

        // Optional custom error message precision
        if (is_error && !error_message.empty())
        {
            LOGE("Precision: %s", error_message.c_str());
        }

        // If unrecoverable, abort
        if (is_unrecoverable)
        {
            throw std::runtime_error("Unrecoverable OpenXR error");
        }

        return !is_error;
    }

    void egl_check(EGLBoolean result, const std::string &error_message = "")
    {
        if (result == EGL_FALSE)
        {
            // Get error
            EGLint error = eglGetError();
            if (error == EGL_SUCCESS)
            {
                // No error
                return;
            }
            else
            {
                // Print error
                LOGE("[EGL Error] An EGL function call returned EGLBoolean = EGL_FALSE");
                LOGE("EGL Error: %d", error);
            }

            // Pretty print error
            LOGE("[EGL Error] An EGL function call returned EGLBoolean = EGL_FALSE");

            // Optional custom error message precision
            if (!error_message.empty())
            {
                LOGE("Precision: %s", error_message.c_str());
            }
        }
    }

    void gl_check(GLenum result, const std::string &error_message = "")
    {
        if (result != GL_NO_ERROR)
        {
            // Pretty print error
            LOGE("[GL Error] An GL function call returned GLenum = %d", result);

            // Optional custom error message precision
            if (!error_message.empty())
            {
                LOGE("Precision: %s", error_message.c_str());
            }
        }
    }

    void load_shader(const char *filename, struct android_app *app, std::vector<char> &out)
    {
        // Open file
        AAssetManager *asset_manager = app->activity->assetManager;
        AAsset        *asset         = AAssetManager_open(asset_manager, filename, AASSET_MODE_BUFFER);
        if (asset == nullptr)
        {
            LOGE("Could not open shader file %s", filename);
            return;
        }

        // Get file size
        off_t file_size = AAsset_getLength(asset);
        if (file_size == 0)
        {
            LOGE("Shader file %s is empty", filename);
            return;
        }

        // Read file
        out.resize(file_size);
        AAsset_read(asset, out.data(), file_size);
        AAsset_close(asset);

        out.push_back('\0');

        LOG("Loaded shader %s", filename);
    }

    // region Conversions

    std::string xr_reference_space_type_to_string(XrReferenceSpaceType space_type)
    {
        switch (space_type)
        {
            case XR_REFERENCE_SPACE_TYPE_STAGE: return "Stage";
            case XR_REFERENCE_SPACE_TYPE_LOCAL: return "Local";
            case XR_REFERENCE_SPACE_TYPE_VIEW: return "View";
            default: return std::to_string(space_type);
        }
    }

    XrTime VRSystem::Data::to_xr_time(uint32_t rtp_timestamp) const
    {
        timespec ts      = rtp_clock->to_timespec(rtp_timestamp);
        XrTime   xr_time = 0;
        xr_check(xrConvertTimespecTimeToTimeKHR(xr_instance, &ts, &xr_time));

        return xr_time;
    }

    rtp::RTPClock::time_point VRSystem::Data::to_rtp_time_point(XrTime xr_time) const
    {
        timespec ts = rtp_clock->to_timespec(to_rtp_timestamp(xr_time));
        return rtp_clock->from_timespec(ts);
    }

    uint32_t VRSystem::Data::to_rtp_timestamp(XrTime xr_time) const
    {
        timespec ts {};
        xr_check(xrConvertTimeToTimespecTimeKHR(xr_instance, xr_time, &ts));
        return rtp_clock->rtp_timestamp_from_timespec(ts);
    }

    constexpr Quaternion to_quat(const XrQuaternionf &quat)
    {
        return Quaternion {quat.x, quat.y, quat.z, quat.w};
    }

    constexpr Vector3<float> to_vec3(const XrVector3f &vec)
    {
        return {vec.x, vec.y, vec.z};
    }

    constexpr Pose to_pose(const XrPosef &xr_pose_left, const XrPosef &xr_pose_right)
    {
        // OpenVR uses only one pose for both eyes, while OpenXR tracks each eye independently.

        return Pose {
            .orientation = to_quat(xr_pose_left.orientation),
            // Take center point
            .position = (to_vec3(xr_pose_left.position) + to_vec3(xr_pose_right.position)) / 2.0f,
        };
    }

    constexpr Pose to_pose(const XrPosef &pose)
    {
        return Pose {
            .orientation = to_quat(pose.orientation),
            .position    = to_vec3(pose.position),
        };
    }

    constexpr Fov to_fov(const XrFovf &fov)
    {
        return Fov {
            .left  = fov.angleLeft,
            .right = fov.angleRight,
            .up    = fov.angleUp,
            .down  = fov.angleDown,
        };
    }

    // endregion

    // region Check support

    bool check_xr_instance_extension_support(const std::vector<const char *> &desired_extensions)
    {
        // Get the number of available desired_extensions
        uint32_t available_extensions_count = 0;
        xr_check(xrEnumerateInstanceExtensionProperties(nullptr, 0, &available_extensions_count, nullptr));
        // Create an array with enough room and fetch the available desired_extensions
        std::vector<XrExtensionProperties> available_extensions(available_extensions_count, {XR_TYPE_EXTENSION_PROPERTIES});
        xr_check(xrEnumerateInstanceExtensionProperties(nullptr,
                                                        available_extensions_count,
                                                        &available_extensions_count,
                                                        available_extensions.data()));

        // For each desired extension, rg_renderer_check if it is available
        bool valid = true;
        for (const auto &desired_extension : desired_extensions)
        {
            bool       found = false;
            const auto ext   = std::string(desired_extension);

            // Search available extensions until the desired one is found or not
            for (const auto &available_extension : available_extensions)
            {
                if (ext == std::string(available_extension.extensionName))
                {
                    found = true;
                    break;
                }
            }

            // Stop looking if nothing was found
            if (!found)
            {
                valid = false;
                LOGE("[Error] The extension \"%s\" is not available.", ext.c_str());
                break;
            }
        }

        return valid;
    }

    bool check_layer_support(const std::vector<const char *> &desired_layers)
    {
        // Get the number of available API layers
        uint32_t available_layers_count = 0;
        xr_check(xrEnumerateApiLayerProperties(0, &available_layers_count, nullptr));
        // Create an array with enough room and fetch the available layers
        std::vector<XrApiLayerProperties> available_layers(available_layers_count, {XR_TYPE_API_LAYER_PROPERTIES});
        xr_check(xrEnumerateApiLayerProperties(available_layers_count, &available_layers_count, available_layers.data()));

        // For each desired layer, rg_renderer_check if it is available
        bool valid = true;
        for (const auto &desired_layer : desired_layers)
        {
            bool       found = false;
            const auto layer = std::string(desired_layer);

            // Search available layers until the desired one is found or not
            for (const auto &available_layer : available_layers)
            {
                if (layer == std::string(available_layer.layerName))
                {
                    found = true;
                    break;
                }
            }

            // Stop looking if nothing was found
            if (!found)
            {
                valid = false;
                LOGE("[Error] The layer \"%s\" is not available.", layer.c_str());
                break;
            }
        }

        return valid;
    }

    XrReferenceSpaceType choose_reference_space_type(XrSession session)
    {
        // Define the priority of each space type. We will take the first one that is available.
        XrReferenceSpaceType space_type_preference[] = {
            XR_REFERENCE_SPACE_TYPE_STAGE, // Based on play area center (most common for room-scale VR)
            XR_REFERENCE_SPACE_TYPE_LOCAL, // Based on starting location
        };

        // Get available space types
        uint32_t available_spaces_count = 0;
        xr_check(xrEnumerateReferenceSpaces(session, 0, &available_spaces_count, nullptr));
        std::vector<XrReferenceSpaceType> available_spaces(available_spaces_count);
        xr_check(xrEnumerateReferenceSpaces(session, available_spaces_count, &available_spaces_count, available_spaces.data()));

        // Choose the first available space type
        for (const auto &space_type : space_type_preference)
        {
            for (const auto &available_space : available_spaces)
            {
                if (space_type == available_space)
                {
                    return space_type;
                }
            }
        }

        check(false, "No supported reference space type found.");
        throw;
    }

    EGLConfig choose_egl_config(EGLDisplay display)
    {
        // Get list of configs
        EGLint num_configs;
        egl_check(eglGetConfigs(display, nullptr, 0, &num_configs));
        std::vector<EGLConfig> configs(num_configs);
        egl_check(eglGetConfigs(display, configs.data(), num_configs, &num_configs));

        const EGLint config_attribs[] = {
            EGL_RED_SIZE,
            8,
            EGL_GREEN_SIZE,
            8,
            EGL_BLUE_SIZE,
            8,
            EGL_ALPHA_SIZE,
            8,
            EGL_DEPTH_SIZE,
            0,
            EGL_STENCIL_SIZE,
            0,
            EGL_SAMPLES,
            0,
            EGL_NONE,
        };
        for (auto config : configs)
        {
            EGLint value;
            egl_check(eglGetConfigAttrib(display, config, EGL_RENDERABLE_TYPE, &value));
            if ((value & (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) != (EGL_WINDOW_BIT | EGL_PBUFFER_BIT))
            {
                continue;
            }

            // Compare other requirements
            int i = 0;
            for (; config_attribs[i] != EGL_NONE; i += 2)
            {
                egl_check(eglGetConfigAttrib(display, config, config_attribs[i], &value));
                if (value != config_attribs[i + 1])
                {
                    break;
                }
            }
            if (config_attribs[i] == EGL_NONE)
            {
                // End reached, all requirements met
                return config;
            }
        }

        return nullptr;
    }

    float setup_refresh_rate(XrSession session, float target_refresh_rate)
    {
        // Get extension functions

        // Get available refresh rates
        uint32_t available_refresh_rates_count = 0;
        xr_check(xrEnumerateDisplayRefreshRatesFB(session, 0, &available_refresh_rates_count, nullptr));
        std::vector<float> available_refresh_rates(available_refresh_rates_count);
        xr_check(xrEnumerateDisplayRefreshRatesFB(session,
                                                  available_refresh_rates_count,
                                                  &available_refresh_rates_count,
                                                  available_refresh_rates.data()));

        // Choose the one that is closest to DESIRED_REFRESH_RATE
        float best_refresh_rate = available_refresh_rates[0];
        for (const auto &refresh_rate : available_refresh_rates)
        {
            if (std::abs(refresh_rate - target_refresh_rate) < std::abs(best_refresh_rate - target_refresh_rate))
            {
                best_refresh_rate = refresh_rate;
            }
        }

        // Request refresh rate
        xr_check(xrRequestDisplayRefreshRateFB(session, best_refresh_rate));

        return best_refresh_rate;
    }

    // endregion

    // region Debug utils

    /**
     * Callback for the vulkan debug messenger
     * @param message_severity Severity of the message
     * @param message_types Type of the message
     * @param callback_data Additional m_data concerning the message
     * @param user_data User m_data passed to the debug messenger
     */
    XrBool32 debug_messenger_callback(XrDebugUtilsMessageSeverityFlagsEXT         message_severity,
                                      XrDebugUtilsMessageTypeFlagsEXT             message_types,
                                      const XrDebugUtilsMessengerCallbackDataEXT *callback_data,
                                      void                                       *_)
    {
        // Inspired by VkBootstrap's default debug messenger. (Made by Charles Giessen)
        // Get severity
        const char *str_severity;
        switch (message_severity)
        {
            case XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: str_severity = "VERBOSE"; break;
            case XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: str_severity = "ERROR"; break;
            case XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: str_severity = "WARNING"; break;
            case XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: str_severity = "INFO"; break;
            default: str_severity = "UNKNOWN"; break;
        }

        // Get type
        const char *str_type;
        switch (message_types)
        {
            case 7: str_type = "General | Validation | Performance"; break;
            case 6: str_type = "Validation | Performance"; break;
            case 5: str_type = "General | Performance"; break;
            case 4: str_type = "Performance"; break;
            case 3: str_type = "General | Validation"; break;
            case 2: str_type = "Validation"; break;
            case 1: str_type = "General"; break;
            default: str_type = "Unknown"; break;
        }

        // Print the message to stderr if it is an error.
        if (message_severity == XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            LOGE("[OpenXR %s: %s]\n%s", str_severity, str_type, callback_data->message);
        }
        else
        {
            LOG("[OpenXR %s: %s]\n%s", str_severity, str_type, callback_data->message);
        }

        return XR_FALSE;
    }

    // endregion

    void VRSystem::Data::init_engine()
    {
        // Create OpenXR instance
        {
            LOG("Using OpenXR, version %d.%d.%d",
                XR_VERSION_MAJOR(XR_CURRENT_API_VERSION),
                XR_VERSION_MINOR(XR_CURRENT_API_VERSION),
                XR_VERSION_PATCH(XR_CURRENT_API_VERSION));

            XrApplicationInfo app_info {
                .applicationName    = WVB_APP_NAME,
                .applicationVersion = WVB_VERSION_32,
                .engineName         = WVB_ENGINE_NAME,
                .engineVersion      = WVB_VERSION_32,
                .apiVersion         = XR_CURRENT_API_VERSION,
            };

            // Create instance
            std::vector<const char *> required_extensions = {
                XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
                XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
                XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME,
                XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,

#ifdef USE_OPENXR_VALIDATION_LAYERS
                XR_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
            };

            check(check_xr_instance_extension_support(required_extensions), "Not all required OpenXR extensions are supported.");

#ifdef USE_OPENXR_VALIDATION_LAYERS
            std::vector<const char *> enabled_layers;
            enabled_layers.push_back("XR_APILAYER_LUNARG_core_validation");
            if (!check_layer_support(enabled_layers))
            {
                LOGE("[Warning] OpenXR validation layers requested, but not available.\n"
                     "          Try setting the \"XR_API_LAYER_PATH\" environment variable (see README).\n"
                     "          Continuing without validation layers...");
                enabled_layers.clear();
            }
#endif

            XrInstanceCreateInfoAndroidKHR android_info {
                .type                = XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR,
                .next                = XR_NULL_HANDLE,
                .applicationVM       = app->activity->vm,
                .applicationActivity = app->activity,
            };
            XrInstanceCreateInfo instance_create_info {
                .type            = XR_TYPE_INSTANCE_CREATE_INFO,
                .next            = &android_info,
                .applicationInfo = app_info,
            // Layers
#ifdef USE_OPENXR_VALIDATION_LAYERS
                .enabledApiLayerCount = static_cast<uint32_t>(enabled_layers.size()),
                .enabledApiLayerNames = enabled_layers.data(),
#else
                .enabledApiLayerCount = 0,
                .enabledApiLayerNames = XR_NULL_HANDLE,
#endif
                // Extensions
                .enabledExtensionCount = static_cast<uint32_t>(required_extensions.size()),
                .enabledExtensionNames = required_extensions.data(),
            };

            xr_check(xrCreateInstance(&instance_create_info, &xr_instance), "Failed to create instance");

            // Print runtime name
            XrInstanceProperties instance_properties {XR_TYPE_INSTANCE_PROPERTIES};
            xr_check(xrGetInstanceProperties(xr_instance, &instance_properties), "Failed to get instance properties");

            LOG("[OpenXR] Using runtime \"%s\", version %d.%d.%d",
                instance_properties.runtimeName,
                XR_VERSION_MAJOR(instance_properties.runtimeVersion),
                XR_VERSION_MINOR(instance_properties.runtimeVersion),
                XR_VERSION_PATCH(instance_properties.runtimeVersion));
        } // namespace wvb::client

        // === Load dynamic functions ===
        {
#ifdef USE_OPENXR_VALIDATION_LAYERS
            xr_check(xrGetInstanceProcAddr(xr_instance,
                                           "xrCreateDebugUtilsMessengerEXT",
                                           reinterpret_cast<PFN_xrVoidFunction *>(&xrCreateDebugUtilsMessengerEXT)),
                     "Failed to load xrCreateDebugUtilsMessengerEXT");
            xr_check(xrGetInstanceProcAddr(xr_instance,
                                           "xrDestroyDebugUtilsMessengerEXT",
                                           reinterpret_cast<PFN_xrVoidFunction *>(&xrDestroyDebugUtilsMessengerEXT)),
                     "Failed to load xrDestroyDebugUtilsMessengerEXT");
#endif
            xr_check(xrGetInstanceProcAddr(xr_instance,
                                           "xrConvertTimeToTimespecTimeKHR",
                                           reinterpret_cast<PFN_xrVoidFunction *>(&xrConvertTimeToTimespecTimeKHR)),
                     "Failed to load xrConvertTimeToTimespecTimeKHR");
            xr_check(xrGetInstanceProcAddr(xr_instance,
                                           "xrConvertTimespecTimeToTimeKHR",
                                           reinterpret_cast<PFN_xrVoidFunction *>(&xrConvertTimespecTimeToTimeKHR)),
                     "Failed to load xrConvertTimespecTimeToTimeKHR");
            xr_check(xrGetInstanceProcAddr(xr_instance,
                                           "xrPerfSettingsSetPerformanceLevelEXT",
                                           reinterpret_cast<PFN_xrVoidFunction *>(&xrPerfSettingsSetPerformanceLevelEXT)),
                     "Failed to load xrPerfSettingsSetPerformanceLevelEXT");
            xr_check(xrGetInstanceProcAddr(xr_instance,
                                           "xrEnumerateDisplayRefreshRatesFB",
                                           reinterpret_cast<PFN_xrVoidFunction *>(&xrEnumerateDisplayRefreshRatesFB)),
                     "Failed to load xrEnumerateDisplayRefreshRatesFB");
            xr_check(xrGetInstanceProcAddr(xr_instance,
                                           "xrGetDisplayRefreshRateFB",
                                           reinterpret_cast<PFN_xrVoidFunction *>(&xrGetDisplayRefreshRateFB)),
                     "Failed to load xrGetDisplayRefreshRateFB");
            xr_check(xrGetInstanceProcAddr(xr_instance,
                                           "xrRequestDisplayRefreshRateFB",
                                           reinterpret_cast<PFN_xrVoidFunction *>(&xrRequestDisplayRefreshRateFB)),
                     "Failed to load xrRequestDisplayRefreshRateFB");
        }

// === Create debug messenger ===
#ifdef USE_OPENXR_VALIDATION_LAYERS
        {
            XrDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = {
                // Struct info
                .type = XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                .next = XR_NULL_HANDLE,
                // Message settings
                .messageSeverities = XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
                .messageTypes      = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                                | XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                // Callback
                .userCallback = static_cast<PFN_xrDebugUtilsMessengerCallbackEXT>(debug_messenger_callback),
            };
            xr_check(xrCreateDebugUtilsMessengerEXT(xr_instance, &debug_messenger_create_info, &xr_debug_messenger),
                     "Failed to create debug messenger");
        }
#endif

        // === Get system ===
        {
            XrSystemGetInfo system_info {
                .type = XR_TYPE_SYSTEM_GET_INFO,
                .next = XR_NULL_HANDLE,
                // We only support headsets
                .formFactor = FORM_FACTOR,
            };
            xr_check(xrGetSystem(xr_instance, &system_info, &xr_system_id), "Failed to get system");

            // Print system info
            XrSystemProperties system_properties {
                .type = XR_TYPE_SYSTEM_PROPERTIES,
                .next = XR_NULL_HANDLE,
            };
            xr_check(xrGetSystemProperties(xr_instance, xr_system_id, &system_properties), "Failed to get system properties");
            LOG("System name: %s", system_properties.systemName);
            specs.system_name       = std::string(system_properties.systemName);
            specs.manufacturer_name = "Oculus";
        }

        // === Initialize EGL ===
        {
            PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetOpenGLESGraphicsRequirementsKHR;
            xr_check(xrGetInstanceProcAddr(xr_instance,
                                           "xrGetOpenGLESGraphicsRequirementsKHR",
                                           reinterpret_cast<PFN_xrVoidFunction *>(&xrGetOpenGLESGraphicsRequirementsKHR)));

            XrGraphicsRequirementsOpenGLESKHR graphics_requirements {
                .type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR,
                .next = XR_NULL_HANDLE,
            };
            xr_check(xrGetOpenGLESGraphicsRequirementsKHR(xr_instance, xr_system_id, &graphics_requirements));

            init_egl();

            // Check with requirements
            int major, minor;
            glGetIntegerv(GL_MAJOR_VERSION, &major);
            glGetIntegerv(GL_MINOR_VERSION, &minor);
            const XrVersion version = XR_MAKE_VERSION(major, minor, 0);
            check(version >= graphics_requirements.minApiVersionSupported && version <= graphics_requirements.maxApiVersionSupported,
                  "Unsupported OpenGL ES version");
        }

        // Get OVR extensions
        glFramebufferTextureMultiviewOVR =
            (PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC) eglGetProcAddress("glFramebufferTextureMultiviewOVR");
        check(glFramebufferTextureMultiviewOVR != nullptr, "Failed to load glFramebufferTextureMultiviewOVR");

        // Create graphics binding
        XrGraphicsBindingOpenGLESAndroidKHR graphics_binding {
            .type    = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
            .next    = XR_NULL_HANDLE,
            .display = egl_context.display,
            .config  = egl_context.config,
            .context = egl_context.context,
        };
        XrSessionCreateInfo session_create_info {
            .type     = XR_TYPE_SESSION_CREATE_INFO,
            .next     = &graphics_binding,
            .systemId = xr_system_id,
        };
        xr_check(xrCreateSession(xr_instance, &session_create_info, &xr_session), "Failed to create session");

        {
            // Get view configuration views
            uint32_t view_config_view_count = 0;
            xr_check(xrEnumerateViewConfigurationViews(xr_instance,
                                                       xr_system_id,
                                                       VIEW_CONFIGURATION_TYPE,
                                                       0,
                                                       &view_config_view_count,
                                                       nullptr));
            check(view_config_view_count == NB_EYES, "Expected 2 view configuration views");

            xr_check(xrEnumerateViewConfigurationViews(xr_instance,
                                                       xr_system_id,
                                                       VIEW_CONFIGURATION_TYPE,
                                                       2,
                                                       &view_config_view_count,
                                                       xr_view_configuration_views));

            // Ensure that both eyes have the same resolution
            check(xr_view_configuration_views[EYE_LEFT].recommendedImageRectWidth
                      == xr_view_configuration_views[EYE_RIGHT].recommendedImageRectWidth,
                  "Left and right eye should have the same width");
            check(xr_view_configuration_views[EYE_LEFT].recommendedImageRectHeight
                      == xr_view_configuration_views[EYE_RIGHT].recommendedImageRectHeight,
                  "Left and right eye should have the same height");
            specs.eye_resolution = {
                xr_view_configuration_views[EYE_LEFT].recommendedImageRectWidth,
                xr_view_configuration_views[EYE_LEFT].recommendedImageRectHeight,
            };

            // Create reference spaces

            // Check if stage space is supported
            auto world_space_type = choose_reference_space_type(xr_session);

            // Stage space
            XrReferenceSpaceCreateInfo reference_space_create_info {
                .type                 = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
                .next                 = XR_NULL_HANDLE,
                .referenceSpaceType   = world_space_type,
                .poseInReferenceSpace = XR_POSE_IDENTITY,
            };
            xr_check(xrCreateReferenceSpace(xr_session, &reference_space_create_info, &xr_world_space),
                     "Failed to create world reference space");

            // Head space
            reference_space_create_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
            xr_check(xrCreateReferenceSpace(xr_session, &reference_space_create_info, &xr_head_space),
                     "Failed to create head reference space");

            // Get world bounds
            XrExtent2Df world_bounds;
            xr_check(xrGetReferenceSpaceBoundsRect(xr_session, world_space_type, &world_bounds));
            specs.world_bounds = {
                world_bounds.width,
                world_bounds.height,
            };
        }

        // === Create swapchain ===
        {
            GLenum                format = GL_SRGB8_ALPHA8;
            XrSwapchainCreateInfo swapchain_create_info {
                .type        = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                .next        = XR_NULL_HANDLE,
                .createFlags = 0,
                .usageFlags  = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                .format      = format,
                .sampleCount = 1,
                .width       = specs.eye_resolution.width,
                .height      = specs.eye_resolution.height,
                .faceCount   = 1,
                .arraySize   = NB_EYES,
                .mipCount    = 1,
            };
            xr_check(xrCreateSwapchain(xr_session, &swapchain_create_info, &xr_swapchain), "Failed to create swapchain");

            // Get swapchain images
            uint32_t xr_swapchain_images_count = 0;
            xr_check(xrEnumerateSwapchainImages(xr_swapchain, 0, &xr_swapchain_images_count, nullptr));
            std::vector<XrSwapchainImageOpenGLESKHR> swapchain_images(xr_swapchain_images_count,
                                                                      {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
            xr_check(xrEnumerateSwapchainImages(xr_swapchain,
                                                xr_swapchain_images_count,
                                                &xr_swapchain_images_count,
                                                reinterpret_cast<XrSwapchainImageBaseHeader *>(swapchain_images.data())));

            swapchain_framebuffer.images.resize(xr_swapchain_images_count);

            for (uint32_t i = 0; i < xr_swapchain_images_count; ++i)
            {
                swapchain_framebuffer.images[i].color_texture = swapchain_images[i].image;

                glGenFramebuffers(1, &swapchain_framebuffer.images[i].framebuffer);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, swapchain_framebuffer.images[i].framebuffer);
                glFramebufferTextureMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, swapchain_images[i].image, 0, 0, NB_EYES);
                check(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Failed to create framebuffer");
            }
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        }

        // Get additional specs
        {
            float refresh_rate = setup_refresh_rate(xr_session, DESIRED_REFRESH_RATE);
            LOG("Refresh rate: %f Hz", refresh_rate);
            specs.refresh_rate.numerator   = static_cast<uint32_t>(refresh_rate);
            specs.refresh_rate.denominator = 1; // Refresh rate is always a whole number despite being a float
        }

        // Setup OpenGL
        init_gles();

        {
            // Get views
            XrViewLocateInfo view_locate_info {
                .type                  = XR_TYPE_VIEW_LOCATE_INFO,
                .next                  = XR_NULL_HANDLE,
                .viewConfigurationType = VIEW_CONFIGURATION_TYPE,
                .displayTime           = to_xr_time(rtp_clock->now_rtp_timestamp()),
                .space                 = xr_head_space,
            };
            XrView      views[NB_EYES];
            uint32_t    view_count = NB_EYES;
            XrViewState xr_view_state {
                .type = XR_TYPE_VIEW_STATE,
            };
            xr_check(xrLocateViews(xr_session, &view_locate_info, &xr_view_state, view_count, &view_count, views));

            // Compute IPD
            specs.ipd = views[1].pose.position.x - views[0].pose.position.x;
        }
    }

    void VRSystem::Data::init_egl()
    {
        if (egl_context.display != EGL_NO_DISPLAY)
        {
            return;
        }
        if (native_window == nullptr)
        {
            throw std::runtime_error("Native window is not set");
        }

        egl_context.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        egl_check(eglInitialize(egl_context.display, &egl_context.version_major, &egl_context.version_minor));

        egl_context.config = choose_egl_config(egl_context.display);
        check(egl_context.config != nullptr, "Failed to choose EGL config");

        // Create context
        const EGLint context_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION,
            3,
            EGL_NONE,
        };
        egl_context.context = eglCreateContext(egl_context.display, egl_context.config, EGL_NO_CONTEXT, context_attribs);
        egl_check(egl_context.context != EGL_NO_CONTEXT, "Failed to create EGL context");

        // Get surface from native window
        egl_context.surface = eglCreateWindowSurface(egl_context.display, egl_context.config, native_window, nullptr);
        egl_check(egl_context.surface != EGL_NO_SURFACE, "Failed to create EGL surface");

        // Make current
        egl_check(eglMakeCurrent(egl_context.display, egl_context.surface, egl_context.surface, egl_context.context));
    }

    bool VRSystem::Data::init_decoder()
    {
        if (video_decoder != nullptr && !video_decoder_initialized)
        {
            video_decoder->init();
            video_decoder_initialized = true;
        }
        return video_decoder_initialized;
    }

    void VRSystem::Data::cleanup_engine()
    {
        // Wait for rendering to finish
        glFinish();

        // End session
        if (xr_session != XR_NULL_HANDLE)
        {
            end_session();
        }

        if (xr_swapchain != XR_NULL_HANDLE)
        {
            xr_check(xrDestroySwapchain(xr_swapchain));
        }

        // Cleanup OpenGL
        cleanup_gles();

        if (xr_session != XR_NULL_HANDLE)
        {
            xr_check(xrDestroySession(xr_session));
        }

        cleanup_egl();

#ifdef USE_OPENXR_VALIDATION_LAYERS
        if (xr_debug_messenger != XR_NULL_HANDLE)
        {
            xr_check(xrDestroyDebugUtilsMessengerEXT(xr_debug_messenger));
        }
#endif

        if (xr_instance != XR_NULL_HANDLE)
        {
            xr_check(xrDestroyInstance(xr_instance));
        }
    }

    void VRSystem::Data::cleanup_egl()
    {
        if (egl_context.display != EGL_NO_DISPLAY)
        {
            egl_check(eglMakeCurrent(egl_context.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
        }

        if (egl_context.context != EGL_NO_CONTEXT)
        {
            egl_check(eglDestroyContext(egl_context.display, egl_context.context));
            egl_context.context = EGL_NO_CONTEXT;
        }

        if (egl_context.display != EGL_NO_DISPLAY)
        {
            egl_check(eglTerminate(egl_context.display));
            egl_context.display = EGL_NO_DISPLAY;
        }
    }

    void VRSystem::Data::init_gles()
    {
        // Find root directory of assets

        // Load shaders
        std::vector<char> vertex_shader_source;
        load_shader("fullscreen.vert", app, vertex_shader_source);

        std::vector<char> nv12_fragment_shader_source;
        load_shader("texture_nv12.frag", app, nv12_fragment_shader_source);

        std::vector<char> rgba_fragment_shader_source;
        load_shader("texture_rgba.frag", app, rgba_fragment_shader_source);

        std::vector<char> bgra_fragment_shader_source;
        load_shader("texture_bgra.frag", app, bgra_fragment_shader_source);

        GLuint      vertex_shader             = glCreateShader(GL_VERTEX_SHADER);
        const char *vertex_shader_source_cstr = vertex_shader_source.data();
        glShaderSource(vertex_shader, 1, &vertex_shader_source_cstr, nullptr);
        glCompileShader(vertex_shader);
        // Check if success
        GLint success;
        glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLchar info_log[512];
            glGetShaderInfoLog(vertex_shader, 512, nullptr, info_log);
            LOGE("Failed to compile vertex shader: %s", info_log);
        }

        GLuint      nv12_fragment_shader             = glCreateShader(GL_FRAGMENT_SHADER);
        const char *nv12_fragment_shader_source_cstr = nv12_fragment_shader_source.data();
        glShaderSource(nv12_fragment_shader, 1, &nv12_fragment_shader_source_cstr, nullptr);
        glCompileShader(nv12_fragment_shader);
        // Check if success
        glGetShaderiv(nv12_fragment_shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLchar info_log[512];
            glGetShaderInfoLog(nv12_fragment_shader, 512, nullptr, info_log);
            LOGE("Failed to compile NV12 fragment shader: %s", info_log);
        }

        GLuint      rgba_fragment_shader             = glCreateShader(GL_FRAGMENT_SHADER);
        const char *rgba_fragment_shader_source_cstr = rgba_fragment_shader_source.data();
        glShaderSource(rgba_fragment_shader, 1, &rgba_fragment_shader_source_cstr, nullptr);
        glCompileShader(rgba_fragment_shader);
        // Check if success
        glGetShaderiv(rgba_fragment_shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLchar info_log[512];
            glGetShaderInfoLog(rgba_fragment_shader, 512, nullptr, info_log);
            LOGE("Failed to compile RGBA fragment shader: %s", info_log);
        }

        GLuint      bgra_fragment_shader             = glCreateShader(GL_FRAGMENT_SHADER);
        const char *bgra_fragment_shader_source_cstr = bgra_fragment_shader_source.data();
        glShaderSource(bgra_fragment_shader, 1, &bgra_fragment_shader_source_cstr, nullptr);
        glCompileShader(bgra_fragment_shader);
        // Check if success
        glGetShaderiv(bgra_fragment_shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLchar info_log[512];
            glGetShaderInfoLog(bgra_fragment_shader, 512, nullptr, info_log);
            LOGE("Failed to compile BGRA fragment shader: %s", info_log);
        }

        // Create programs
        gl_shader_program_nv12 = glCreateProgram();
        glAttachShader(gl_shader_program_nv12, vertex_shader);
        glAttachShader(gl_shader_program_nv12, nv12_fragment_shader);
        glLinkProgram(gl_shader_program_nv12);
        // Check if success
        glGetProgramiv(gl_shader_program_nv12, GL_LINK_STATUS, &success);
        if (!success)
        {
            GLchar info_log[512];
            glGetProgramInfoLog(gl_shader_program_nv12, 512, nullptr, info_log);
            LOGE("Failed to link NV12 shader program: %s", info_log);
        }

        gl_shader_program_rgba = glCreateProgram();
        glAttachShader(gl_shader_program_rgba, vertex_shader);
        glAttachShader(gl_shader_program_rgba, rgba_fragment_shader);
        glLinkProgram(gl_shader_program_rgba);
        // Check if success
        glGetProgramiv(gl_shader_program_rgba, GL_LINK_STATUS, &success);
        if (!success)
        {
            GLchar info_log[512];
            glGetProgramInfoLog(gl_shader_program_rgba, 512, nullptr, info_log);
            LOGE("Failed to link RGBA shader program: %s", info_log);
        }

        gl_shader_program_bgra = glCreateProgram();
        glAttachShader(gl_shader_program_bgra, vertex_shader);
        glAttachShader(gl_shader_program_bgra, bgra_fragment_shader);
        glLinkProgram(gl_shader_program_bgra);
        // Check if success
        glGetProgramiv(gl_shader_program_bgra, GL_LINK_STATUS, &success);
        if (!success)
        {
            GLchar info_log[512];
            glGetProgramInfoLog(gl_shader_program_bgra, 512, nullptr, info_log);
            LOGE("Failed to link BGRA shader program: %s", info_log);
        }

        glDeleteShader(vertex_shader);
        glDeleteShader(nv12_fragment_shader);
        glDeleteShader(rgba_fragment_shader);
        glDeleteShader(bgra_fragment_shader);
    }

    void VRSystem::Data::render_frame(ClientFrameTimeMeasurements &frame_execution_time)
    {
        XrTime display_time = xr_frame_state.predictedDisplayTime;

        std::optional<GLFrameTexture> frame                  = std::nullopt;
        std::optional<FrameInfo>      frame_info             = std::nullopt;
        bool                          something_was_rendered = false;

        // Begin frame
        frame_execution_time.begin_frame_timestamp = rtp_clock->now_rtp_timestamp();
        XrFrameBeginInfo frame_begin_info {XR_TYPE_FRAME_BEGIN_INFO};
        xr_check(xrBeginFrame(xr_session, &frame_begin_info));

        std::vector<XrCompositionLayerProjection> layers;
        XrView                                    views[NB_EYES];
        XrCompositionLayerProjectionView          projection_layer_views[2];

        if (xr_frame_state.shouldRender)
        {
            // Get next image
            XrSwapchainImageAcquireInfo acquire_info {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            uint32_t                    image_index;
            xr_check(xrAcquireSwapchainImage(xr_swapchain, &acquire_info, &image_index));
            XrSwapchainImageWaitInfo swapchain_wait_info {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            swapchain_wait_info.timeout = XR_INFINITE_DURATION;
            xr_check(xrWaitSwapchainImage(xr_swapchain, &swapchain_wait_info));

            frame_execution_time.after_wait_swapchain_timestamp = rtp_clock->now_rtp_timestamp();

            // Update frame texture
            get_frame_from_decoder(frame, frame_info);

            // Catch-up
            while (
                    // Last frame was dropped
                    (ENABLE_FRAME_DROP_CATCHUP && accumulated_delay > 0) ||
                    // There are too many frames in queue
                    (ENABLE_LARGE_QUEUE_CATCHUP && frame_info_queue.size() > LARGE_QUEUE_CATCHUP_THRESHOLD))
            {
                // Try to pull a frame again to avoid accumulating delay.
                bool has_frame = get_frame_from_decoder(frame, frame_info);
                if (has_frame)
                {
                    if (accumulated_delay > 0)
                    {
                        accumulated_delay--;
                    }
                    measurements_bucket->add_catched_up_frame();
                }
                else
                {
                    // No more frames, use the last successful one
                    frame      = gl_last_frame_texture;
                    frame_info = last_frame_info;
                    break;
                }
            }

            if (!frame.has_value() && gl_last_frame_texture.has_value())
            {
                frame = gl_last_frame_texture;

                // We didn't get a frame, so we'll reuse the previous one
                // This effectively counts as a frame drop, the decoder couldn't decode a new frame
                // in time
                accumulated_delay++;
                measurements_bucket->add_dropped_frames();
            }
            if (!frame_info.has_value() && last_frame_info.has_value())
            {
                frame_info = last_frame_info;
            }
            if (frame.has_value() && frame_info.has_value())
            {
                frame_execution_time.frame_id                       = frame_info->frame_id;
                frame_execution_time.last_packet_received_timestamp = frame_info->last_packet_received_timestamp;
                frame_execution_time.pushed_to_decoder_timestamp    = frame_info->push_timestamp;

                something_was_rendered = draw_frame(frame, swapchain_framebuffer.images[image_index].framebuffer);
            }

            // Release image
            XrSwapchainImageReleaseInfo release_info {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xr_check(xrReleaseSwapchainImage(xr_swapchain, &release_info));

            frame_execution_time.after_render_timestamp = rtp_clock->now_rtp_timestamp();

            if (something_was_rendered)
            {
                std::optional<const PoseCacheEntry *> latest_frame_pose =
                    find_old_pose(frame.has_value() ? frame_info.value().pose_timestamp : latest_frame_pose_timestamp);

                if (latest_frame_pose.has_value())
                {
                    debug_pose = *latest_frame_pose.value();
                    // Use the value that was given to the driver
                    for (uint32_t i = 0; i < NB_EYES; i++)
                    {
                        views[i].type = XR_TYPE_VIEW;
                        views[i].pose = latest_frame_pose.value()->poses[i];
                        views[i].fov  = latest_frame_pose.value()->fovs[i];
                    }
                    display_time                            = latest_frame_pose.value()->xr_time;
                    frame_execution_time.tracking_timestamp = latest_frame_pose.value()->sample_timestamp;
                    frame_execution_time.pose_timestamp     = latest_frame_pose.value()->pose_timestamp;
                }
                else if (debug_pose.pose_timestamp != 0)
                {
                    // Use the last value that was given to the driver
                    for (uint32_t i = 0; i < NB_EYES; i++)
                    {
                        views[i].type = XR_TYPE_VIEW;
                        views[i].pose = debug_pose.poses[i];
                        views[i].fov  = debug_pose.fovs[i];
                    }
                    display_time                            = debug_pose.xr_time;
                    frame_execution_time.tracking_timestamp = debug_pose.sample_timestamp;
                    frame_execution_time.pose_timestamp     = debug_pose.pose_timestamp;
                }
                else
                {
                    // Locate views based on timestamp to have an approximation of the pose that was
                    // used to render the frame
                    XrViewLocateInfo view_locate_info {XR_TYPE_VIEW_LOCATE_INFO};
                    view_locate_info.viewConfigurationType = VIEW_CONFIGURATION_TYPE;
                    view_locate_info.displayTime =
                        (frame.has_value()) ? to_xr_time(frame_info.value().pose_timestamp) : xr_frame_state.predictedDisplayTime;
                    display_time           = view_locate_info.displayTime;
                    view_locate_info.space = xr_world_space;
                    uint32_t    view_count;
                    XrViewState xr_view_state {XR_TYPE_VIEW_STATE};
                    xr_check(xrLocateViews(xr_session, &view_locate_info, &xr_view_state, 0, &view_count, nullptr));
                    check(view_count == NB_EYES, "Unexpected number of views");
                    xr_check(xrLocateViews(xr_session, &view_locate_info, &xr_view_state, view_count, &view_count, views));

                    if (app_running)
                    {
                        LOG("Using approximate pose for frame %lu", frame_index);
                    }
                }

                // Submit frame
                for (int i = 0; i < NB_EYES; i++)
                {
                    projection_layer_views[i] = {
                        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
                        .pose = views[i].pose,
                        .fov  = views[i].fov,
                        .subImage =
                            {
                                .swapchain = xr_swapchain,
                                .imageRect =
                                    {
                                        .offset = {0, 0},
                                        .extent =
                                            {
                                                static_cast<int32_t>(specs.eye_resolution.width),
                                                static_cast<int32_t>(specs.eye_resolution.height),
                                            },
                                    },
                                .imageArrayIndex = static_cast<uint32_t>(i),
                            },
                    };
                }

                layers.emplace_back(XrCompositionLayerProjection {
                    .type       = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
                    .layerFlags = 0,
                    .space      = xr_world_space,
                    .viewCount  = NB_EYES,
                    .views      = projection_layer_views,
                });
            }
        }

        std::vector<XrCompositionLayerBaseHeader *> layers_ptr;
        for (auto &layer : layers)
        {
            layers_ptr.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer));
        }

        // End frame
        XrFrameEndInfo frame_end_info {XR_TYPE_FRAME_END_INFO};
        frame_end_info.displayTime          = display_time;
        frame_end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        frame_end_info.layerCount           = static_cast<uint32_t>(layers_ptr.size());
        frame_end_info.layers               = layers_ptr.empty() ? nullptr : layers_ptr.data();
        xr_check(xrEndFrame(xr_session, &frame_end_info));

        if (app_running && frame_index % 100 == 0)
        {
            timespec display_ts {};
            xr_check(xrConvertTimeToTimespecTimeKHR(xr_instance, xr_frame_state.predictedDisplayTime, &display_ts));

            timespec pose_ts = rtp_clock->to_timespec(latest_frame_pose_timestamp);

            auto delta_s = static_cast<double>(display_ts.tv_sec - pose_ts.tv_sec)
                           + static_cast<double>(display_ts.tv_nsec - pose_ts.tv_nsec) / NS_PER_SEC;

            LOG("Frame %lu | display: %lu.%09lu | pose: %lu.%09lu | delta: %f",
                frame_index,
                display_ts.tv_sec,
                display_ts.tv_nsec,
                pose_ts.tv_sec,
                pose_ts.tv_nsec,
                delta_s);
        }

        frame_execution_time.end_frame_timestamp = rtp_clock->now_rtp_timestamp();

        if (frame_execution_time.tracking_timestamp != 0)
        {
            // We know when the tracking was measured, and when the frame was rendered
            // We can compute how many frames ago the tracking was measured, to improve
            // the prediction of the timing of new frames
            const auto delay  = rtp::rtp_timestamps_distance_us(frame_execution_time.tracking_timestamp,
                                                               frame_execution_time.predicted_present_timestamp,
                                                               *rtp_clock);
            frames_in_advance = (delay.count() / specs.refresh_rate.inter_frame_delay_us()) - 1;
        }

        if (something_was_rendered)
        {
            measurements_bucket->add_image_quality_measurement({
                .frame_id        = static_cast<uint32_t>(frame_info->frame_id),
                .codestream_size = static_cast<uint32_t>(frame_info->frame_size),
                .raw_size        = static_cast<uint32_t>(frame->size),
                .psnr            = 0 // TODO
            });
        }
    }

    void VRSystem::Data::cleanup_gles()
    {
        if (gl_shader_program_nv12 != 0)
        {
            glDeleteProgram(gl_shader_program_nv12);
            gl_shader_program_nv12 = 0;
        }
    }

    void VRSystem::Data::handle_events()
    {
        XrResult result = XR_SUCCESS;

        while (result == XR_SUCCESS)
        {
            XrEventDataBuffer event_data {
                .type = XR_TYPE_EVENT_DATA_BUFFER,
                .next = nullptr,
            };
            result = xrPollEvent(xr_instance, &event_data);
            if (result == XR_SUCCESS)
            {
                LOG("Event received: %d", event_data.type);

                switch (event_data.type)
                {
                    case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                    {
                        LOGE("Instance loss pending");
                        break;
                    }
                    case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                    {
                        LOG("Session state changed");
                        XrEventDataSessionStateChanged &session_state_changed =
                            *reinterpret_cast<XrEventDataSessionStateChanged *>(&event_data);
                        switch (session_state_changed.state)
                        {
                            case XR_SESSION_STATE_READY:
                            {
                                begin_session();
                                break;
                            }
                            case XR_SESSION_STATE_STOPPING:
                            {
                                end_session();
                                break;
                            }
                            case XR_SESSION_STATE_EXITING:
                            {
                                should_exit = true;
                                break;
                            }
                            case XR_SESSION_STATE_LOSS_PENDING:
                            {
                                LOGE("Session loss pending");
                                break;
                            }
                            default: break;
                        }
                        break;
                    }
                    default: break;
                }
            }
        }
    }

    std::optional<const PoseCacheEntry *> VRSystem::Data::find_old_pose(uint32_t rtp_pose_timestamp) const
    {
        if (rtp_pose_timestamp == 0)
        {
            return std::nullopt;
        }

        int i = pose_cache_index - 1;
        if (i < 0)
        {
            i = TRACKING_STATE_CACHE_SIZE - 1;
        }

        do
        {
            if (pose_cache[i].pose_timestamp == rtp_pose_timestamp)
            {
                return &pose_cache[i];
            }
            else if (pose_cache[i].pose_timestamp < rtp_pose_timestamp)
            {
                break;
            }

            i--;
            if (i < 0)
            {
                i = TRACKING_STATE_CACHE_SIZE - 1;
            }
        } while (i != pose_cache_index);

        return std::nullopt;
    }

    bool VRSystem::Data::new_frame(ClientFrameTimeMeasurements &frame_execution_time)
    {
        if (!session_running)
        {
            return false;
        }

        frame_index++;

        frame_execution_time.frame_index        = frame_index;
        frame_execution_time.tracking_timestamp = 0;
        frame_execution_time.pose_timestamp     = 0;

        // Wait frame
        frame_execution_time.begin_wait_frame_timestamp = rtp_clock->now_rtp_timestamp();
        XrFrameWaitInfo wait_info {XR_TYPE_FRAME_WAIT_INFO};
        xr_check(xrWaitFrame(xr_session, &wait_info, &xr_frame_state));

        frame_execution_time.predicted_present_timestamp = to_rtp_timestamp(xr_frame_state.predictedDisplayTime);

        return true;
    }

    void VRSystem::Data::soft_shutdown()
    {
        // Reset decoder
        if (video_decoder != nullptr)
        {
            video_decoder             = nullptr;
            video_decoder_initialized = false;
        }

        // Reset state
        latest_frame_pose_timestamp = 0;
        frames_in_advance           = 3;
        last_tracking_time          = {};
        xr_frame_state              = {XR_TYPE_FRAME_STATE};
        gl_last_frame_texture       = std::nullopt;
        last_frame_info             = std::nullopt;
        frame_index                 = 0;
        app_running                 = false;
        frame_info_queue.clear();

        // Reset pose cache
        pose_cache_index = 0;
        for (auto &pose : pose_cache)
        {
            pose = {};
        }
        debug_pose = {};
    }

    bool VRSystem::Data::save_frame_if_needed(IOBuffer &image)
    {
        if (!last_frame_info.has_value() || !last_frame_info->should_save_frame)
        {
            return false;
        }

        if (!gl_last_frame_texture.has_value())
        {
            LOGE("No last frame texture");
            return false;
        }

        LOG("Saving frame %d", last_frame_info->frame_id);

        // Create RGBA render texture
        GLuint render_texture;
        glGenTextures(1, &render_texture);
        glBindTexture(GL_TEXTURE_2D, render_texture);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA,
                     static_cast<GLsizei>(specs.eye_resolution.width * NB_EYES),
                     static_cast<GLsizei>(specs.eye_resolution.height),
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     nullptr);

        // Create FBO
        GLuint fbo;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_texture, 0);

        GLenum draw_buffers[1] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, draw_buffers);

        // Render left eye

        bool something_was_rendered =
            draw_frame(gl_last_frame_texture, fbo, {specs.eye_resolution.width * 2, specs.eye_resolution.height}, {false, false});

        if (!something_was_rendered)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &render_texture);
            return false;
        }

        // Read pixels
        image.size = specs.eye_resolution.width * specs.eye_resolution.height * 4 * 2;
        image.data = new uint8_t[image.size];
        glBindTexture(GL_TEXTURE_2D, render_texture);
        glReadPixels(0,
                     0,
                     static_cast<GLsizei>(specs.eye_resolution.width * 2),
                     static_cast<GLsizei>(specs.eye_resolution.height),
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     image.data);
        gl_check(glGetError(), "failed to read pixels");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &render_texture);

        last_frame_info->should_save_frame = false;

        return true;
    }

    bool VRSystem::Data::draw_frame(const std::optional<GLFrameTexture> &frame,
                                    GLuint                               framebuffer,
                                    Extent2D                             extent,
                                    VertexShaderSettings                 settings) const
    {
        if (frame->format == ImageFormat::NV12)
        {
            // Bind framebuffer
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);

            // Set viewport
            glViewport(0,
                       0,
                       extent.width == 0 ? static_cast<GLsizei>(specs.eye_resolution.width) : static_cast<GLsizei>(extent.width),
                       extent.height == 0 ? static_cast<GLsizei>(specs.eye_resolution.height) : static_cast<GLsizei>(extent.height));

            // Set textures
            glUseProgram(gl_shader_program_nv12);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, frame.value().textures[0]);
            glUniform1i(glGetUniformLocation(gl_shader_program_nv12, "nv12_tex_y"), 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, frame.value().textures[1]);
            glUniform1i(glGetUniformLocation(gl_shader_program_nv12, "nv12_tex_uv"), 1);

            // Set settings
            glUniform1i(glGetUniformLocation(gl_shader_program_nv12, "flip_y"), settings.flip_y);
            glUniform1i(glGetUniformLocation(gl_shader_program_nv12, "split_texture"), settings.split_texture);

            glDrawArrays(GL_TRIANGLES, 0, 6);

            return true;
        }
        else if (frame.value().format == ImageFormat::R8G8B8A8_UNORM)
        {
            // Bind framebuffer
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);

            // Set viewport
            glViewport(0,
                       0,
                       extent.width == 0 ? static_cast<GLsizei>(specs.eye_resolution.width) : static_cast<GLsizei>(extent.width),
                       extent.height == 0 ? static_cast<GLsizei>(specs.eye_resolution.height) : static_cast<GLsizei>(extent.height));

            // Set textures
            glUseProgram(gl_shader_program_rgba);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, frame.value().textures[0]);
            glUniform1i(glGetUniformLocation(gl_shader_program_rgba, "rgba_tex"), 0);

            // Set settings
            glUniform1i(glGetUniformLocation(gl_shader_program_rgba, "flip_y"), settings.flip_y);
            glUniform1i(glGetUniformLocation(gl_shader_program_rgba, "split_texture"), settings.split_texture);

            glDrawArrays(GL_TRIANGLES, 0, 6);

            return true;
        }
        else if (frame.value().format == ImageFormat::B8G8R8A8_UNORM)
        {
            // Bind framebuffer
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);

            // Set viewport
            glViewport(0,
                       0,
                       extent.width == 0 ? static_cast<GLsizei>(specs.eye_resolution.width) : static_cast<GLsizei>(extent.width),
                       extent.height == 0 ? static_cast<GLsizei>(specs.eye_resolution.height) : static_cast<GLsizei>(extent.height));

            // Set textures
            glUseProgram(gl_shader_program_bgra);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, frame.value().textures[0]);
            glUniform1i(glGetUniformLocation(gl_shader_program_bgra, "bgra_tex"), 0);

            // Set settings
            glUniform1i(glGetUniformLocation(gl_shader_program_bgra, "flip_y"), settings.flip_y);
            glUniform1i(glGetUniformLocation(gl_shader_program_bgra, "split_texture"), settings.split_texture);

            glDrawArrays(GL_TRIANGLES, 0, 6);

            return true;
        }

        return false;
    }

    void VRSystem::Data::begin_session()
    {
        if (!session_running)
        {
            XrSessionBeginInfo session_begin_info {XR_TYPE_SESSION_BEGIN_INFO};
            session_begin_info.primaryViewConfigurationType = VIEW_CONFIGURATION_TYPE;
            xr_check(xrBeginSession(xr_session, &session_begin_info));
            LOG("-- OpenXR session started --");

            // Enable performance boost
            xr_check(xrPerfSettingsSetPerformanceLevelEXT(xr_session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, CPU_PERFORMANCE_MODE));
            xr_check(xrPerfSettingsSetPerformanceLevelEXT(xr_session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, GPU_PERFORMANCE_MODE));

            // Update refresh rate
            //            float refresh_rate = 0.0f;
            //            xr_check(xrGetDisplayRefreshRateFB(xr_session, &refresh_rate));
            //            specs.refresh_rate = {static_cast<uint32_t>(refresh_rate), 1};
            //            LOG("Refresh rate: %f Hz", refresh_rate);

            session_running = true;
            LOG("-- OpenXR session started --");
        }
    }

    void VRSystem::Data::end_session()
    {
        if (session_running)
        {
            xr_check(xrEndSession(xr_session));
            session_running = false;
        }
    }

    bool VRSystem::Data::get_frame_from_decoder(std::optional<GLFrameTexture> &frame, std::optional<FrameInfo> &frame_info)
    {
        frame      = std::nullopt;
        frame_info = std::nullopt;

        if (video_decoder != nullptr && video_decoder_initialized)
        {
            frame = video_decoder->get_frame_gpu();
            if (frame.has_value())
            {
                if (!frame_info_queue.empty())
                {
                    frame_info = frame_info_queue.front();
                    frame_info_queue.pop_front();
                    last_frame_info = frame_info;
                }
                else
                {
                    LOGE("Pulled frame, but didn't have any info in queue. Potential desynchronization !\n");
                }

                gl_last_frame_texture = frame;
                measurements_bucket->add_decoder_pulled_frame();
                return true;
            }
        }

        return false;
    }

    // =======================================================================================
    // =                                         API                                         =
    // =======================================================================================

    VRSystem::VRSystem(std::shared_ptr<rtp::RTPClock> rtp_clock, std::shared_ptr<ClientMeasurementBucket> measurements_bucket)
        : m_data {new Data {
            .measurements_bucket = std::move(measurements_bucket),
            .rtp_clock           = std::move(rtp_clock),
        }}
    {
        //        m_data->init_engine();
    }

    VRSystem::~VRSystem()
    {
        if (m_data != nullptr)
        {
            m_data->cleanup_engine();

            delete m_data;
        }
    }

    bool VRSystem::push_frame_data(const uint8_t *data,
                                   size_t         size,
                                   uint32_t       frame_id,
                                   bool           end_of_stream,
                                   uint32_t       timestamp,
                                   uint32_t       pose_timestamp,
                                   uint32_t       last_packet_received_timestamp,
                                   bool           save_frame) const
    {
        if (m_data->video_decoder == nullptr || !m_data->video_decoder_initialized)
        {
            throw std::runtime_error("Video decoder must not be null");
        }

        m_data->app_running       = true;
        const auto push_timestamp = m_data->rtp_clock->now_rtp_timestamp();
        bool       pushed         = m_data->video_decoder->push_packet(data, size, end_of_stream);
        if (pushed)
        {
            m_data->measurements_bucket->add_decoder_pushed_frame();
            m_data->frame_info_queue.push_back({
                .frame_id                       = frame_id,
                .end_of_stream                  = end_of_stream,
                .pose_timestamp                 = pose_timestamp,
                .push_timestamp                 = push_timestamp,
                .last_packet_received_timestamp = last_packet_received_timestamp,
                .frame_size                     = size,
                .should_save_frame              = save_frame,
            });
        }
        return pushed;
    }

    void VRSystem::set_decoder(const std::shared_ptr<IVideoDecoder> &video_decoder)
    {
        if (video_decoder == nullptr)
        {
            throw std::runtime_error("Video decoder must not be null");
        }
        m_data->video_decoder             = video_decoder;
        m_data->video_decoder_initialized = false;
    }

    void VRSystem::init(const ApplicationInfo &app_info)
    {
        LOG("Initializing VRSystem (OpenXR)");

        if (m_data->app != nullptr)
        {
            throw std::runtime_error("VRSystem already initialized");
        }

        if (app_info.android_app == nullptr)
        {
            throw std::runtime_error("Android app must not be null");
        }
        if (app_info.android_app->window == nullptr)
        {
            throw std::runtime_error("Android app window must not be null");
        }

        m_data->app           = app_info.android_app;
        m_data->native_window = app_info.android_app->window;

        m_data->init_engine();
    }

    void VRSystem::shutdown()
    {
        if (m_data->app == nullptr)
        {
            throw std::runtime_error("VRSystem not initialized");
        }

        m_data->cleanup_engine();

        m_data->app           = nullptr;
        m_data->native_window = nullptr;
    }

    void VRSystem::render(ClientFrameTimeMeasurements &frame_execution_time)
    {
        if (m_data->session_running)
        {
            m_data->render_frame(frame_execution_time);
        }
    }

    VRSystemSpecs VRSystem::specs() const
    {
        return m_data->specs;
    }

    bool VRSystem::get_pose(XrTime xr_time, TrackingState &out_tracking_state) const
    {
        const auto now = m_data->rtp_clock->now_rtp_timestamp();

        XrViewLocateInfo view_locate_info {XR_TYPE_VIEW_LOCATE_INFO};
        view_locate_info.viewConfigurationType = VIEW_CONFIGURATION_TYPE;
        view_locate_info.displayTime           = xr_time;
        view_locate_info.space                 = m_data->xr_world_space;
        uint32_t    view_count;
        XrViewState xr_view_state {XR_TYPE_VIEW_STATE};
        XrView      xr_views[NB_EYES] {XR_TYPE_VIEW};
        if (!xr_check(xrLocateViews(m_data->xr_session, &view_locate_info, &xr_view_state, NB_EYES, &view_count, xr_views)))
        {
            return false;
        }

        // Locate head reference space
        XrSpaceLocation xr_space_location {XR_TYPE_SPACE_LOCATION};
        if (!xr_check(xrLocateSpace(m_data->xr_head_space, m_data->xr_world_space, xr_time, &xr_space_location)))
        {
            return false;
        }

        // Convert to RTP timestamp
        uint32_t rtp_timestamp = m_data->to_rtp_timestamp(xr_time);

        // Save in cache
        auto &cache_slot            = m_data->pose_cache[m_data->pose_cache_index];
        cache_slot.pose_timestamp   = rtp_timestamp;
        cache_slot.sample_timestamp = now;
        cache_slot.xr_time          = xr_time;
        cache_slot.poses[EYE_LEFT]  = xr_views[EYE_LEFT].pose;
        cache_slot.poses[EYE_RIGHT] = xr_views[EYE_RIGHT].pose;
        cache_slot.fovs[EYE_LEFT]   = xr_views[EYE_LEFT].fov;
        cache_slot.fovs[EYE_RIGHT]  = xr_views[EYE_RIGHT].fov;
        m_data->pose_cache_index    = (m_data->pose_cache_index + 1) % TRACKING_STATE_CACHE_SIZE;

        out_tracking_state.pose_timestamp   = rtp_timestamp;
        out_tracking_state.sample_timestamp = cache_slot.sample_timestamp;
        out_tracking_state.pose             = to_pose(xr_space_location.pose);
        out_tracking_state.fov_left         = to_fov(xr_views[EYE_LEFT].fov);
        out_tracking_state.fov_right        = to_fov(xr_views[EYE_RIGHT].fov);

        m_data->measurements_bucket->add_tracking_time_measurement({
            .pose_timestamp               = rtp_timestamp,
            .tracking_received_timestamp  = now,
            .tracking_processed_timestamp = m_data->rtp_clock->now_rtp_timestamp(),
        });

        return true;
    }

    uint64_t VRSystem::ntp_epoch() const
    {
        return m_data->ntp_epoch();
    }

    bool VRSystem::get_next_tracking_state(TrackingState &out_tracking_state) const
    {
        // Get the next predicted display time
        const auto rtp = m_data->to_rtp_time_point(m_data->xr_frame_state.predictedDisplayTime)
                         + 1 * std::chrono::microseconds(m_data->specs.refresh_rate.inter_frame_delay_us());
        XrTime predicted_display_time =
            m_data->to_xr_time(m_data->rtp_clock->to_rtp_timestamp(std::chrono::time_point_cast<rtp::RTPClock::duration>(rtp)));

        // Get the predicted pose at that time
        return get_pose(predicted_display_time, out_tracking_state);
    }

    bool VRSystem::init_decoder()
    {
        return m_data->init_decoder();
    }

    bool VRSystem::new_frame(ClientFrameTimeMeasurements &frame_execution_time)
    {
        return m_data->new_frame(frame_execution_time);
    }

    void VRSystem::handle_events()
    {
        if (m_data->app == nullptr)
        {
            return;
        }

        m_data->handle_events();
    }

    void VRSystem::soft_shutdown()
    {
        m_data->soft_shutdown();
    }

    uint32_t VRSystem::get_decoder_frame_delay() const
    {
        if (!m_data || !m_data->video_decoder)
        {
            LOGE("Can't get decoder frame delay: decoder not initialized");
            return 0;
        }

        return m_data->video_decoder->get_frame_delay();
    }

    bool VRSystem::save_frame_if_needed(IOBuffer &image) const
    {
        return m_data->save_frame_if_needed(image);
    }

} // namespace wvb::client