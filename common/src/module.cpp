#include <wvb_common/formats/h264.h>
#include <wvb_common/formats/hevc.h>
#include <wvb_common/formats/av1.h>
#include <wvb_common/formats/vp9.h>
#include <wvb_common/module.h>

#include <filesystem>
#include <iostream>
#include <optional>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace wvb
{

    bool is_module_file(std::string file_name)
    {
        // A module should start with "wvb_module_"
        // On Linux, it may be preceded by "lib" or "lib_"
        if (file_name.size() < 12)
        {
            return false;
        }

        // Remove lib prefix
        if (file_name.starts_with("lib"))
        {
            file_name = file_name.substr(3);
        }
        else if (file_name.starts_with("lib_"))
        {
            file_name = file_name.substr(4);
        }

        if (file_name.starts_with("wvb_module_"))
        {
            // Valid extensions are .so, .dylib, .dll
            if (file_name.ends_with(".dll") || file_name.ends_with(".so") || file_name.ends_with(".dylib"))
            {
                return true;
            }
        }

        return false;
    }

    std::optional<Module> load_module(const std::string &file_name)
    {
#ifdef _WIN32
        // Load DLL

        HINSTANCE hGetProcIDDLL = LoadLibrary(file_name.c_str());
        if (hGetProcIDDLL == nullptr)
        {
            char error_code[256];
            FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, (LPSTR) error_code, 256, nullptr);

            std::cerr << "Failed to load module " << file_name << ": " << std::string(error_code) << "\n";

            return std::nullopt;
        }

        // Get function
        auto get_module_info = (GetModuleInfoFunction) GetProcAddress(hGetProcIDDLL, "get_module_info");
        if (get_module_info == nullptr)
        {
            char error_code[256];
            FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, (LPSTR) error_code, 256, nullptr);

            std::cerr << "Failed to load module " << file_name << ": " << std::string(error_code) << "\n";
            return std::nullopt;
        }

        auto m   = get_module_info();
        m.handle = hGetProcIDDLL;
        return m;
#else
        // Load shared library
        void *handle = dlopen(file_name.c_str(), RTLD_LAZY);
        if (!handle)
        {
            std::cerr << "Failed to load module " << dlerror() << "\n";
            return std::nullopt;
        }

        // Get function
        GetModuleInfoFunction get_module_info = (GetModuleInfoFunction) dlsym(handle, "get_module_info");
        if (!get_module_info)
        {
            std::cerr << "Failed to load module " << dlerror() << "\n";
            return std::nullopt;
        }

        Module m = get_module_info();
        m.handle = handle;
        return m;
#endif
    }

    void Module::close() const
    {
        if (handle)
        {
#ifdef _WIN32
            FreeLibrary((HINSTANCE) handle);
#else
            dlclose(handle);
#endif
        }
    }

    std::vector<Module> load_modules()
    {
        std::vector<Module> modules;

        // Add built-in modules
        modules.push_back(Module {
            .codec_id = "h265",
            .name     = "H.265",
#ifdef _WIN32
            .create_video_encoder = wvb::create_hevc_encoder,
#endif
#ifdef __ANDROID__
            .create_video_decoder = wvb::create_hevc_decoder,
#endif
        });
        modules.push_back(Module {
            .codec_id            = "h264",
            .name                = "H.264",
            .create_packetizer   = wvb::create_h264_rtp_packetizer,
            .create_depacketizer = wvb::create_h264_rtp_depacketizer,
#ifdef _WIN32
            .create_video_encoder = wvb::create_h264_encoder,
#endif
#ifdef __ANDROID__
            .create_video_decoder = wvb::create_h264_decoder,
#endif
        });
        modules.push_back(Module {
            .codec_id = "av1",
            .name     = "AV1",
#ifdef _WIN32
            .create_video_encoder = wvb::create_av1_encoder,
#endif
#ifdef __ANDROID__
            .create_video_decoder = wvb::create_av1_decoder,
#endif
        });
        modules.push_back(Module {
            .codec_id = "vp9",
            .name     = "VP9",
#ifdef _WIN32
            .create_video_encoder = wvb::create_vp9_encoder,
#endif
#ifdef __ANDROID__
            .create_video_decoder = wvb::create_vp9_decoder,
#endif
        });

#ifndef __ANDROID__
        // List files in current directory
        // If file name starts with "wvb_module_", load it
        for (const auto &entry : std::filesystem::directory_iterator("."))
        {
            if (entry.is_regular_file() && is_module_file(entry.path().filename().string()))
            {
                std::cout << "Found module: " << entry.path().filename().string() << "\n";

                // Try to call the "wvb::Module get_module_info()" function
                auto module = load_module(entry.path().string());
                if (module.has_value() && !module->codec_id.empty() && !module->name.empty())
                {
                    modules.push_back(module.value());
                }
            }
        }
#else
        // On Android, we don't have the permissions to list files in the current directory
        // In the future, we could try other methods to look for modules
        // For example, the server could send the list of the modules it has so the app can
        // try to load them
        // TODO
#endif

        return modules;
    }

} // namespace wvb
