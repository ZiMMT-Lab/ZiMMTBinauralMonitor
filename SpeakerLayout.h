#pragma once

#include <array>
#include <cmath>

//==============================================================================
/**
    Geometry of the 32-loudspeaker ZiMMT dome.

    Azimuth 0 deg = front, 90 deg = left (increasing counter-clockwise)
    elevation 0 deg = front, 90 deg = zenith 
*/
namespace zimmt
{
    inline constexpr int numSpeakers = 32;

    struct AzEl { float azDeg, elDeg; };

    inline constexpr std::array<AzEl, numSpeakers> speakerAzEl
    {{
        {  22.5f,  0.0f }, { 337.5f,  0.0f }, { 292.5f,  0.0f }, { 248.0f,  0.0f },
        { 202.5f,  0.0f }, { 157.5f,  0.0f }, { 122.6f,  0.0f }, {  67.5f,  0.0f },
        {  45.0f, 14.6f }, {   0.0f, 14.4f }, { 315.0f, 14.8f }, { 270.0f, 21.4f },
        { 225.0f, 15.9f }, { 180.0f, 14.4f }, { 135.0f, 16.0f }, {  90.0f, 22.4f },
        {  22.5f, 28.1f }, { 337.5f, 28.1f }, { 292.5f, 40.3f }, { 247.9f, 41.0f },
        { 202.5f, 33.7f }, { 157.5f, 33.7f }, { 112.1f, 41.0f }, {  67.5f, 40.0f },
        {  27.4f, 65.5f }, { 331.8f, 65.4f }, { 270.0f, 68.0f }, { 208.0f, 65.4f },
        { 152.2f, 65.3f }, {  90.0f, 68.0f }, {  90.0f, 85.2f }, { 270.0f, 85.2f },
    }};

    /** A point on the unit dome in display space: x = right, y = up, z = front. */
    struct Vec3 { float x, y, z; };

    inline Vec3 toUnitVector (AzEl p) noexcept
    {
        const float d2r = 3.14159265358979323846f / 180.0f;
        const float a = p.azDeg * d2r, e = p.elDeg * d2r;
        return { -std::cos (e) * std::sin (a), std::sin (e), std::cos (e) * std::cos (a) };
    }
}
