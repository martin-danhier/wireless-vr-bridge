#include "wvb_server/arg_parser.h"

#include <wvb_common/macros.h>

namespace wvb::server
{
    enum class MultiArgType
    {
        NONE,
        BENCHMARK_PASSES,
        NETWORK_SETTINGS,
    };

    typedef bool (*ParseFieldFunc)(std::string &field, uint32_t field_index, void *user_data);

    std::optional<uint32_t> parse_numerical_field(const std::string &val,
                                                  const std::string &field_name,
                                                  uint32_t           min_value = 0,
                                                  uint32_t           max_value = UINT32_MAX)
    {
        // Sanitize input
        bool valid = true;
        if (val.empty())
        {
            valid = false;
        }
        else
        {
            for (char c : val)
            {
                if (!std::isdigit(c))
                {
                    valid = false;
                    break;
                }
            }
        }

        if (!valid)
        {
            LOGE("Expected a numerical value after field \"%s\". Example: %s=5\n", field_name.c_str(), field_name.c_str());
            return std::nullopt;
        }

        uint32_t value = std::stoul(val);

        if (value < min_value || value > max_value)
        {
            LOGE("Value for field \"%s\" is out of range. Expected value between %u and %u.\n",
                 field_name.c_str(),
                 min_value,
                 max_value);
            return std::nullopt;
        }

        return value;
    }

    bool parse_benchmark_pass_field(std::string &field, uint32_t field_index, BenchmarkPass *pass)
    {
        if (field_index == 0)
        {
            if (field.empty())
            {
                LOGE("Expected a codec id for benchmark pass #%u.\n", pass->pass_index);
                return false;
            }

            // Codec id mandatory field
            pass->codec_id = field;
        }
        else
        {
            if (field.empty())
            {
                // Skip
                return true;
            }

            // Split by '='
            std::string str_val   = "";
            size_t      split_pos = field.find('=');
            if (split_pos != std::string::npos)
            {
                str_val = field.substr(split_pos + 1);
                field   = field.substr(0, split_pos);
            }

            // Parse field
            if (field == "n")
            {
                auto val = parse_numerical_field(str_val, "n", 1);
                if (!val.has_value())
                {
                    return false;
                }
                pass->num_repetitions = val.value();
            }
            else if (field == "ds")
            {
                auto val = parse_numerical_field(str_val, "ds");
                if (!val.has_value())
                {
                    return false;
                }
                pass->duration_startup_phase_ms = val.value();
            }
            else if (field == "dt")
            {
                auto val = parse_numerical_field(str_val, "dt");
                if (!val.has_value())
                {
                    return false;
                }
                pass->duration_timing_phase_ms = val.value();
            }
            else if (field == "dq")
            {
                auto val = parse_numerical_field(str_val, "dq");
                if (!val.has_value())
                {
                    return false;
                }
                pass->duration_frame_quality_phase_ms = val.value();
            }
            else if (field == "de")
            {
                auto val = parse_numerical_field(str_val, "de");
                if (!val.has_value())
                {
                    return false;
                }
                pass->duration_end_margin_ms = val.value();
            }
            else if (field == "delay")
            {
                if (str_val == "auto")
                {
                    pass->codec_settings.delay = -1;
                }
                else
                {
                    auto val = parse_numerical_field(str_val, "delay", 0, UINT8_MAX);
                    if (!val.has_value())
                    {
                        return false;
                    }
                    pass->codec_settings.delay = static_cast<uint8_t>(val.value());
                }
            }
            else if (field == "bpp")
            {
                auto val = parse_numerical_field(str_val, "bpp", 1, UINT8_MAX);
                if (!val.has_value())
                {
                    return false;
                }
                pass->codec_settings.bpp = static_cast<uint8_t>(val.value());
            }
            else if (field == "bitrate")
            {
                auto val = parse_numerical_field(str_val, "bitrate");
                if (!val.has_value())
                {
                    return false;
                }
                pass->codec_settings.bitrate = val.value();
            }
            else
            {
                LOGE("Invalid field \"%s\" for benchmark pass #%u.\n", field.c_str(), pass->pass_index);
                return false;
            }
        }

        return true;
    }
    bool parse_network_settings_field(std::string &field, uint32_t field_index, NetworkSettings *settings)
    {
        if (field.empty())
        {
            // Skip
            return true;
        }

        // Split by '='
        std::string str_val   = "";
        size_t      split_pos = field.find('=');
        if (split_pos != std::string::npos)
        {
            str_val = field.substr(split_pos + 1);
            field   = field.substr(0, split_pos);
        }

        // Parse field
        if (field == "pc")
        {
            auto val = parse_numerical_field(str_val, "pc", 1);
            if (!val.has_value())
            {
                return false;
            }
            settings->ping_count = val.value();
        }
        else if (field == "pi")
        {
            auto val = parse_numerical_field(str_val, "pi");
            if (!val.has_value())
            {
                return false;
            }
            settings->ping_interval_ms = val.value();
        }
        else if (field == "pt")
        {
            auto val = parse_numerical_field(str_val, "pt", 1);
            if (!val.has_value())
            {
                return false;
            }
            settings->ping_timeout_ms = val.value();
        }
        else
        {
            LOGE("Invalid field \"%s\" for network settings.\n", field.c_str());
            return false;
        }

        return true;
    }

    int32_t parse_multi_arg(std::string &arg, ParseFieldFunc pfn_parse_field, void *user_data)
    {
        // Split by ';' and iterate
        uint32_t field_index = 0;
        bool     has_next    = true;
        while (has_next)
        {
            auto        pos = arg.find(';');
            std::string field;
            if (pos == std::string::npos)
            {
                // Last element
                has_next = false;
                field    = arg;
            }
            else
            {
                field = arg.substr(0, pos);
                arg   = arg.substr(pos + 1);
            }

            if (pfn_parse_field != nullptr)
            {
                if (!pfn_parse_field(field, field_index, user_data))
                {
                    return -1;
                }
            }

            field_index++;
        }

        return field_index;
    }

    std::optional<AppSettings> parse_arguments(int argc, char **argv)
    {
        // Generate default settings
        AppSettings  settings {};
        MultiArgType prev_arg_type   = MultiArgType::NONE;
        uint8_t      multi_arg_index = 0;

        // No arguments, use default settings
        if (argc <= 1 || argv == nullptr)
        {
            LOG("Using default settings.\n");
            return std::move(settings);
        }

        for (int i = 1; i < argc; i++)
        {
            char *arg = argv[i];
            if (arg == nullptr) // Invalid argument
            {
                continue;
            }
            std::string str_arg(arg);

            bool valid = true;

            if (str_arg.starts_with('-'))
            {
                prev_arg_type   = MultiArgType::NONE;
                multi_arg_index = 0;

                // Split arg at the first '=' sign
                std::string str_val   = "";
                size_t      split_pos = str_arg.find('=');
                if (split_pos != std::string::npos)
                {
                    str_val = str_arg.substr(split_pos + 1);
                    str_arg = str_arg.substr(0, split_pos);
                }

                if (str_arg == "-h" || str_arg == "--help")
                {
                    // Print usage
                    return std::nullopt;
                }
                else if (str_arg == "-b" || str_arg == "--benchmark")
                {
                    // Run in benchmark mode
                    settings.app_mode = AppMode::BENCHMARK;
                    prev_arg_type     = MultiArgType::BENCHMARK_PASSES;
                }
                else if (str_arg == "-n" || str_arg == "--network")
                {
                    // Specify network settings
                    prev_arg_type = MultiArgType::NETWORK_SETTINGS;
                }
                else if (str_arg == "-ri" || str_arg == "--run-interval")
                {
                    auto value = parse_numerical_field(str_val, "--run-interval");
                    if (!value.has_value())
                    {
                        return std::nullopt;
                    }
                    settings.benchmark_settings.duration_inter_run_interval_ms = value.value();
                }
                else if (str_arg == "-sp" || str_arg == "--steamvr-path")
                {
                    if (str_val.size() <= 2 || str_val[0] != '"' || str_val[str_val.size() - 1] != '"') // Minimum two ""
                    {
                        LOGE("Expected an absolute path after --steamvr-path. Example: --steamvr-path=\"C:\\Program Files "
                             "(x86)\\Steam\\steamapps\\common\\SteamVR\"\n");
                        return std::nullopt;
                    }
                    settings.steamvr_path = str_val.substr(1, str_val.size() - 2);
                }
                else if (str_arg == "-c" || str_arg == "--codec")
                {
                    if (str_val.empty())
                    {
                        LOGE("Expected a codec ID after --codec. Example: --codec=h264\n");
                        return std::nullopt;
                    }
                    settings.preferred_codec = str_val;
                }
                else
                {
                    valid = false;
                }
            }
            // Multi-argument
            else if (prev_arg_type == MultiArgType::BENCHMARK_PASSES)
            {
                BenchmarkPass pass {
                    .pass_index = multi_arg_index++,
                };

                auto count = parse_multi_arg(str_arg, reinterpret_cast<ParseFieldFunc>(&parse_benchmark_pass_field), &pass);
                if (count == -1)
                {
                    return std::nullopt;
                }

                if (pass.codec_id.empty())
                {
                    LOGE("Codec ID is mandatory\n");
                    return std::nullopt;
                }

                settings.benchmark_settings.passes.push_back(pass);
            }
            else if (prev_arg_type == MultiArgType::NETWORK_SETTINGS)
            {
                if (parse_multi_arg(str_arg,
                                    reinterpret_cast<ParseFieldFunc>(&parse_network_settings_field),
                                    &settings.network_settings)
                    == -1)
                {
                    return std::nullopt;
                }
                // Can only have one network settings argument
                prev_arg_type = MultiArgType::NONE;
            }
            else
            {
                valid = false;
            }

            if (!valid)
            {
                LOGE("Invalid argument: \"%s\"\n", arg);
                return std::nullopt;
            }
        }

        if (settings.app_mode == AppMode::BENCHMARK && settings.benchmark_settings.passes.empty())
        {
            LOGE("Benchmark mode enabled, but no benchmark passes specified.\n");
            return std::nullopt;
        }

        return std::move(settings);
    }

    void print_usage()
    {
        LOG("Usage: wvb_server [options]\n\n");

        LOG("Options:\n");
        LOG("    -h,  --help         \t\tPrint this help message\n");
        LOG("    -b,  --benchmark    \t\tRun in benchmark mode (execute benchmark passes and save measurements) and specify passes "
            "(see "
            "below)\n");
        LOG("    -n,  --network      \t\tSpecify network settings (see below)\n");
        LOG("    -ri, --run-interval \t\tSpecify the interval between two benchmark runs in milliseconds. Default = 5000\n");
        LOG("    -c,  --codec        \t\tSpecify the codec to use when in normal mode (see available ones below). Ignored for "
            "benchmarking. Default = h265\n");
        LOG("    -sp, --steamvr-path \t\tSpecify the path to the SteamVR installation. Default = \"C:\\Program Files "
            "(x86)\\Steam\\steamapps\\common\\SteamVR\"\n");

        LOG("\nBenchmark passes syntax:\n");
        LOG("    -b \"<benchmark_pass_1>\" \"<benchmark_pass_2>\" ...\n");
        LOG("        A benchmark pass is a string of the form <codec_id>[;<option key>=<value>]\n");
        LOG("    Built-in codecs (others may be available through modules):\n");
        LOG("        h264: H.264 NVENC\n");
        LOG("        h265: H.265 NVENC\n");
        LOG("        vp9:  VP9\n");
        LOG("        av1:  AV1\n\n");
        LOG("    Available options:\n");
        LOG("        n=<number of repetitions>:   Number of runs with this configuration.              Default = 10\n");
        LOG("        ds=<startup phase duration>: Duration of the startup phase in milliseconds.       Default = 15000\n");
        LOG("        dt=<timing phase duration>:  Duration of the timing phase in milliseconds.        Default = 4000\n");
        LOG("        dq=<quality phase duration>: Duration of the frame quality phase in milliseconds. Default = 200\n");
        LOG("        de=<quality phase duration>: Duration of the end margin phase in milliseconds.    Default = 4000\n");
        LOG("        delay=<encoder frame delay>: Number of frames to delay the encoder.               Default = 0\n");
        LOG("        bpp=<bits per pixel>:        Target bits per pixel. (0 = auto)                    Default = 0\n");
        LOG("        bitrate=<bitrate>:           Target bitrate in bits per second. (0 = auto)        Default = 0\n");

        LOG("\nNetwork settings syntax:\n");
        LOG("    -n \"<option key>=<value>[;<option key>=<value>]\"\n");
        LOG("    Available options:\n");
        LOG("        pc=<ping count>:    Number of ping sent by the client during the sync phase.      Default = 10\n");
        LOG("                            Client will send between pc and 2*pc pings depending on packet losses.\n");
        LOG("        pi=<ping interval>: Interval in milliseconds between reply/timeout and next ping. Default = 200\n");
        LOG("        pt=<ping timeout>:  Timeout in milliseconds for a ping reply.                     Default = 500\n");

        LOG("\nExamples:\n");
        LOG("    wvb_server --benchmark \"h264;n=10;ds=10000;dt=2000;dq=200\" \"h265;n=10;ds=10000;dt=2000;dq=200\" --network "
            "\"pc=10;pi=200;pt=500\" --run-interval=3000\n");
        LOG("        Run the benchmark with 2 passes: one in h264, and one in h265. Other parameters are equivalent to the default "
            "values.\n");
        LOG("    wvb_server -b \"h264\" \"h265\"\n");
        LOG("        Shorter equivalent to the above command.\n");
        LOG("    wvb_server -c=h265\n");
        LOG("        Run in normal mode with h265 codec.\n");
    }
} // namespace wvb::server
