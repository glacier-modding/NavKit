#pragma once
#include "../../NavWeakness/Vec3.h"
#include "../../ResourceLib_HM3/Generated/ZHMGen.h"

namespace Math {
    class Plane {
    public:
        Plane(): m_normal(Vec3()), m_d(0) {
        }

        Plane(const Vec3 &normal, const float d) : m_normal(normal), m_d(d) {
        }

        Vec3 m_normal;
        float m_d;
    };

    float DistanceSquared(const float4 &a, const float4 &b);

    float DistanceSquared(const float4 &v);

    float DotProduct(const float4 &a, const float4 &b);

    float4 CrossProduct(const float4 &a, const float4 &b);

    Vec3 CrossProduct(const Vec3 &a, const Vec3 &b);

    float4 operator/(const float4 &v, float scalar);

    float4 Normalize(const float4 &v);

    Vec3 Normalize(const Vec3 &v);

    Vec3 calculatePlaneNormal(const Vec3 &point1, const Vec3 &point2, const Vec3 &point3);

    Plane buildPlane(const Vec3 a, const Vec3 b, const Vec3 c);

    float Distance(const SVector3 *a, const float4 *b);

    float Distance(const float4 *a, const float4 *b);

    float Length(const float4 &vector);

    bool RayAABBIntersect(float4 *vIntersectionPoint, const float4 *vMin, const float4 *vMax,
                          const float4 *vStart,
                          const float4 *vDirection);

    class Quaternion {
    public:
        Quaternion() : x(0), y(0), z(0), w(0) {}
        Quaternion(const float x, const float y, const float z, const float w) : x(x), y(y), z(z), w(w) {
        }

        float x, y, z, w;
    };

    Vec3 rotatePoint(const Vec3 &point, const Quaternion &quaternion);
}
