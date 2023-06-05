#pragma once

#include <string>

#define NB_EYES   2
#define EYE_LEFT  0
#define EYE_RIGHT 1

namespace wvb
{

    typedef uint64_t SharedTextureHandle;

    struct Extent2D
    {
        uint32_t width  = 0;
        uint32_t height = 0;
    };

    struct Extent2Df
    {
        float width  = 0;
        float height = 0;
    };

    struct RefreshRate
    {
        uint32_t numerator   = 0;
        uint32_t denominator = 1;

        [[nodiscard]] inline float to_float() const { return static_cast<float>(numerator) / static_cast<float>(denominator); }

        [[nodiscard]] inline uint32_t inter_frame_delay_us() const { return (denominator * 1000000) / numerator; }
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
        /** @brief The distance from the eye to the center of the head */
        float eye_to_head_distance;
        /** @brief the bounds of the world in meters */
        Extent2Df world_bounds;
    };

    struct Quaternion
    {
        float x = 0;
        float y = 0;
        float z = 0;
        float w = 1;
    };

    template<typename T = float>
    struct Vector2
    {
        T x = 0;
        T y = 0;

        constexpr Vector2 operator+(const Vector2 &other) const { return Vector2 {x + other.x, y + other.y}; }
        constexpr Vector2 operator+(Vector2 &&other) const { return Vector2 {x + other.x, y + other.y}; }

        constexpr Vector2 operator/(const float &other) const { return Vector2 {x / other, y / other}; }
        constexpr Vector2 operator/(float &&other) const { return Vector2 {x / other, y / other}; }
    };

    template<typename T = float>
    struct Vector3
    {
        T x = 0;
        T y = 0;
        T z = 0;

        constexpr Vector3 operator+(const Vector3 &other) const { return Vector3 {x + other.x, y + other.y, z + other.z}; }
        constexpr Vector3 operator+(Vector3 &&other) const { return Vector3 {x + other.x, y + other.y, z + other.z}; }

        constexpr Vector3 operator/(const float &other) const { return Vector3 {x / other, y / other, z / other}; }
        constexpr Vector3 operator/(float &&other) const { return Vector3 {x / other, y / other, z / other}; }
    };

    struct Pose
    {
        Quaternion     orientation;
        Vector3<float> position;
    };

    struct Fov
    {
        float left  = 0;
        float right = 0;
        float up    = 0;
        float down  = 0;
    };

    struct TrackingState
    {
        uint32_t sample_timestamp = 0;
        uint32_t pose_timestamp   = 0;
        Pose     pose;
        Fov      fov_left;
        Fov      fov_right;
    };

} // namespace wvb