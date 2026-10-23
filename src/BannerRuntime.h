#ifndef BANNER_RUNTIME_H
#define BANNER_RUNTIME_H
#include "hdtSkyrimPhysicsWorld.h"
#include "BannerCollision.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace hdt {
    class BannerRuntime : public RE::BSTEventSink<RE::TESCellAttachDetachEvent>,
                          public RE::BSTEventSink<RE::TESSpellCastEvent> {
    public:
        static BannerRuntime& GetSingleton();
        void BeforeFrame();
        void BeforeWrite();
        void AfterWrite();
        void Shutdown();

        bool HasBanners() const {
            return !m_bannerInstances.empty();
        }

        void AddSkinnedMeshSystem(SkinnedMeshSystem*);
        void RemoveSkinnedMeshSystem(SkinnedMeshSystem*);
        void RefreshConstraints();

        bool OwnsSpring(const StiffSpringConstraint* p) const {
            return m_springs.contains(p);
        }

        void ApplyWindTwist(const std::vector<std::pair<SkinnedMeshBone*, btVector3>>&);
        std::vector<std::pair<SkinnedMeshBone*, btVector3>> CaptureWindForces();
        RE::BSEventNotifyControl ProcessEvent(const RE::TESCellAttachDetachEvent*,
                                              RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override;
        void QueueBannerDamageReset();
        RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent*,
                                              RE::BSTEventSource<RE::TESSpellCastEvent>*) override;

    private:
        struct BannerArrowState {
            RE::NiPoint3 position;
            bool hit = false;
            bool ropeHit = false;
            RE::ProjectileHandle projectile;
            std::array<uint8_t, 3> piercedBones{};
            btVector3 weights{0, 0, 0}, piercedPoint{0, 0, 0}, direction{0, 0, 0};
            btVector3 pinLocal{0, 0, 0}, pinTarget{0, 0, 0}, pinVelocity{0, 0, 0};
            float ageAfterHit = 0;
            bool pinned = false;
        };

        void SyncBannerInstance(RE::TESObjectREFR* candidate);
        void SyncAllBanners();
        void ResolveBannerRooms();
        void SyncBannerMagic();
        void ApplyBannerEnchantmentHit(RE::EnchantmentItem* enchantment, RE::ObjectRefHandle reference);

        struct BannerMagicState {
            RE::ProjectileHandle projectile;
            RE::NiPoint3 position;
            float livingTime = 0;
            std::unordered_set<RE::FormID> pushed, blasted, damaged;
        };

        std::unordered_map<RE::FormID, BannerMagicState> m_bannerMagic;
        void CollideBannerWithWorld();
        void CollideBannerWithActors();
        void ApplyBannerArrowPins();
        void SyncBannerDamage();
        void SyncBannerArrows(std::unordered_map<RE::FormID, BannerArrowState>& arrows,
                              const std::array<SkinnedMeshBone*, 88>& bones,
                              const std::vector<RE::ProjectileHandle>& flying);
        RE::ObjectRefHandle m_bannerReference;
        RE::NiPointer<RE::NiNode> m_bannerRoot;
        RE::BSTSmartPointer<SkyrimSystem> m_bannerSystem;
        float m_bannerScanTimer = 0.0f;

        bool m_loading = false;
        std::unordered_set<const StiffSpringConstraint*> m_springs;
        std::vector<StiffSpringConstraint*> m_fabric;

        struct BannerInstance {
            RE::ObjectRefHandle reference;
            RE::NiPointer<RE::NiNode> root;
            RE::BSTSmartPointer<SkyrimSystem> system;
            std::unordered_map<RE::FormID, BannerArrowState> arrows;
            std::array<btVector3, 88> previousPositions{};
            std::array<BP::BannerSweepHistory, 88> worldSweeps{};
            std::array<SkinnedMeshBone*, 88> bones{};
            bool worldContactInitialized = false;
            bool roomResolved = false;
            std::array<BannerWallSupport, 88> wallSupports{};
            std::array<BannerFaceSupport, 112> faceSupports{};
            unsigned faceProbeCursor = 0, recoveryProbeCursor = 0;
            btVector3 recoveryNormal{0, 0, 0}, recoveryDown{0, 0, 0};
            bool damageApplied = false;
            std::string damageMask;
        };

        std::vector<BannerInstance> m_bannerInstances;
        size_t m_worldProbeCursor = 0;

        struct BannerDamagePart {
            RE::NiPointer<RE::BSTriShape> geometry;
            RE::NiPointer<RE::BSShaderProperty> originalShader;
            RE::BSTSmartPointer<RE::BSShaderMaterial> originalMaterial;
            RE::NiPointer<RE::NiAlphaProperty> originalAlpha, animatedAlpha;
            RE::BSTSmartPointer<RE::BSShaderMaterial> cooledMaterial, fireMaterial;
            RE::BSShaderMaterial* cooledLiveMaterial = nullptr;
            std::unique_ptr<RE::NiColor> fireColor;
            RE::NiColor* originalEmissiveColor = nullptr;
            float originalEmissiveMult = 0;
            bool originalOwnEmit = false, originalGlowMap = false, fireGlowActive = false;
            bool originalNoTransparencyMultiSample = false;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> wearRanks;
            RE::NiPointer<RE::NiSourceTexture> wearTexture;
            int wearCutoff = -1, glowCutoff = -1;
            unsigned glowWidth = 0;
            unsigned layout = 0;
            unsigned ordinal = 0;
            int stage = 0;
        };

        struct BannerDamageState {
            RE::ObjectRefHandle reference;
            RE::FormID cell = 0;
            std::array<std::string, 4> coverage;
            uint8_t hits = 0;
            float magicDamageCooldown = 0;
            float fireContactTime = 0, fireGlowHeat = 0, fireGlowReleaseHeat = 0, fireGlowCooldownAge = 0;
            RE::FormID lastHitActor = 0;
            uint64_t lastHitSwing = 0;
            RE::NiPointer<RE::NiNode> root;
            std::vector<BannerDamagePart> parts;
            bool applied = false;
            int appliedStage = 0;
            int completedStage = 0;
            float transitionAge = 0;
        };

        void RestoreBannerDamage(BannerDamageState&);
        void UpdateBannerFireGlow(BannerDamagePart&, float);
        void StabilizeBannerFabric();
        void HoldBannerAnchors();
        void CollideBannerFaces(BannerInstance&, RE::bhkWorld*, const std::array<btVector3, 88>&, int&);
        bool ApplyBannerDamagePart(BannerDamageState&, RE::NiNode*, RE::BSTriShape*, unsigned, unsigned, int);
        std::unordered_map<RE::FormID, BannerDamageState> m_bannerDamage;

        struct BannerAttackCycle {
            RE::ATTACK_STATE_ENUM state = RE::ATTACK_STATE_ENUM::kNone;
            bool attacking = false;
            uint64_t serial = 0;
        };

        std::unordered_map<RE::FormID, BannerAttackCycle> m_bannerAttackCycles;
        std::unordered_map<std::string, RE::NiPointer<RE::NiNode>> m_damageTextureModels;
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_damageWearRanks;
        std::unordered_map<std::string, RE::NiPointer<RE::NiSourceTexture>> m_damageGlowTextures;
        std::unordered_map<std::string, RE::NiPointer<RE::NiNode>> m_damageGlowModels;
        std::mutex m_damageEventLock;
        std::vector<std::pair<RE::ObjectRefHandle, RE::FormID>> m_areaCasts;
        std::vector<RE::ObjectRefHandle> m_areaHazards;
        float m_hazardScanAge = .5f;
        std::vector<RE::FormID> m_detachedBannerReferences;
        bool m_damageResetRequested = false;
    };
}

#endif
