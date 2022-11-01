#pragma once

#include <string>

namespace wvb
{
    struct Extent2D
    {
        uint32_t width  = 0;
        uint32_t height = 0;
    };

    struct RefreshRate
    {
        uint32_t numerator   = 0;
        uint32_t denominator = 1;

        [[nodiscard]] inline float to_float() const { return static_cast<float>(numerator) / static_cast<float>(denominator); }
    };

    struct VRSystemSpecs
    {
        /** @brief The name of the VR system (e.g "Oculus Quest 2") */
        std::string system_name;
        /** @brief The name of the VR system's manufacturer (e.g "Oculus") */
        std::string manufacturer_name;
        /** @brief The resolution of a single eye */
        Extent2D eye_resolution;
        /** @brief The refresh rate of the VR system */
        RefreshRate refresh_rate;
        /** @brief The inter-pupillary distance (IPD) of the VR system */
        float ipd;
    };
} // namespace wvb