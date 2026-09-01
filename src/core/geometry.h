#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace editor {

struct Vec2 {
    float x = 0;
    float y = 0;
};

struct Mat3 {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
};

inline Mat3 mul(const Mat3& p, const Mat3& q) {
    return {
        p.a * q.a + p.c * q.b,
        p.b * q.a + p.d * q.b,
        p.a * q.c + p.c * q.d,
        p.b * q.c + p.d * q.d,
        p.a * q.tx + p.c * q.ty + p.tx,
        p.b * q.tx + p.d * q.ty + p.ty,
    };
}

inline Mat3 transform(Vec2 p, Vec2 s, float deg) {
    float r = deg * 3.1415926535f / 180.f;
    float cs = std::cos(r);
    float sn = std::sin(r);
    return {cs * s.x, sn * s.x, -sn * s.y, cs * s.y, p.x, p.y};
}

inline std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

inline std::string slash(const std::filesystem::path& p) {
    return p.generic_string();
}

} // namespace editor
