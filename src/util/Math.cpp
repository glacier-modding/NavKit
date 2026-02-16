#pragma once

#include "../../include/NavKit/util/Math.h"
#include <algorithm>

namespace Math {
    float distanceSquared(const float4& a, const float4& b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    float distanceSquared(const float4& v) {
        return v.x * v.x + v.y * v.y + v.z * v.z;
    }

    float dotProduct(const float4& a, const float4& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    float4 crossProduct(const float4& a, const float4& b) {
        return float4{
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x, 0.0f
        };
    }

    Vec3 crossProduct(const Vec3& a, const Vec3& b) {
        return Vec3{
            a.Y * b.Z - a.Z * b.Y,
            a.Z * b.X - a.X * b.Z,
            a.X * b.Y - a.Y * b.X
        };
    }

    float4 operator/(const float4& v, float scalar) {
        return float4(v.x / scalar, v.y / scalar, v.z / scalar, v.w / scalar);
    }

    float4 normalize(const float4& v) {
        const float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);

        if (length > 0.0f) {
            return v / length;
        } else {
            return v;
        }
    }

    Vec3 normalize(const Vec3& v) {
        if (const float length = sqrtf(v.X * v.X + v.Y * v.Y + v.Z * v.Z); length > 0.0f) {
            return v / length;
        }
        return v;
    }

    Vec3 calculatePlaneNormal(const Vec3& point1, const Vec3& point2, const Vec3& point3) {
        const Vec3 v1 = point2 - point1;
        const Vec3 v2 = point3 - point1;

        return normalize(v1.Cross(v2));
    }

    Plane buildPlane(const Vec3 a, const Vec3 b, const Vec3 c) {
        Vec3 normal = calculatePlaneNormal(a, b, c);
        const float d = -normal.Dot(a);
        return Plane({normal.X, normal.Y, normal.Z}, d);
    }

    float distance2D(const SVector3* a, const float4* b) {
        const float dx = a->x - b->x;
        const float dy = a->y - b->y;
        return sqrtf(dx * dx + dy * dy);
    }

    float distance2D(const float4* a, const float4* b) {
        const float dx = a->x - b->x;
        const float dy = a->y - b->y;
        return sqrtf(dx * dx + dy * dy);
    }

    float length(const float4& vector) {
        return sqrtf(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
    }

    bool rayAabbIntersect(float4* vIntersectionPoint, const float4* vMin, const float4* vMax,
                          const float4* vStart,
                          const float4* vDirection) {
        // Early out if AABB is invalid
        if (vMax->x < vMin->x || vMax->y < vMin->y || vMax->z < vMin->z) {
            return false;
        }

        // Check if ray origin is inside AABB
        if (vStart->x >= vMin->x && vStart->x <= vMax->x &&
            vStart->y >= vMin->y && vStart->y <= vMax->y &&
            vStart->z >= vMin->z && vStart->z <= vMax->z) {
            *vIntersectionPoint = *vStart;
            return true;
        }

        // Calculate intersection times with AABB planes
        float tMinX = (vMin->x - vStart->x) / vDirection->x;
        float tMaxX = (vMax->x - vStart->x) / vDirection->x;
        float tMinY = (vMin->y - vStart->y) / vDirection->y;
        float tMaxY = (vMax->y - vStart->y) / vDirection->y;
        float tMinZ = (vMin->z - vStart->z) / vDirection->z;
        float tMaxZ = (vMax->z - vStart->z) / vDirection->z;

        // Handle division by zero (ray parallel to axis)
        if (vDirection->x == 0.0f) {
            if (vStart->x < vMin->x || vStart->x > vMax->x) {
                return false;
            }
            tMinX = -std::numeric_limits<float>::infinity();
            tMaxX = std::numeric_limits<float>::infinity();
        }
        if (vDirection->y == 0.0f) {
            if (vStart->y < vMin->y || vStart->y > vMax->y) {
                return false;
            }
            tMinY = -std::numeric_limits<float>::infinity();
            tMaxY = std::numeric_limits<float>::infinity();
        }
        if (vDirection->z == 0.0f) {
            if (vStart->z < vMin->z || vStart->z > vMax->z) {
                return false;
            }
            tMinZ = -std::numeric_limits<float>::infinity();
            tMaxZ = std::numeric_limits<float>::infinity();
        }

        // Find the largest tMin and the smallest tMax
        const float t1 = std::max(std::max(tMinX, tMinY), tMinZ);
        const float t2 = std::min(std::min(tMaxX, tMaxY), tMaxZ);

        // Check if there's an intersection
        if (t1 > t2) {
            return false;
        }

        // Calculate intersection point
        *vIntersectionPoint = *vStart + *vDirection * t1;

        return true;
    }

    // Quaternion multiplication
    Quaternion quaternionMultiply(const Quaternion& q1, const Quaternion& q2) {
        Quaternion result{};
        result.w = q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z;
        result.x = q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y;
        result.y = q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x;
        result.z = q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w;
        return result;
    }

    // Quaternion inverse
    Quaternion quaternionInverse(const Quaternion& q) {
        const float normSq = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
        Quaternion result{};
        result.w = q.w / normSq;
        result.x = -q.x / normSq;
        result.y = -q.y / normSq;
        result.z = -q.z / normSq;
        return result;
    }

    // Rotate a point with a quaternion
    Vec3 rotatePoint(const Vec3& point, const Quaternion& quaternion) {
        const Quaternion pointQuat = {point.X, point.Y, point.Z, 0};
        const Quaternion rotatedPointQuat = quaternionMultiply(
            quaternion, quaternionMultiply(pointQuat, quaternionInverse(quaternion)));
        const Vec3 rotatedPoint = {rotatedPointQuat.x, rotatedPointQuat.y, rotatedPointQuat.z};
        return rotatedPoint;
    }
}