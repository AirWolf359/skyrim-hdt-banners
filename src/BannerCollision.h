#ifndef BANNER_COLLISION_H
#define BANNER_COLLISION_H

#include "Utility.h"
#include <LinearMath/btVector3.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <LinearMath/btTransform.h>
#include <cstdint>
#include <RE/H/hkpClosestRayHitCollector.h>
#include <RE/H/hkpCollidable.h>
#include <RE/H/hkpShapeContainer.h>
#include <RE/H/hkpWorldObject.h>
#include <RE/T/TESHavokUtilities.h>

namespace hdt {
    struct BannerActorCapsule {
        btVector3 a, b;
        float radius;
    };

    inline BannerActorCapsule MakeBannerHeadCapsule(const btVector3& head, const btVector3& neck,
                                                    const btVector3& actorUp, float scale) {
        auto up = head - neck;
        if (up.length2() < .01f || up.length2() > 1024.f * scale * scale) {
            up = actorUp;
        }
        if (up.length2() < .01f) {
            up = {0, 0, 1};
        }
        up.normalize();
        return {head - up * (.5f * scale), head + up * (3.5f * scale), 7.5f * scale + .5f};
    }

    inline bool ExpandBannerHeadForEquipment(BannerActorCapsule& capsule, const btVector3& head,
                                             const btVector3& center, float radius, float scale) {
        if (!std::isfinite(radius) || radius < 2.f * scale || radius > 18.f * scale || !std::isfinite(center.x()) ||
            !std::isfinite(center.y()) || !std::isfinite(center.z()) ||
            (center - head).length2() > 576.f * scale * scale) {
            return false;
        }
        auto up = capsule.b - capsule.a;
        if (up.length2() < .001f) {
            return false;
        }
        up.normalize();
        const float height = (center - head).dot(up);
        const auto lateral = center - head - up * height;
        capsule.radius = std::max(capsule.radius, std::min(12.f * scale + .5f, radius + lateral.length() + .5f));
        const float upper = std::clamp(height, -2.f * scale, 6.f * scale);
        if ((capsule.b - head).dot(up) < upper) {
            capsule.b = head + up * upper;
        }
        if ((capsule.a - head).dot(up) > upper) {
            capsule.a = head + up * upper;
        }
        return true;
    }

    inline bool ProjectBannerActorContact(btVector3& position, btVector3& velocity, const BannerActorCapsule& capsule,
                                          const btVector3& surfaceVelocity) {
        const auto axis = capsule.b - capsule.a;
        const auto t =
            axis.length2() > 0.0001f ? btClamped((position - capsule.a).dot(axis) / axis.length2(), 0.0f, 1.0f) : 0.0f;
        const auto center = capsule.a + axis * t;
        auto normal = position - center;
        const auto distance = normal.length();
        if (distance >= capsule.radius || distance < 0.0001f) {
            return false;
        }
        normal /= distance;
        position += normal * (capsule.radius - distance + 0.05f);
        const float closing = (velocity - surfaceVelocity).dot(normal);
        if (closing < 0) {
            velocity -= normal * closing;
        }
        return true;
    }
}

namespace hdt {
    struct BannerTriangleContact {
        btVector3 cloth, capsule, weights;
        float axisFraction = 0;
    };

    inline btVector3 BannerClosestTriangle(const btVector3& p, const std::array<btVector3, 3>& tri) {
        const auto ab = tri[1] - tri[0], ac = tri[2] - tri[0], ap = p - tri[0];
        if (ab.cross(ac).length2() < 1e-10f) {
            btVector3 best(1, 0, 0);
            float distance = (p - tri[0]).length2();
            for (int edge = 0; edge < 3; ++edge) {
                const int next = (edge + 1) % 3;
                const auto delta = tri[next] - tri[edge];
                const float fraction =
                    delta.length2() > 1e-10f ? std::clamp((p - tri[edge]).dot(delta) / delta.length2(), 0.0f, 1.0f) : 0;
                const float squared = (p - tri[edge] - delta * fraction).length2();
                if (squared < distance) {
                    distance = squared;
                    best.setZero();
                    best[edge] = 1 - fraction;
                    best[next] = fraction;
                }
            }
            return best;
        }
        const float d1 = ab.dot(ap), d2 = ac.dot(ap);
        if (d1 <= 0 && d2 <= 0) {
            return {1, 0, 0};
        }
        const auto bp = p - tri[1];
        const float d3 = ab.dot(bp), d4 = ac.dot(bp);
        if (d3 >= 0 && d4 <= d3) {
            return {0, 1, 0};
        }
        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0 && d1 >= 0 && d3 <= 0) {
            const float v = d1 / (d1 - d3);
            return {1 - v, v, 0};
        }
        const auto cp = p - tri[2];
        const float d5 = ab.dot(cp), d6 = ac.dot(cp);
        if (d6 >= 0 && d5 <= d6) {
            return {0, 0, 1};
        }
        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0 && d2 >= 0 && d6 <= 0) {
            const float w = d2 / (d2 - d6);
            return {1 - w, 0, w};
        }
        const float va = d3 * d6 - d5 * d4;
        if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
            const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return {0, 1 - w, w};
        }
        const float sum = va + vb + vc;
        if (std::abs(sum) < 1e-10f) {
            return {1, 0, 0};
        }
        return {va / sum, vb / sum, vc / sum};
    }

    inline btVector3 BannerTrianglePoint(const std::array<btVector3, 3>& tri, const btVector3& weights) {
        return tri[0] * weights.x() + tri[1] * weights.y() + tri[2] * weights.z();
    }

    inline BannerTriangleContact BannerClosestCapsuleTriangle(const BannerActorCapsule& cap,
                                                              const std::array<btVector3, 3>& tri) {
        const auto axis = cap.b - cap.a;
        float fraction, u, v;
        if (BannerSegmentHit(cap.a, axis, tri[0], tri[1], tri[2], fraction, u, v)) {
            const auto p = cap.a + axis * fraction;
            return {p, p, btVector3(1 - u - v, u, v), fraction};
        }
        auto weights = BannerClosestTriangle(cap.a, tri);
        BannerTriangleContact best{BannerTrianglePoint(tri, weights), cap.a, weights, 0};
        float bestDistance = (best.cloth - best.capsule).length2();
        auto consider = [&](const btVector3& w, float s) {
            const auto cloth = BannerTrianglePoint(tri, w), capsule = cap.a + axis * s;
            const float distance = (cloth - capsule).length2();
            if (distance < bestDistance) {
                bestDistance = distance;
                best = {cloth, capsule, w, s};
            }
        };
        consider(BannerClosestTriangle(cap.b, tri), 1);
        for (int edge = 0; edge < 3; ++edge) {
            const int next = (edge + 1) % 3;
            const auto e = tri[next] - tri[edge], r = cap.a - tri[edge];
            const float a = axis.length2(), b = axis.dot(e), c = e.length2(), d = axis.dot(r), f = e.dot(r);
            float s = 0, t = 0;
            if (a < 1e-8f) {
                t = c > 1e-8f ? std::clamp(f / c, 0.0f, 1.0f) : 0;
            } else if (c < 1e-8f) {
                s = std::clamp(-d / a, 0.0f, 1.0f);
            } else {
                const float denominator = a * c - b * b;
                s = denominator > 1e-8f ? std::clamp((b * f - c * d) / denominator, 0.0f, 1.0f) : 0;
                t = (b * s + f) / c;
                if (t < 0) {
                    t = 0;
                    s = std::clamp(-d / a, 0.0f, 1.0f);
                } else if (t > 1) {
                    t = 1;
                    s = std::clamp((b - d) / a, 0.0f, 1.0f);
                }
            }
            btVector3 w(0, 0, 0);
            w[edge] = 1 - t;
            w[next] = t;
            consider(w, s);
        }
        return best;
    }

    struct BannerSweptActorContact {
        btVector3 weights, normal;
        float axisFraction = 0, fraction = 0;
    };

    inline bool SweepBannerActorTriangle(const BannerActorCapsule& oldCap, const BannerActorCapsule& cap,
                                         const std::array<btVector3, 3>& oldTri, const std::array<btVector3, 3>& tri,
                                         BannerSweptActorContact& result) {
        const float radius = std::max(oldCap.radius, cap.radius);
        for (int axis = 0; axis < 3; ++axis) {
            float tmin = tri[0][axis], tmax = tmin;
            for (int i = 0; i < 3; ++i) {
                tmin = std::min({tmin, tri[i][axis], oldTri[i][axis]});
                tmax = std::max({tmax, tri[i][axis], oldTri[i][axis]});
            }
            const float cmin = std::min({oldCap.a[axis], oldCap.b[axis], cap.a[axis], cap.b[axis]}) - radius;
            const float cmax = std::max({oldCap.a[axis], oldCap.b[axis], cap.a[axis], cap.b[axis]}) + radius;
            if (tmin > cmax || tmax < cmin) {
                return false;
            }
        }
        float clothMotion = 0;
        for (int i = 0; i < 3; ++i) {
            clothMotion = std::max(clothMotion, (tri[i] - oldTri[i]).length());
        }
        const float speed = clothMotion + std::max((cap.a - oldCap.a).length(), (cap.b - oldCap.b).length());
        float time = 0;
        for (int iteration = 0; iteration < 160; ++iteration) {
            std::array<btVector3, 3> moving;
            for (int i = 0; i < 3; ++i) {
                moving[i] = oldTri[i].lerp(tri[i], time);
            }
            const BannerActorCapsule movingCap{oldCap.a.lerp(cap.a, time), oldCap.b.lerp(cap.b, time), radius};
            const auto closest = BannerClosestCapsuleTriangle(movingCap, moving);
            auto normal = closest.cloth - closest.capsule;
            const float distance = normal.length();
            if (distance <= radius + 0.01f) {
                if (distance > 0.0001f) {
                    normal /= distance;
                } else {
                    normal = (moving[1] - moving[0]).cross(moving[2] - moving[0]);
                    if (normal.length2() < 1e-8f) {
                        return false;
                    }
                    normal.normalize();
                    const auto relative = (cap.a - oldCap.a).lerp(cap.b - oldCap.b, closest.axisFraction) -
                                          BannerTrianglePoint(tri, closest.weights) +
                                          BannerTrianglePoint(oldTri, closest.weights);
                    if (normal.dot(relative) < 0) {
                        normal = -normal;
                    }
                }
                result = {closest.weights, normal, closest.axisFraction, time};
                return true;
            }
            if (speed < 1e-8f) {
                return false;
            }
            time += (distance - radius) / speed;
            if (time > 1) {
                return false;
            }
        }
        return false;
    }

    inline float BannerActorSeparation(const BannerActorCapsule& cap, const std::array<btVector3, 3>& tri,
                                       const BannerSweptActorContact& contact) {
        const auto center = cap.a.lerp(cap.b, contact.axisFraction);
        return std::max(0.0f,
                        cap.radius + 0.05f - contact.normal.dot(BannerTrianglePoint(tri, contact.weights) - center));
    }

    inline bool ResolveBannerActorTriangle(const BannerActorCapsule& before, const BannerActorCapsule& current,
                                           const std::array<btVector3, 3>& start, std::array<btVector3, 3>& end,
                                           std::array<btVector3, 3>& velocities,
                                           const std::array<float, 3>& inverseMass, float elapsed,
                                           bool releaseSlidingContact = true, btVector3* resolvedWeights = nullptr) {
        BannerSweptActorContact contact;
        if (!SweepBannerActorTriangle(before, current, start, end, contact)) {
            return false;
        }
        if (releaseSlidingContact) {
            const auto relativeMotion = (current.a - before.a).lerp(current.b - before.b, contact.axisFraction) -
                                        BannerTrianglePoint(end, contact.weights) +
                                        BannerTrianglePoint(start, contact.weights);
            const auto point = BannerTrianglePoint(end, contact.weights), axis = current.b - current.a;
            const float along =
                axis.length2() > 0.0001f ? std::clamp((point - current.a).dot(axis) / axis.length2(), 0.0f, 1.0f) : 0;
            const auto offset = point - current.a - axis * along;
            const auto tangent = offset - contact.normal * offset.dot(contact.normal);
            const bool separating = relativeMotion.dot(contact.normal) <= 0.01f;
            const bool leftSurface = tangent.length2() > current.radius * current.radius;
            if (separating || leftSurface) {
                const auto closest = BannerClosestCapsuleTriangle(current, end);
                auto normal = closest.cloth - closest.capsule;
                const float distance = normal.length();
                if (distance >= current.radius) {
                    return false;
                }
                if (distance > 0.0001f) {
                    normal /= distance;
                } else {
                    normal = contact.normal;
                }
                contact.weights = closest.weights;
                contact.axisFraction = closest.axisFraction;
                contact.normal = normal;
            }
        }
        float separation = BannerActorSeparation(current, end, contact);
        if (separation <= 0) {
            const auto closest = BannerClosestCapsuleTriangle(current, end);
            auto normal = closest.cloth - closest.capsule;
            const float distance = normal.length();
            if (distance >= current.radius) {
                return false;
            }
            if (distance > 0.0001f) {
                normal /= distance;
                if (normal.dot(contact.normal) < 0) {
                    normal = -normal;
                }
            } else {
                normal = contact.normal;
            }
            contact.weights = closest.weights;
            contact.axisFraction = closest.axisFraction;
            contact.normal = normal;
            separation = BannerActorSeparation(current, end, contact);
            if (separation <= 0) {
                return false;
            }
        }
        float denominator = 0;
        btVector3 clothVelocity(0, 0, 0);
        for (size_t j = 0; j < 3; ++j) {
            denominator += contact.weights[j] * contact.weights[j] * inverseMass[j];
            clothVelocity += velocities[j] * contact.weights[j];
        }
        if (denominator < 0.5f) {
            return false;
        }
        auto actorVelocity =
            (current.a - before.a).lerp(current.b - before.b, contact.axisFraction) / std::max(elapsed, 0.001f);
        if (actorVelocity.length2() > 4000000) {
            actorVelocity *= 2000 / actorVelocity.length();
        }
        const float closing = std::min(0.0f, (clothVelocity - actorVelocity).dot(contact.normal));
        if (resolvedWeights) {
            *resolvedWeights = contact.weights;
        }
        for (size_t j = 0; j < 3; ++j) {
            const float weight = contact.weights[j] * inverseMass[j] / denominator;
            end[j] += contact.normal * (separation * weight);
            velocities[j] -= contact.normal * (closing * weight);
        }
        return true;
    }
}

namespace hdt {
    struct BannerActorShape {
        BannerActorCapsule capsule;
        std::uintptr_t identity;
    };

    inline bool BannerActorCanSweep(const BannerActorShape& before, const BannerActorShape& current) {
        return before.identity == current.identity &&
               std::max((before.capsule.a - current.capsule.a).length2(),
                        (before.capsule.b - current.capsule.b).length2()) < 2048.0f * 2048.0f;
    }

    inline bool MakeBannerWeaponCapsule(const btVector3& minimum, const btVector3& maximum, const btTransform& world,
                                        float scale, BannerActorCapsule& capsule) {
        const auto extent = maximum - minimum;
        if (!std::isfinite(scale) || scale <= 0 || extent.x() < 0 || extent.y() < 0 || extent.z() < 0) {
            return false;
        }
        int axis = 0;
        if (extent.y() > extent[axis]) {
            axis = 1;
        }
        if (extent.z() > extent[axis]) {
            axis = 2;
        }
        if (extent[axis] < 2 || extent[axis] * scale > 600) {
            return false;
        }
        const float widthA = extent[(axis + 1) % 3] * 0.5f, widthB = extent[(axis + 2) % 3] * 0.5f;
        const float radius =
            std::min(extent[axis] * 0.5f, std::clamp(std::sqrt(widthA * widthA + widthB * widthB), 1.5f, 8.0f));
        auto a = (minimum + maximum) * 0.5f, b = a;
        a[axis] = minimum[axis] + radius;
        b[axis] = maximum[axis] - radius;
        capsule = {world * (a * scale), world * (b * scale), radius * scale + 0.5f};
        return true;
    }
}

namespace hdt {
    inline bool BannerRecoveryFrame(const btVector3& topLeft, const btVector3& topRight, const btVector3& bottomLeft,
                                    btVector3& normal, btVector3& down) {
        down = bottomLeft - topLeft;
        normal = (topRight - topLeft).cross(down);
        if (normal.length2() < .0001f || down.length2() < .0001f) {
            return false;
        }
        normal.normalize();
        down.normalize();
        return true;
    }

    inline bool BannerRecoveryCorrectionAllowed(float distanceSquared, float scale) {
        const float limit = std::max(.5f, 4.f * scale);
        return distanceSquared >= 0 && distanceSquared <= limit * limit;
    }

    struct BannerWallSupport {
        btVector3 point;
        btVector3 normal;
        float age = 0;
        bool active = false;
    };

    inline bool BannerWallSupportValid(const btVector3& position, const BannerWallSupport& wall, float margin,
                                       float scale) {
        const auto displacement = position - wall.point;
        const float gap = wall.normal.dot(displacement);
        const float reach = std::max(.5f, 6.f * scale), depth = std::max(.5f, 4.f * scale);
        return wall.age <= .35f && gap >= -depth && gap <= margin + 1.f &&
               (displacement - wall.normal * gap).length2() <= reach * reach;
    }

    struct BannerWorldContact {
        btVector3 position;
        btVector3 velocity;
        btVector3 surface;
        btVector3 normal;
    };

    inline bool BannerEnclosedRecoveryAllowed(const btVector3& position, const BannerWorldContact& a,
                                              const BannerWorldContact& b, bool sameSolid, float scale) {
        const float tolerance = std::max(.05f, .1f * scale);
        const float limit = std::max(.5f, 12.f * scale);
        return sameSolid && a.normal.dot(b.normal) < -.5f && a.normal.dot(position - a.surface) < -tolerance &&
               b.normal.dot(position - b.surface) < -tolerance &&
               std::min(a.position.distance2(position), b.position.distance2(position)) <= limit * limit;
    }

    inline bool BannerWorldEntryHit(const btVector3& start, const btVector3& end, const btVector3& normal) {
        const auto motion = end - start;
        return motion.length2() > .000001f && normal.length2() > .5f && normal.dot(motion) <= 0;
    }

    inline BannerWorldContact HoldBannerWallContact(btVector3 position, btVector3 normal, const btVector3& surface,
                                                    float margin, btVector3 velocity) {
        normal.normalize();
        const float penetration = margin - normal.dot(position - surface);
        if (penetration > 0) {
            position += normal * penetration;
        }
        const float normalVelocity = velocity.dot(normal);
        if (penetration >= 0 && normalVelocity < 0) {
            velocity -= normal * normalVelocity;
        }
        return {position, velocity, surface, normal};
    }

    inline BannerWorldContact ProjectBannerWorldContact(const btVector3& start, const btVector3& end,
                                                        const btVector3& position, btVector3 normal, float hitFraction,
                                                        float margin, btVector3 velocity) {
        normal.normalize();
        if (normal.dot(end - start) > 0) {
            normal = -normal;
        }
        const auto surface = start + (end - start) * hitFraction;
        return HoldBannerWallContact(position, normal, surface, margin, velocity);
    }
}

namespace hdt {
    inline bool BannerFaceRecoveryVisit(unsigned sequence) {
        return (sequence / 112) % 4 == 3;
    }

    struct BannerFaceSupport {
        BannerWallSupport wall;
        btVector3 weights{1.f / 3, 1.f / 3, 1.f / 3};
    };

    inline bool BannerFaceConflictsWithNode(const btVector3& before, const btVector3& after,
                                            const btVector3& faceNormal, const BannerWallSupport& node, float margin,
                                            float scale) {
        if (!node.active || faceNormal.dot(node.normal) >= -.5f ||
            !BannerWallSupportValid(before, node, margin, scale)) {
            return false;
        }
        const float oldGap = node.normal.dot(before - node.point), newGap = node.normal.dot(after - node.point);
        return newGap < margin - .01f && newGap < oldGap - .01f;
    }

    inline bool ResolveBannerFacePlane(std::array<btVector3, 3>& positions, std::array<btVector3, 3>& velocities,
                                       const std::array<float, 3>& inverseMass, const btVector3& weights,
                                       const btVector3& surface, const btVector3& normal, float margin) {
        btVector3 point(0, 0, 0), velocity(0, 0, 0);
        float denominator = 0;
        for (int i = 0; i < 3; ++i) {
            point += positions[i] * weights[i];
            velocity += velocities[i] * weights[i];
            denominator += inverseMass[i] * weights[i] * weights[i];
        }
        if (denominator < .000001f) {
            return false;
        }
        const auto held = HoldBannerWallContact(point, normal, surface, margin, velocity);
        const auto correction = held.position - point, speed = held.velocity - velocity;
        if (correction.length2() < .000001f && speed.length2() < .000001f) {
            return false;
        }
        for (int i = 0; i < 3; ++i) {
            const float factor = inverseMass[i] * weights[i] / denominator;
            positions[i] += correction * factor;
            velocities[i] += speed * factor;
        }
        return true;
    }

}

namespace hdt {
    inline bool LimitBannerFabricLength(btVector3& a, btVector3& b, btVector3& va, btVector3& vb, float inverseMassA,
                                        float inverseMassB, float maxLength) {
        const float sum = inverseMassA + inverseMassB;
        if (sum <= 0 || maxLength <= 0) {
            return false;
        }
        const auto delta = b - a;
        const float length = delta.length();
        if (length <= maxLength || length < .0001f) {
            return false;
        }
        const auto direction = delta / length;
        const auto correction = direction * (length - maxLength);
        a += correction * (inverseMassA / sum);
        b -= correction * (inverseMassB / sum);
        const float separating = (vb - va).dot(direction);
        if (separating > 0) {
            const auto speed = direction * separating;
            va += speed * (inverseMassA / sum);
            vb -= speed * (inverseMassB / sum);
        }
        return true;
    }
}

namespace BP {
    struct BannerSweepHistory {
        btVector3 checked{0, 0, 0};
        bool initialized = false;

        bool due(const btVector3& position, uint64_t frame, uint64_t phase, bool contact) {
            if (!initialized) {
                checked = position;
                initialized = true;
                return false;
            }
            return contact || ((frame + phase) & 1) == 0;
        }

        void commit(const btVector3& position) {
            checked = position;
            initialized = true;
        }
    };
}

namespace BP {
    class BannerVisibilityCache {
        std::unordered_map<const RE::hkpCollidable*, bool> values;

    public:
        void clear() {
            values.clear();
        }

        template <class Query> bool accepts(const RE::hkpCollidable& root, Query query) {
            if (auto it = values.find(&root); it != values.end()) {
                return it->second;
            }
            const bool result = query(root);
            if (values.size() < 1024) {
                values.emplace(&root, result);
            }
            return result;
        }
    };

    inline thread_local BannerVisibilityCache bannerVisibilityCache;

    inline void BeginBannerWorldQueries() {
        bannerVisibilityCache.clear();
    }

    inline bool SolidBannerLayer(RE::COL_LAYER layer) {
        using L = RE::COL_LAYER;
        switch (layer) {
            case L::kStatic:
            case L::kAnimStatic:
            case L::kTransparent:
            case L::kClutter:
            case L::kWeapon:
            case L::kTrees:
            case L::kProps:
            case L::kTerrain:
            case L::kTrap:
            case L::kGround:
            case L::kDebrisSmall:
            case L::kDebrisLarge:
            case L::kTransparentSmall:
            case L::kTransparentSmallAnim:
                return true;
            default:
                return false;
        }
    }

    inline bool SolidBannerFilter(const RE::CFilter& filter) {
        return !(filter.filter & RE::CFilter::Flags::kNoCollision) &&
               SolidBannerLayer(static_cast<RE::COL_LAYER>(filter.filter & RE::CFilter::Flags::kLayerMask));
    }

    inline bool SolidBannerRoot(const RE::hkpCollidable& root) {
        return root.broadPhaseHandle.type == static_cast<int>(RE::hkpWorldObject::BroadPhaseType::kEntity) &&
               SolidBannerFilter(root.broadPhaseHandle.collisionFilterInfo);
    }

    struct AcceptBannerCollision {
        bool operator()(const RE::hkpCollidable&) const {
            return true;
        }
    };

    template <class Query, class Accept = AcceptBannerCollision>
    bool PickBannerSolidQuery(RE::bhkPickData& pick, Query query, Accept accept = {}) {
        const auto from = pick.rayInput.from;
        const auto originalRay = pick.ray;
        const auto delta = pick.rayInput.to - from;
        alignas(16) float d[4];
        _mm_store_ps(d, delta.quad);
        const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float epsilon = std::clamp(.0001f / std::max(length, .0001f), .000001f, .001f);
        float offset = 0;
        auto restore = [&]() {
            pick.rayInput.from = from;
            pick.ray = originalRay;
        };
        for (int skipped = 0; skipped < 8; ++skipped) {
            pick.rayOutput.Reset();
            query(pick);
            if (!pick.rayOutput.HasHit()) {
                restore();
                return false;
            }
            const float hit = offset + (1 - offset) * pick.rayOutput.hitFraction;
            if (SolidBannerRoot(*pick.rayOutput.rootCollidable) && accept(*pick.rayOutput.rootCollidable)) {
                pick.rayOutput.hitFraction = hit;
                restore();
                return true;
            }
            offset = std::max(hit + epsilon, offset + epsilon);
            if (offset >= 1) {
                break;
            }
            pick.rayInput.from = from + delta * RE::hkVector4(offset);
            pick.ray = pick.rayInput.to - pick.rayInput.from;
        }
        pick.rayOutput.Reset();
        restore();
        return false;
    }

    inline bool PickBannerSolid(RE::bhkWorld* world, RE::bhkPickData& pick) {
        return PickBannerSolidQuery(
            pick,
            [&](RE::bhkPickData& ray) {
                world->PickObject(ray);
            },
            [](const RE::hkpCollidable& root) {
                return bannerVisibilityCache.accepts(root, [](const RE::hkpCollidable& candidate) {
                    auto* ref = RE::TESHavokUtilities::FindCollidableRef(candidate);
                    auto* object = ref ? ref->Get3D() : nullptr;
                    for (auto* node = object; node; node = node->parent) {
                        if (node->GetAppCulled()) {
                            return false;
                        }
                    }
                    return true;
                });
            });
    }
}

#endif // BANNER_COLLISION_H
