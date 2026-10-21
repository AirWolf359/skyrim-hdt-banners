#ifndef BANNER_FORCES_H
#define BANNER_FORCES_H

#include "BannerCollision.h"
#include <LinearMath/btVector3.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hdt {
    inline bool BannerArrowClothHit(const btVector3& origin, const btVector3& delta,
                                    const std::array<btVector3, 3>& triangle, float thickness, float& fraction,
                                    btVector3& weights) {
        float u, v;
        if (BannerSegmentHit(origin, delta, triangle[0], triangle[1], triangle[2], fraction, u, v)) {
            weights = btVector3(1 - u - v, u, v);
            return true;
        }
        const auto closest = BannerClosestCapsuleTriangle({origin, origin + delta, thickness}, triangle);
        if ((closest.cloth - closest.capsule).length2() > thickness * thickness) {
            return false;
        }
        fraction = closest.axisFraction;
        weights = closest.weights;
        return true;
    }

    inline bool BannerArrowBackingReach(const btVector3& pierced, const btVector3& impact, btVector3 direction,
                                        float shaftLength) {
        if (direction.length2() < 0.0001f) {
            return false;
        }
        direction.normalize();
        const auto offset = impact - pierced;
        const float along = offset.dot(direction);
        return along >= -4 && along <= shaftLength + 12 && (offset - direction * along).length2() <= 144;
    }

    inline bool BannerArrowBackingTarget(const btVector3& surface, btVector3 normal, const btVector3& pierced,
                                         btVector3 incoming, float clearance, btVector3& target) {
        if (normal.length2() < 0.0001f || incoming.length2() < 0.0001f) {
            return false;
        }
        normal.normalize();
        incoming.normalize();
        const float side = normal.dot(pierced - surface);
        if (side < -.01f || (std::abs(side) <= .01f && normal.dot(incoming) > 0.00001f)) {
            normal = -normal;
        }
        target = surface + normal * clearance;
        return true;
    }

    inline float BannerArrowImpactMagnitude(float speed) {
        return std::clamp(speed * 0.08f, 120.0f, 400.0f);
    }

    inline bool BannerArrowSurfacePinTarget(const btVector3& start, const btVector3& end, btVector3 normal,
                                            float fraction, float clearance, btVector3& target) {
        if (normal.length2() < 0.0001f || fraction < 0 || fraction > 1) {
            return false;
        }
        normal.normalize();
        if (normal.dot(end - start) > 0) {
            normal = -normal;
        }
        target = start + (end - start) * fraction + normal * clearance;
        return true;
    }

    inline bool BannerArrowShaftContains(const btVector3& point, const btVector3& center, btVector3 direction,
                                         float halfLength) {
        if (direction.length2() < 0.0001f || !std::isfinite(halfLength) || halfLength <= 0) {
            return false;
        }
        direction.normalize();
        const auto offset = point - center;
        const float axial = offset.dot(direction);
        return std::abs(axial) <= halfLength + 8 && (offset - direction * axial).length2() <= 144;
    }

    inline bool ResolveBannerArrowPin(std::array<btVector3, 3>& positions, std::array<btVector3, 3>& velocities,
                                      const std::array<float, 3>& inverseMass, const btVector3& weights,
                                      const btVector3& target, const btVector3& targetVelocity) {
        btVector3 point(0, 0, 0), velocity(0, 0, 0);
        float denominator = 0;
        for (size_t i = 0; i < 3; ++i) {
            point += positions[i] * weights[i];
            velocity += velocities[i] * weights[i];
            denominator += weights[i] * weights[i] * inverseMass[i];
        }
        if (denominator < 0.5f || (target - point).length2() > 300.0f * 300.0f) {
            return false;
        }
        for (size_t i = 0; i < 3; ++i) {
            const float factor = weights[i] * inverseMass[i] / denominator;
            positions[i] += (target - point) * factor;
            velocities[i] += (targetVelocity - velocity) * factor;
        }

        btVector3 mean(0, 0, 0);
        float freeWeight = 0;
        for (size_t i = 0; i < 3; ++i) {
            if (inverseMass[i] > 0) {
                mean += velocities[i] * weights[i];
                freeWeight += weights[i];
            }
        }
        if (freeWeight > 0.0001f) {
            mean /= freeWeight;
            for (size_t i = 0; i < 3; ++i) {
                if (inverseMass[i] > 0) {
                    velocities[i] = mean + (velocities[i] - mean) * 0.1f;
                }
            }
        }
        return true;
    }
}

namespace hdt {
    inline bool BannerRopeHit(const btVector3& origin, const btVector3& delta, const btVector3& a, const btVector3& b,
                              float radius, float& fraction, float& along) {
        if (!std::isfinite(radius) || radius < 0) {
            return false;
        }
        btVector3 weights;
        if (!BannerArrowClothHit(origin, delta, {a, b, b}, radius, fraction, weights)) {
            return false;
        }
        along = std::clamp(float(weights.y() + weights.z()), 0.f, 1.f);
        return true;
    }

    inline std::array<float, 2> BannerRopeImpactWeights(float along, bool mobileA, bool mobileB) {
        std::array<float, 2> weights{mobileA ? 1.f - along : 0.f, mobileB ? along : 0.f};
        const float sum = weights[0] + weights[1];
        if (sum > 0) {
            weights[0] /= sum;
            weights[1] /= sum;
        }
        return weights;
    }
}

namespace hdt {
    inline float BannerMagicFalloff(float distance, float radius) {
        if (!std::isfinite(distance) || !std::isfinite(radius) || radius <= 0 || distance >= radius) {
            return 0;
        }
        const float x = std::clamp(1 - distance / radius, 0.f, 1.f);
        return x * x;
    }

    inline float BannerMagicImpulseBudget(bool voice, bool continuous, float force, float dt) {
        if (!std::isfinite(force)) {
            force = 0;
        }
        const float base = (voice ? 320.f : 180.f) + std::clamp(std::sqrt(std::abs(force)) * 3.f, 0.f, 100.f);
        return continuous ? base * 2.5f * std::clamp(dt, 0.f, .05f) : base;
    }

    inline float BannerMagicIntensity(unsigned tier, float magnitude, float power) {
        if (tier >= 1 && tier <= 3) {
            return tier == 1 ? 1.f : tier == 2 ? 2.f : 4.f;
        }
        const float mag =
            std::isfinite(magnitude) && magnitude > 0 ? std::clamp(std::sqrt(magnitude / 25.f), .5f, 2.5f) : 1.f;
        const float cast = std::isfinite(power) && power > 0 ? std::clamp(std::sqrt(power), .5f, 2.f) : 1.f;
        return std::clamp(mag * cast, .2f, 5.f);
    }

    inline bool BannerMagicDamageHit(uint8_t& hits, float& cooldown, float magnitude = 0, bool continuous = false,
                                     bool devastating = false) {
        devastating = devastating || (std::isfinite(magnitude) && magnitude >= 100.f);
        if ((cooldown > 0 && !devastating) || hits >= 20) {
            return false;
        }
        if (devastating) {
            hits = 20;
            cooldown = 1.f;
            return true;
        }
        const float rate = std::isfinite(magnitude) ? std::clamp(magnitude / 25.f, 1.f, 4.f) : 1.f;
        const unsigned amount = continuous ? 1u
                                : std::isfinite(magnitude)
                                    ? unsigned(std::round(std::clamp(magnitude / 8.f, 1.f, 20.f)))
                                    : 1u;
        hits = uint8_t(std::min(unsigned(hits) + amount, 20u));
        cooldown = continuous ? 1.f / rate : 1.f;
        return true;
    }

    inline btVector3 BannerMagicKick(const btVector3& velocity, btVector3 direction, float speed, float weight) {
        if (!std::isfinite(direction.length2()) || direction.length2() < .0001f || !std::isfinite(speed) ||
            !std::isfinite(weight)) {
            return {0, 0, 0};
        }
        direction.normalize();
        const float add =
            std::min(std::max(0.f, 8000.f - velocity.dot(direction)), std::clamp(speed * weight, 0.f, 6000.f));
        return direction * add;
    }
}

#endif
