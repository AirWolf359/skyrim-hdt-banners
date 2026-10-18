#include "BannerForces.h"
#include "BannerRuntime.h"

namespace hdt {
    void BannerRuntime::SyncBannerArrows(std::unordered_map<RE::FormID, BannerArrowState>& m_bannerArrows,
                                         const std::array<SkinnedMeshBone*, 88>& cachedBones,
                                         const std::vector<RE::ProjectileHandle>& flying) {
        if (!m_bannerSystem || m_loading) {
            m_bannerArrows.clear();
            return;
        }
        const auto owner = m_bannerReference.get();
        if (!owner) {
            m_bannerArrows.clear();
            return;
        }

        auto* ui = RE::UI::GetSingleton();
        if (!ui || ui->GameIsPaused()) {
            return;
        }
        auto* manager = RE::Projectile::Manager::GetSingleton();
        if (!manager) {
            return;
        }
        if (flying.empty() && m_bannerArrows.empty()) {
            return;
        }
        auto handles = flying;
        for (const auto& [id, state] : m_bannerArrows) {
            if (state.hit && (state.pinned || state.ageAfterHit < 6.0f)) {
                handles.push_back(state.projectile);
            }
        }
        std::unordered_map<RE::FormID, BannerArrowState> next;
        std::array<SkinnedMeshBone*, 75> grid{};
        std::copy_n(cachedBones.begin(), 75, grid.begin());
        btVector3 minimum(BT_LARGE_FLOAT, BT_LARGE_FLOAT, BT_LARGE_FLOAT),
            maximum(-BT_LARGE_FLOAT, -BT_LARGE_FLOAT, -BT_LARGE_FLOAT);
        bool hasGrid = false;
        for (auto* bone : cachedBones) {
            if (bone) {
                const auto p = bone->m_rig.getWorldTransform().getOrigin();
                minimum.setMin(p);
                maximum.setMax(p);
                hasGrid = true;
            }
        }
        if (!hasGrid) {
            m_bannerArrows.clear();
            return;
        }
        const auto clothCenter = (minimum + maximum) * 0.5f;
        const float clothRadius = (maximum - minimum).length() * 0.5f + 16;
        const float dt = std::clamp(RE::GetSecondsSinceLastFrame(), 0.0f, 0.05f);
        for (auto handle : handles) {
            auto projectile = handle.get();
            auto* arrow = projectile ? projectile->As<RE::ArrowProjectile>() : nullptr;
            if (!arrow || arrow->IsDisabled() || arrow->IsDeleted() ||
                arrow->GetProjectileRuntimeData().flags.any(RE::Projectile::Flags::kDestroyed,
                                                            RE::Projectile::Flags::kFading)) {
                continue;
            }
            const auto id = projectile->GetFormID();
            if (next.contains(id)) {
                continue;
            }
            const auto end = projectile->GetPosition();
            const auto velocity = projectile->GetProjectileRuntimeData().linearVelocity;
            auto prior = m_bannerArrows.find(id);
            BannerArrowState state;
            const bool hasPrior =
                prior != m_bannerArrows.end() && prior->second.projectile.get().get() == projectile.get();
            if (hasPrior) {
                state = prior->second;
            }
            state.position = end;
            state.projectile = handle;
            if (state.ropeHit) {
                state.ageAfterHit += dt;
                next.emplace(id, state);
                continue;
            }

            const auto start = hasPrior ? prior->second.position : end - velocity * dt;
            btVector3 origin = convertNi(start), delta = convertNi(end - start);
            const float nearFraction = delta.length2() > 0.001f
                                           ? std::clamp((clothCenter - origin).dot(delta) / delta.length2(), 0.0f, 1.0f)
                                           : 0;
            if (!state.hit &&
                (origin + delta * nearFraction - clothCenter).length2() > (clothRadius + 256) * (clothRadius + 256)) {
                next.emplace(id, state);
                continue;
            }
            bool shaftOnly = false;
            btVector3 impactPoint(0, 0, 0), impactDirection = convertNi(velocity);
            bool solidImpact = false;
            const auto impactResult = arrow->GetMissileRuntimeData().impactResult;
            for (const auto* impact : arrow->GetProjectileRuntimeData().impacts) {
                if (!impact) {
                    continue;
                }
                const auto target = impact->collidee.get();
                if (target && target->As<RE::Actor>()) {
                    continue;
                }
                if (impact->impactResult != RE::ImpactResult::kStick && impactResult != RE::ImpactResult::kStick &&
                    impact->impactResult != RE::ImpactResult::kImpale) {
                    continue;
                }
                const auto candidate = convertNi(impact->desiredTargetLoc);
                if (!std::isfinite(candidate.x()) || !std::isfinite(candidate.y()) || !std::isfinite(candidate.z()) ||
                    (candidate - convertNi(end)).length2() > 256.0f * 256.0f) {
                    continue;
                }
                impactPoint = candidate;
                solidImpact = true;
                if (convertNi(impact->negativeVelocity).length2() > 0.0001f) {
                    impactDirection = -convertNi(impact->negativeVelocity);
                }
                break;
            }
            const bool alreadyEmbedded =
                (impactResult == RE::ImpactResult::kStick ||
                 (solidImpact && impactResult == RE::ImpactResult::kImpale)) &&
                (arrow->GetProjectileRuntimeData().flags.all(RE::Projectile::Flags::kProcessedImpacts) || solidImpact);
            auto* arrowModel = arrow->Get3D();
            btVector3 shaftCenter = arrowModel ? convertNi(arrowModel->worldBound.center) : convertNi(end);
            float halfShaftLength = 40.0f;
            float bestBound = 0;
            auto inspectBounds = [&](auto&& self, RE::NiAVObject* object) -> void {
                if (!object || object->GetAppCulled()) {
                    return;
                }
                if (auto* shape = object->AsTriShape()) {
                    const auto& bound = shape->GetModelData().modelBound;
                    const float radius = bound.radius * object->world.scale;
                    if (radius > bestBound && radius > 4 && radius < 128) {
                        bestBound = radius;
                        shaftCenter = convertNi(object->world.translate +
                                                object->world.rotate * (bound.center * object->world.scale));
                        halfShaftLength = radius;
                    }
                }
                if (auto* node = object->AsNode()) {
                    for (const auto& child : node->GetChildren()) {
                        self(self, child.get());
                    }
                }
            };
            if (arrowModel) {
                inspectBounds(inspectBounds, arrowModel);
            }
            if (bestBound == 0 && arrowModel && arrowModel->worldBound.radius > 4 &&
                arrowModel->worldBound.radius < 128 && (shaftCenter - convertNi(end)).length2() < 256.0f * 256.0f) {
                halfShaftLength = arrowModel->worldBound.radius;
            } else if (bestBound == 0) {
                shaftCenter = solidImpact && impactDirection.length2() > 0.0001f
                                  ? impactPoint - impactDirection.normalized() * halfShaftLength
                                  : convertNi(end);
            }
            if (!state.hit && alreadyEmbedded && (!hasPrior || delta.length2() < 0.001f)) {
                btVector3 flightDirection = impactDirection;
                if (flightDirection.length2() < 0.0001f) {
                    flightDirection = state.direction;
                }
                if (arrowModel && flightDirection.length2() > 0.0001f) {
                    flightDirection.normalize();
                    origin = shaftCenter - flightDirection * halfShaftLength;
                    delta = flightDirection * (2 * halfShaftLength);
                    shaftOnly = true;
                }
            }
            const auto hanger = clothCenter;
            const float fraction = delta.length2() > 0.001f
                                       ? std::clamp((hanger - origin).dot(delta) / delta.length2(), 0.0f, 1.0f)
                                       : 0.0f;
            if (!state.hit && (origin + delta * fraction - hanger).length2() > clothRadius * clothRadius) {
                next.emplace(id, state);
                continue;
            }
            float best = 2.0f;
            SkinnedMeshBone* hitBones[3]{};
            float hitWeights[3]{};
            if (!state.hit && delta.length2() > 0.001f) {
                for (int r = 0; r < 14; ++r) {
                    for (int c = 0; c < 4; ++c) {
                        SkinnedMeshBone* quad[4]{};
                        const int rows[4]{r, r, r + 1, r + 1}, cols[4]{c, c + 1, c, c + 1};
                        for (int i = 0; i < 4; ++i) {
                            quad[i] = grid[rows[i] * 5 + cols[i]];
                        }
                        if (!quad[0] || !quad[1] || !quad[2] || !quad[3]) {
                            continue;
                        }
                        const int triangles[2][3]{{0, 2, 1}, {1, 2, 3}};
                        for (const auto& tri : triangles) {
                            const std::array<btVector3, 3> surface{quad[tri[0]]->m_rig.getWorldTransform().getOrigin(),
                                                                   quad[tri[1]]->m_rig.getWorldTransform().getOrigin(),
                                                                   quad[tri[2]]->m_rig.getWorldTransform().getOrigin()};
                            float distance;
                            btVector3 weights;
                            if (!BannerArrowClothHit(origin, delta, surface,
                                                     std::max(1.0f, 3.0f * m_bannerRoot->world.scale), distance,
                                                     weights) ||
                                distance >= best) {
                                continue;
                            }
                            if (m_bannerDamage.contains(owner->GetFormID()) &&
                                m_bannerDamage.at(owner->GetFormID()).applied) {
                                std::array<uint8_t, 3> ids{};
                                for (int j = 0; j < 3; ++j) {
                                    ids[j] = static_cast<uint8_t>(rows[tri[j]] * 5 + cols[tri[j]]);
                                }
                                const auto& damaged = m_bannerDamage.at(owner->GetFormID());
                                if (!BannerDamageTrianglePointPresent(
                                        ids, {weights.x(), weights.y(), weights.z()},
                                        damaged.coverage[std::max(1, damaged.appliedStage) - 1])) {
                                    continue;
                                }
                            }
                            best = distance;
                            for (int i = 0; i < 3; ++i) {
                                hitBones[i] = quad[tri[i]];
                                hitWeights[i] = weights[i];
                            }
                        }
                    }
                }
                unsigned ropeSegment = 88;
                float ropeAlong = 0;
                for (unsigned i = 75; i < 87; ++i) {
                    if (cachedBones[i] && cachedBones[i + 1]) {
                        float at, along;
                        if (BannerRopeHit(origin, delta, cachedBones[i]->m_rig.getWorldTransform().getOrigin(),
                                          cachedBones[i + 1]->m_rig.getWorldTransform().getOrigin(),
                                          std::max(1.f, 2.f * m_bannerRoot->world.scale), at, along) &&
                            at < best) {
                            best = at;
                            ropeSegment = i;
                            ropeAlong = along;
                        }
                    }
                }
                if (ropeSegment < 87 && !shaftOnly) {
                    auto& a = cachedBones[ropeSegment]->m_rig;
                    auto& b = cachedBones[ropeSegment + 1]->m_rig;
                    const auto weights = BannerRopeImpactWeights(ropeAlong, !a.isStaticOrKinematicObject(),
                                                                 !b.isStaticOrKinematicObject());
                    const float impulse = BannerArrowImpactMagnitude(convertNi(velocity).length());
                    for (unsigned j = 0; j < 2; ++j) {
                        auto& rig = cachedBones[ropeSegment + j]->m_rig;
                        if (weights[j] <= 0 || rig.isStaticOrKinematicObject()) {
                            continue;
                        }
                        const auto kick =
                            BannerMagicKick(rig.getLinearVelocity(), delta, impulse * rig.getInvMass(), weights[j]);
                        rig.activate(true);
                        rig.applyCentralImpulse(kick / rig.getInvMass());
                    }
                    state.hit = true;
                    state.ropeHit = true;
                    state.ageAfterHit = 0;

                    next.emplace(id, state);
                    continue;
                }
                if (best <= 1 && ropeSegment == 88) {
                    const auto impulse =
                        shaftOnly ? btVector3(0, 0, 0)
                                  : delta.normalized() * BannerArrowImpactMagnitude(convertNi(velocity).length());
                    btVector3 contact(0, 0, 0);
                    for (int i = 0; i < 3; ++i) {
                        contact += hitBones[i]->m_rig.getWorldTransform().getOrigin() * hitWeights[i];
                    }
                    std::array<float, 75> patchWeights{};
                    float totalWeight = 0;
                    for (size_t i = 0; i < grid.size(); ++i) {
                        if (!grid[i] || grid[i]->m_rig.isStaticOrKinematicObject()) {
                            continue;
                        }
                        const float radius2 = (grid[i]->m_rig.getWorldTransform().getOrigin() - contact).length2();
                        if (radius2 >= 120.0f * 120.0f) {
                            continue;
                        }
                        patchWeights[i] = 1 - radius2 / (120.0f * 120.0f);
                        totalWeight += patchWeights[i];
                    }
                    if (totalWeight > 0) {
                        for (size_t i = 0; i < grid.size(); ++i) {
                            if (patchWeights[i] > 0) {
                                grid[i]->m_rig.applyCentralImpulse(impulse * (patchWeights[i] / totalWeight));
                            }
                        }
                    } else {
                        for (int i = 0; i < 3; ++i) {
                            if (!hitBones[i]->m_rig.isStaticOrKinematicObject()) {
                                hitBones[i]->m_rig.applyCentralImpulse(impulse * hitWeights[i]);
                            }
                        }
                    }
                    state.hit = true;
                    state.ageAfterHit = 0;
                    state.piercedPoint = contact;
                    state.direction = delta.normalized();
                    state.weights = btVector3(hitWeights[0], hitWeights[1], hitWeights[2]);
                    for (size_t j = 0; j < 3; ++j) {
                        for (size_t k = 0; k < grid.size(); ++k) {
                            if (grid[k] == hitBones[j]) {
                                state.piercedBones[j] = static_cast<uint8_t>(k);
                            }
                        }
                    }
                }
            }
            if (state.hit) {
                state.ageAfterHit += std::clamp(RE::GetSecondsSinceLastFrame(), 0.0f, 0.1f);
                auto* model = arrow->Get3D();
                if (!alreadyEmbedded || !model || model->world.scale <= 0) {
                    state.pinned = false;
                } else if (!state.pinned && state.ageAfterHit <= 6.0f) {
                    auto incoming =
                        impactDirection.length2() > 0.0001f ? impactDirection.normalized() : state.direction;
                    const bool shaftReaches =
                        solidImpact
                            ? BannerArrowBackingReach(state.piercedPoint, impactPoint, incoming, 2 * halfShaftLength)
                            : BannerArrowShaftContains(state.piercedPoint, shaftCenter, incoming, halfShaftLength);
                    if (shaftReaches) {
                        auto* cell = owner->GetParentCell();
                        auto* world = cell ? cell->GetbhkWorld() : nullptr;
                        const auto expected = solidImpact ? impactPoint : convertNi(end);
                        btVector3 target(0, 0, 0), bestSurface(0, 0, 0), bestNormal(0, 0, 0);
                        float bestDistance = std::numeric_limits<float>::max();
                        bool foundSurface = false;
                        auto probe = [&](const btVector3& from, const btVector3& to) {
                            if (!world) {
                                return;
                            }
                            RE::bhkPickData pick{};
                            pick.rayOutput.Reset();
                            const float scale = RE::bhkWorld::GetWorldScale();
                            pick.rayInput.from = RE::hkVector4(convertBt(from * scale));
                            pick.rayInput.to = RE::hkVector4(convertBt(to * scale));
                            pick.ray = pick.rayInput.to - pick.rayInput.from;
                            pick.rayInput.enableShapeCollectionFilter = true;
                            pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kClutter);
                            if (!BP::PickBannerSolid(world, pick)) {
                                return;
                            }
                            const auto surface = from + (to - from) * pick.rayOutput.hitFraction;
                            const float distance = (surface - expected).length2();
                            if (distance >= bestDistance || (solidImpact && distance > 32.0f * 32.0f)) {
                                return;
                            }
                            alignas(16) float normal[4];
                            _mm_store_ps(normal, pick.rayOutput.normal.quad);
                            const btVector3 n(normal[0], normal[1], normal[2]);
                            if (n.length2() < .0001f) {
                                return;
                            }
                            bestDistance = distance;
                            bestSurface = surface;
                            bestNormal = n;
                            foundSurface = true;
                        };
                        probe(expected - incoming * (2 * halfShaftLength + 16), expected + incoming * 24);
                        const btVector3 axes[]{btVector3(1, 0, 0), btVector3(0, 1, 0), btVector3(0, 0, 1)};
                        for (const auto& axis : axes) {
                            probe(expected - axis * 24, expected + axis * 24);
                            probe(expected + axis * 24, expected - axis * 24);
                        }
                        if (foundSurface) {
                            foundSurface =
                                BannerArrowBackingTarget(bestSurface, bestNormal, state.piercedPoint, incoming,
                                                         1.25f * m_bannerRoot->world.scale, target);
                        }
                        else if (solidImpact) {
                            foundSurface =
                                BannerArrowBackingTarget(impactPoint, -incoming, state.piercedPoint, incoming,
                                                         1.25f * m_bannerRoot->world.scale, target);
                        }
                        if (foundSurface) {
                            state.pinLocal = convertNi(model->world.rotate.Transpose() *
                                                       convertBt(target - convertNi(model->world.translate))) /
                                             model->world.scale;
                            state.pinTarget = target;
                            state.pinVelocity.setZero();
                            state.pinned = true;
                            for (auto* bone : grid) {
                                if (bone && !bone->m_rig.isStaticOrKinematicObject()) {
                                    bone->m_rig.setLinearVelocity(bone->m_rig.getLinearVelocity() * 0.15f);
                                }
                            }
                        }
                    }
                }
                if (state.pinned) {
                    const auto target = convertNi(model->world.translate +
                                                  model->world.rotate * convertBt(state.pinLocal * model->world.scale));
                    const auto movement = target - state.pinTarget;
                    if (movement.length2() > 128.0f * 128.0f) {
                        state.pinned = false;
                    } else {
                        state.pinVelocity = movement / std::clamp(RE::GetSecondsSinceLastFrame(), 0.001f, 0.25f);
                        if (state.pinVelocity.length2() > 4000000) {
                            state.pinVelocity *= 2000 / state.pinVelocity.length();
                        }
                        state.pinTarget = target;
                    }
                }
            }
            next.emplace(id, state);
        }
        m_bannerArrows.swap(next);
    }

    void BannerRuntime::ApplyBannerArrowPins() {
        if (m_loading || !RE::UI::GetSingleton() || RE::UI::GetSingleton()->GameIsPaused()) {
            return;
        }
        for (auto& instance : m_bannerInstances) {
            if (!instance.system) {
                continue;
            }
            for (int pass = 0; pass < 3; ++pass) {
                for (auto& [id, state] : instance.arrows) {
                    if (!state.pinned) {
                        continue;
                    }
                    if (instance.damageApplied &&
                        !BannerDamageTrianglePointPresent(state.piercedBones,
                                                          {state.weights.x(), state.weights.y(), state.weights.z()},
                                                          instance.damageMask)) {
                        state.pinned = false;
                        state.ageAfterHit = 7;
                        continue;
                    }
                    const auto arrow = state.projectile.get();
                    if (!arrow || !arrow->Get3D() || arrow->IsDisabled() || arrow->IsDeleted() ||
                        arrow->GetProjectileRuntimeData().flags.any(RE::Projectile::Flags::kFading,
                                                                    RE::Projectile::Flags::kDestroyed)) {
                        state.pinned = false;
                        continue;
                    }
                    std::array<SkinnedMeshBone*, 3> bones{};
                    std::array<btVector3, 3> positions{}, velocities{};
                    std::array<float, 3> inverseMass{};
                    bool valid = true;
                    for (size_t i = 0; i < 3; ++i) {
                        const auto index = state.piercedBones[i];
                        if (index >= 75) {
                            valid = false;
                            break;
                        }
                        bones[i] = instance.bones[index];
                        if (!bones[i]) {
                            valid = false;
                            break;
                        }
                        positions[i] = bones[i]->m_rig.getWorldTransform().getOrigin();
                        velocities[i] = bones[i]->m_rig.getLinearVelocity();
                        inverseMass[i] = bones[i]->m_rig.getInvMass();
                    }
                    if (!valid || !ResolveBannerArrowPin(positions, velocities, inverseMass, state.weights,
                                                         state.pinTarget, state.pinVelocity)) {
                        state.pinned = false;
                        state.ageAfterHit = 7.0f;
                        continue;
                    }
                    for (size_t i = 0; i < 3; ++i) {
                        auto& rig = bones[i]->m_rig;
                        auto transform = rig.getWorldTransform();
                        transform.setOrigin(positions[i]);
                        rig.setWorldTransform(transform);
                        rig.setInterpolationWorldTransform(transform);
                        rig.setLinearVelocity(velocities[i]);
                        rig.setInterpolationLinearVelocity(velocities[i]);
                        instance.previousPositions[state.piercedBones[i]] = positions[i];
                    }
                }
            }
        }
    }
}

namespace hdt {
    namespace {
        bool MagicMatchesKeywords(RE::MagicItem* spell, RE::EffectSetting* setting, RE::BGSProjectile* projectile,
                                  const std::vector<std::string>& keywords) {
            auto matches = [&](const RE::BGSKeywordForm* form) {
                return form && std::any_of(keywords.begin(), keywords.end(), [&](const auto& keyword) {
                           return form->HasKeywordString(keyword);
                       });
            };
            if (matches(spell) || matches(setting)) {
                return true;
            }
            if (spell) {
                for (auto* effect : spell->effects) {
                    if (effect && effect->baseEffect && effect->baseEffect->data.projectileBase == projectile &&
                        matches(effect->baseEffect)) {
                        return true;
                    }
                }
            }
            return false;
        }

        unsigned shoutTier(RE::MagicItem* spell) {
            static const auto tiers = []() {
                std::unordered_map<RE::MagicItem*, unsigned> result;
                if (auto* data = RE::TESDataHandler::GetSingleton()) {
                    for (auto* shout : data->GetFormArray<RE::TESShout>()) {
                        if (shout) {
                            for (unsigned i = 0; i < 3; ++i) {
                                if (auto* spell = shout->variations[i].spell) {
                                    const auto [it, added] = result.emplace(spell, i + 1);
                                    if (!added && it->second != i + 1) {
                                        it->second = 0;
                                    }
                                }
                            }
                        }
                    }
                }
                return result;
            }();
            const auto it = tiers.find(spell);
            return it == tiers.end() ? 0 : it->second;
        }

        float matchingMagnitude(RE::MagicItem* spell, RE::EffectSetting* setting, RE::BGSProjectile* projectile) {
            float result = 0;
            if (!spell) {
                return result;
            }
            for (auto* effect : spell->effects) {
                if (effect && effect->baseEffect &&
                    (setting ? effect->baseEffect == setting : effect->baseEffect->data.projectileBase == projectile)) {
                    const float magnitude = effect->GetMagnitude();
                    if (std::isfinite(magnitude)) {
                        result = std::max(result, std::abs(magnitude));
                    }
                }
            }
            return result;
        }
    }

    void BannerRuntime::SyncBannerMagic() {
        auto* ui = RE::UI::GetSingleton();
        if (m_loading || m_bannerInstances.empty()) {
            m_bannerMagic.clear();
            return;
        }
        if (!ui || ui->GameIsPaused()) {
            return;
        }
        auto* manager = RE::Projectile::Manager::GetSingleton();
        if (!manager) {
            return;
        }
        const float dt = std::clamp(RE::GetSecondsSinceLastFrame(), 0.f, .05f);
        if (dt <= 0) {
            return;
        }
        for (auto& [id, state] : m_bannerDamage) {
            state.magicDamageCooldown = std::max(0.f, state.magicDamageCooldown - dt);
        }
        std::vector<RE::ProjectileHandle> handles;
        {
            RE::BSSpinLockGuard guard(manager->projectileLock);
            for (auto h : manager->limited) {
                handles.push_back(h);
            }
            for (auto h : manager->unlimited) {
                handles.push_back(h);
            }
            for (auto h : manager->pending) {
                handles.push_back(h);
            }
        }

        struct Effect {
            RE::FormID id;
            RE::TESObjectCELL* cell;
            btVector3 from, to, direction;
            float radius, speed;
            bool continuous, blast, damage, heat;
        };

        std::vector<Effect> effects;
        std::unordered_map<RE::FormID, BannerMagicState> next;
        for (auto handle : handles) {
            auto projectile = handle.get();
            auto* p = projectile.get();
            if (!p || p->IsDisabled() || p->IsDeleted()) {
                continue;
            }
            auto* base = p->GetBaseObject() ? p->GetBaseObject()->As<RE::BGSProjectile>() : nullptr;
            if (!base || base->IsArrow() || p->As<RE::ArrowProjectile>() || base->IsBarrier()) {
                continue;
            }
            auto& runtime = p->GetProjectileRuntimeData();
            if (!runtime.spell && !runtime.avEffect) {
                continue;
            }
            const auto end = convertNi(p->GetPosition());
            if (!std::isfinite(end.x()) || !std::isfinite(end.y()) || !std::isfinite(end.z())) {
                continue;
            }
            const auto id = p->GetFormID();
            if (next.contains(id)) {
                continue;
            }
            auto prior = m_bannerMagic.find(id);
            const bool tracked = prior != m_bannerMagic.end() && prior->second.projectile.get().get() == p &&
                                 runtime.livingTime >= prior->second.livingTime;
            BannerMagicState state;
            if (tracked) {
                state = std::move(prior->second);
            }
            const bool voice =
                runtime.spell && runtime.spell->GetSpellType() == RE::MagicSystem::SpellType::kVoicePower;
            const unsigned tier = voice ? shoutTier(runtime.spell) : 0;
            const float magnitude = matchingMagnitude(runtime.spell, runtime.avEffect, base);
            const float intensity = BannerMagicIntensity(tier, magnitude, runtime.power);
            const bool damages =
                MagicMatchesKeywords(runtime.spell, runtime.avEffect, base, BP::Settings::increaseDegradationKeywords);
            const bool heat =
                MagicMatchesKeywords(runtime.spell, runtime.avEffect, base, BP::Settings::triggerBurnKeywords);
            const bool continuous = !voice && (base->IsBeam() || base->IsFlamethrower());
            auto direction = convertNi(runtime.linearVelocity);
            if (direction.length2() < .001f) {
                const float pitch = p->GetAngleX(), yaw = p->GetAngleZ();
                direction = {std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch)};
            }
            if (!std::isfinite(direction.length2()) || direction.length2() < .001f) {
                continue;
            }
            direction.normalize();
            auto from = tracked
                            ? convertNi(state.position)
                            : end - convertNi(runtime.linearVelocity) * std::min(dt, std::max(0.f, runtime.livingTime));
            if ((end - from).length2() > 4096.f * 4096.f) {
                from = end;
            }
            auto to = end;
            float radius =
                std::isfinite(base->data.collisionRadius) ? std::clamp(base->data.collisionRadius, 12.f, 100.f) : 16.f;
            if (base->IsCone()) {
                if (auto* cone = p->As<RE::ConeProjectile>()) {
                    const auto& data = cone->GetConeRuntimeData();
                    if (std::isfinite(data.initialCollisionSphereRadius)) {
                        radius = std::max(radius, std::clamp(std::abs(data.initialCollisionSphereRadius), 16.f, 280.f));
                    }
                    const float travel = (end - convertNi(data.origin)).length();
                    if (std::isfinite(travel) && std::isfinite(data.coneAngleTangent)) {
                        radius = std::clamp(radius + std::abs(data.coneAngleTangent) * travel, 16.f, 280.f);
                    }
                }
            }
            if (continuous) {
                from = end;
                const float range =
                    std::isfinite(base->data.range) ? std::clamp(base->data.range, 64.f, 4096.f) : 1024.f;
                to = from + direction * range;
                if (base->IsFlamethrower()) {
                    radius = std::max(radius, 32.f);
                }
            }
            const bool fading = runtime.flags.any(RE::Projectile::Flags::kDestroyed, RE::Projectile::Flags::kFading);
            if (!fading || (!continuous && (to - from).length2() > .001f)) {
                effects.push_back({id, p->GetParentCell(), from, to, direction, radius,
                                   BannerMagicImpulseBudget(voice, continuous, base->data.force, dt) * intensity,
                                   continuous, false, damages, heat});
            }
            auto* explosion = runtime.explosion ? runtime.explosion : base->data.explosionType;
            if (explosion && !runtime.impacts.empty()) {
                const auto* impact = runtime.impacts.front();
                if (impact) {
                    const auto at = convertNi(impact->desiredTargetLoc);
                    const float r = explosion->data.radius;
                    if (std::isfinite(at.length2()) && (at - end).length2() < 512.f * 512.f && std::isfinite(r) &&
                        r > 0) {
                        effects.push_back(
                            {id,
                             p->GetParentCell(),
                             at,
                             at,
                             {0, 0, 0},
                             std::clamp(r, 24.f, 1024.f),
                             BannerMagicImpulseBudget(false, false, explosion->data.force, dt) * intensity,
                             false,
                             true,
                             damages,
                             heat});
                    }
                }
            }
            state.projectile = handle;
            state.position = p->GetPosition();
            state.livingTime = runtime.livingTime;
            next.emplace(id, std::move(state));
        }
        if (!effects.empty()) {
            for (auto& instance : m_bannerInstances) {
                auto ref = instance.reference.get();
                if (!ref || !instance.system || !instance.root) {
                    continue;
                }
                auto* world = ref->GetParentCell() ? ref->GetParentCell()->GetbhkWorld() : nullptr;
                if (!world) {
                    continue;
                }
                std::array<SkinnedMeshBone*, 88> bones{};
                std::array<btVector3, 88> positions{};
                btVector3 center(0, 0, 0);
                bool valid = true;
                for (unsigned i = 0; i < 75; ++i) {
                    bones[i] = instance.bones[i];
                    if (!bones[i]) {
                        valid = false;
                        break;
                    }
                    positions[i] = bones[i]->m_rig.getWorldTransform().getOrigin();
                    center += positions[i] / 75.f;
                }
                if (!valid) {
                    continue;
                }
                for (unsigned i = 75; i < 88; ++i) {
                    if ((bones[i] = instance.bones[i])) {
                        positions[i] = bones[i]->m_rig.getWorldTransform().getOrigin();
                    }
                }
                float extent = 0;
                for (unsigned i = 0; i < 88; ++i) {
                    if (bones[i]) {
                        extent = std::max(extent, (positions[i] - center).length());
                    }
                }
                for (const auto& effect : effects) {
                    auto* bannerCell = ref->GetParentCell();
                    if (effect.cell != bannerCell &&
                        (!effect.cell || !bannerCell || effect.cell->IsInteriorCell() || bannerCell->IsInteriorCell() ||
                         effect.cell->GetRuntimeData().worldSpace != bannerCell->GetRuntimeData().worldSpace)) {
                        continue;
                    }
                    auto& state = next.at(effect.id);
                    auto& pushed = effect.blast ? state.blasted : state.pushed;
                    if (!effect.continuous && pushed.contains(ref->GetFormID())) {
                        continue;
                    }
                    const auto delta = effect.to - effect.from;
                    const float len2 = delta.length2();
                    const float fraction =
                        len2 > .0001f ? std::clamp((center - effect.from).dot(delta) / len2, 0.f, 1.f) : 0;
                    if ((center - (effect.from + delta * fraction)).length2() >
                        (extent + effect.radius) * (extent + effect.radius)) {
                        continue;
                    }
                    btVector3 contact(0, 0, 0);
                    float best = 2;
                    bool hit = false, ropeHit = false;
                    for (unsigned face = 0; face < 112; ++face) {
                        const unsigned cell = face / 2, a = (cell / 4) * 5 + cell % 4;
                        const std::array<unsigned, 3> ids = face % 2 ? std::array<unsigned, 3>{a + 1, a + 6, a + 5}
                                                                     : std::array<unsigned, 3>{a, a + 1, a + 5};
                        float at;
                        btVector3 weights;
                        if (!BannerArrowClothHit(effect.from, delta,
                                                 {positions[ids[0]], positions[ids[1]], positions[ids[2]]},
                                                 effect.radius, at, weights) ||
                            at >= best) {
                            continue;
                        }
                        if (instance.damageApplied &&
                            !BannerDamageTrianglePointPresent({uint8_t(ids[0]), uint8_t(ids[1]), uint8_t(ids[2])},
                                                              {weights.x(), weights.y(), weights.z()},
                                                              instance.damageMask)) {
                            continue;
                        }
                        best = at;
                        contact = positions[ids[0]] * weights[0] + positions[ids[1]] * weights[1] +
                                  positions[ids[2]] * weights[2];
                        hit = true;
                    }
                    for (unsigned i = 75; i < 87; ++i) {
                        if (bones[i] && bones[i + 1]) {
                            float at, along;
                            if (!BannerRopeHit(effect.from, delta, positions[i], positions[i + 1],
                                               effect.radius + 1.45f * instance.root->world.scale, at, along) ||
                                at >= best) {
                                continue;
                            }
                            best = at;
                            contact = positions[i].lerp(positions[i + 1], along);
                            hit = true;
                            ropeHit = true;
                        }
                    }
                    if (!hit) {
                        continue;
                    }

                    RE::bhkPickData pick{};
                    pick.rayOutput.Reset();
                    const float hs = RE::bhkWorld::GetWorldScale();
                    pick.rayInput.from = RE::hkVector4(convertBt(effect.from * hs));
                    pick.rayInput.to = RE::hkVector4(convertBt(contact * hs));
                    pick.ray = pick.rayInput.to - pick.rayInput.from;
                    pick.rayInput.enableShapeCollectionFilter = true;
                    pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kClutter);
                    if (BP::PickBannerSolid(world, pick)) {
                        const float before = (1 - pick.rayOutput.hitFraction) * (contact - effect.from).length();
                        if (before > 4.f) {
                            continue;
                        }
                    }
                    if (!ropeHit && BP::Settings::bEnableDegradation &&
                        (effect.heat || (BP::Settings::bEnableMagicDegradation && effect.damage &&
                                         (effect.continuous || !state.damaged.contains(ref->GetFormID()))))) {
                        auto* manifest = FindBannerStringData(instance.root.get(), "BP_BannerDamageShapes");
                        if (manifest && manifest->value && *manifest->value) {
                            auto& damage = m_bannerDamage[ref->GetFormID()];
                            damage.reference = instance.reference;
                            damage.cell = bannerCell->GetFormID();
                            if (damage.coverage[0].empty()) {
                                for (unsigned stage = 1; stage <= 4; ++stage) {
                                    const auto key = "BP_BannerDamageMask" + std::to_string(stage);
                                    if (auto* mask = FindBannerStringData(instance.root.get(), key.c_str());
                                        mask && mask->value) {
                                        damage.coverage[stage - 1] = mask->value;
                                    }
                                }
                            }
                            if (BP::Settings::bEnableMagicDegradation && effect.damage &&
                                (effect.continuous || !state.damaged.contains(ref->GetFormID()))) {
                                BannerMagicDamageHit(damage.hits, damage.magicDamageCooldown);
                            }
                            if (!effect.continuous && effect.damage && BP::Settings::bEnableMagicDegradation) {
                                state.damaged.insert(ref->GetFormID());
                            }
                        }
                    }
                    if (!ropeHit && effect.heat && BP::Settings::bEnableDegradation) {
                        if (auto damage = m_bannerDamage.find(ref->GetFormID()); damage != m_bannerDamage.end()) {
                            damage->second.fireContactTime = .15f;
                        }
                    }
                    bool applied = false;
                    const float spread =
                        effect.blast ? effect.radius : std::max(120.f * instance.root->world.scale, effect.radius);
                    const float budget = effect.speed * BannerWindScaleFactor(instance.root->world.scale);
                    std::array<float, 88> weights{};
                    float totalWeight = 0;
                    for (unsigned i = 5; i < 88; ++i) {
                        if (bones[i] && !bones[i]->m_rig.isStaticOrKinematicObject()) {
                            weights[i] = BannerMagicFalloff(
                                (positions[i] - (effect.blast ? effect.from : contact)).length(), spread);
                            totalWeight += weights[i];
                        }
                    }
                    if (totalWeight <= .000001f) {
                        continue;
                    }
                    for (unsigned i = 5; i < 88; ++i) {
                        if (!bones[i]) {
                            continue;
                        }
                        auto& rig = bones[i]->m_rig;
                        if (rig.isStaticOrKinematicObject()) {
                            continue;
                        }
                        const float weight = weights[i] / totalWeight;
                        const auto direction = effect.blast ? positions[i] - effect.from : effect.direction;
                        const auto kick =
                            BannerMagicKick(rig.getLinearVelocity(), direction, budget * rig.getInvMass(), weight);
                        if (kick.length2() > .000001f) {
                            rig.activate(true);
                            rig.applyCentralImpulse(kick / rig.getInvMass());

                            applied = true;
                        }
                    }
                    if (applied && !effect.continuous) {
                        pushed.insert(ref->GetFormID());
                    }
                }
            }
        }
        m_bannerMagic = std::move(next);
    }
}
