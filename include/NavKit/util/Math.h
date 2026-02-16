#pragma once
#include "../../NavWeakness/Vec3.h"
#include "../../ResourceLib_HM3/Generated/HM3/ZHMGen.h"

namespace Math {
    class Plane {
    public:
        Plane() : mNormal(Vec3()), mD(0) {}

        Plane(const Vec3& normal, const float d) : mNormal(normal), mD(d) {}

        Vec3 mNormal;
        float mD;
    };

    float distanceSquared(const float4& a, const float4& b);

    float distanceSquared(const float4& v);

    float dotProduct(const float4& a, const float4& b);

    float4 crossProduct(const float4& a, const float4& b);

    Vec3 crossProduct(const Vec3& a, const Vec3& b);

    float4 operator/(const float4& v, float scalar);

    float4 normalize(const float4& v);

    Vec3 normalize(const Vec3& v);

    Vec3 calculatePlaneNormal(const Vec3& point1, const Vec3& point2, const Vec3& point3);

    Plane buildPlane(Vec3 a, Vec3 b, Vec3 c);

    float distance(const SVector3* a, const float4* b);

    float distance(const float4* a, const float4* b);

    float length(const float4& vector);

    bool rayAabbIntersect(float4* vIntersectionPoint, const float4* vMin, const float4* vMax,
                          const float4* vStart,
                          const float4* vDirection);

    class Quaternion {
    public:
        Quaternion() : x(0), y(0), z(0), w(0) {}

        Quaternion(const float x, const float y, const float z, const float w) : x(x), y(y), z(z), w(w) {}

        float x, y, z, w;
    };

    Vec3 rotatePoint(const Vec3& point, const Quaternion& quaternion);
}
