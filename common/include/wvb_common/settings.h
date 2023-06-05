#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wvb
{

    #define WVB_DEFAULT_STEAMVR_PATH "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR"
    #define WVB_STEAMVR_EXE_PATH "bin\\win64\\vrstartup.exe"

    enum AppMode : uint8_t
    {
        UNKNOWN = 0,
        /** Normal mode (proof-of-concept mode): connects the client to Steam VR until any of the components is closed.
         * Frames are rendered based on the actual input sampled from the headset. */
        NORMAL = 1,
        /**
         * Benchmark mode: run the system for a limited time, and take various measures during time windows. Repeat the execution
         * several times. Various configurations can also be specified. All results are exported to files. Input is simulated in a
         * repeatable way to try to make each run as similar as possible.
         */
        BENCHMARK = 2,
    };

    struct CodecSettings
    {
        uint8_t bpp = 3;
        int16_t  delay = 0;
        uint32_t bitrate = 0;
    };

    /** A benchmark pass represents a single configuration that can be measured several times. */
    struct BenchmarkPass
    {
        uint8_t pass_index = 0;
        /**
         * Id of the codec that will be used during this pass.
         * If it doesn't exist, the pass will be skipped.
         */
        std::string codec_id = "";
        /** Codec parameters for this pass. Parameters that don't apply to the codec are ignored. */
        CodecSettings codec_settings {};
        /**
         * Number of times that measurements with this config should be repeated,
         * in order to limit the impact of randomness.
         *
         * CLI key: 'n'
         */
        uint32_t num_repetitions = 10;
        /**
         * Number of milliseconds between the moment when the app starts running and the start
         *  of the measurements.
         *
         * CLI key; 'ds'
         * */
        uint32_t duration_startup_phase_ms = 15000;
        /**
         * Number of milliseconds during which to measure the frame times and network rates.
         *
         * CLI key: 'dt'
         */
        uint32_t duration_timing_phase_ms = 4000;
        /**
         * Number of milliseconds during which to measure image stats.
         *
         * CLI key: 'dq'
         * */
        uint32_t duration_frame_quality_phase_ms = 200;
        /**
         * Number of milliseconds after the end of the measurements before sending the results.
         * 
         * CLI key: 'de'
        */
        uint32_t duration_end_margin_ms = 4000;
    };

    struct BenchmarkSettings
    {
        /** List of all configurations to measure, in order. */
        std::vector<BenchmarkPass> passes;
        /** Number of milliseconds between the end of a run and the start of the next one. */
        uint32_t duration_inter_run_interval_ms = 5000;
    };

    struct NetworkSettings
    {
        /** Number of ping replies that the client waits until it ends the sync phase.
         * If packets are dropped and less replies are received, the client will stop
         * after sending 2*ping_count pings.
         *
         * CLI key: 'pc'
         */
        uint8_t ping_count = 20;
        /** Number of milliseconds between the reception of a ping reply (or timeout) and the sending of the next ping.
         *
         * CLI key: 'pi'
         */
        uint16_t ping_interval_ms = 200;
        /** Number of milliseconds to wait for a ping reply before considering the ping lost.
         *
         * CLI key: 'pt'
         */
        uint16_t ping_timeout_ms = 500;
    };

    struct AppSettings
    {
        AppMode app_mode = AppMode::NORMAL;
        /** Codec that is used when in normal mode.
         * In benchmark mode, the value defined in the pass is used instead. */
        std::string       preferred_codec = "h265";
        std::string       steamvr_path = WVB_DEFAULT_STEAMVR_PATH;
        NetworkSettings   network_settings {};
        BenchmarkSettings benchmark_settings {};
    };

    constexpr std::string to_string(AppMode mode)
    {
        switch (mode)
        {
            case AppMode::NORMAL: return "NORMAL";
            case AppMode::BENCHMARK: return "BENCHMARK";
            default: return "INVALID";
        }
    }
} // namespace wvb
