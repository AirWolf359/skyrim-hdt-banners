#include "FSMPBridge.h"
#include "BannerRuntime.h"
#include <MinHook.h>
#include <bcrypt.h>
#include <filesystem>
#include "hdtConvertNi.h"

namespace BP {
    static uintptr_t base = 0;

    uintptr_t address(uintptr_t rva) {
        return base + rva;
    }

    template <class T> T pointer(uintptr_t rva) {
        return reinterpret_cast<T>(address(rva));
    }

    template <class T> T member(uintptr_t rva) {
        T out{};
        static_assert(sizeof(T) >= sizeof(uintptr_t) && sizeof(T) <= 16);
        auto p = address(rva);
        std::memcpy(&out, &p, sizeof(p));
        return out;
    }

    bool loading() {
        auto* world = hdt::SkyrimPhysicsWorld::get();
        return *reinterpret_cast<const bool*>(reinterpret_cast<const std::byte*>(world) +
                                              Offsets::selected->loadingFlag);
    }

    bool isSpring(const hdt::BoneScaleConstraint* c) {
        return c && *reinterpret_cast<const uintptr_t*>(c) == address(Offsets::selected->springVtable);
    }

    static std::string hashFile(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        BCRYPT_ALG_HANDLE alg{};
        BCRYPT_HASH_HANDLE hash{};
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
            return {};
        }
        DWORD bytes{}, objectSize{};
        BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &bytes,
                          0);
        std::vector<unsigned char> object(objectSize);
        std::array<unsigned char, 32> digest{};
        bool ok = BCryptCreateHash(alg, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0;
        std::array<char, 65536> chunk{};
        while (ok && file) {
            file.read(chunk.data(), chunk.size());
            if (file.gcount()) {
                ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(chunk.data()), static_cast<ULONG>(file.gcount()),
                                    0) >= 0;
            }
        }
        ok = ok && file.eof() && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
        if (hash) {
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
        if (!ok) {
            return {};
        }
        constexpr char hex[] = "0123456789abcdef";
        std::string result;
        for (auto b : digest) {
            result += hex[b >> 4];
            result += hex[b & 15];
        }
        return result;
    }

    bool verifyStock() {
        auto module = GetModuleHandleW(L"hdtsmp64.dll");
        if (!module) {
            logger::critical("Stock FSMP is not loaded; Banner Physics disabled");
            return false;
        }
        std::array<wchar_t, 32768> path{};
        const auto n = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!n || n >= path.size()) {
            return false;
        }
        const auto hash = hashFile(std::filesystem::path(path.data()));
        if (!Offsets::Select(hash)) {
            std::string reason;
            if (!Offsets::Detect(module, path.data(), &reason)) {
                logger::critical("Unsupported FSMP binary: {}. No hooks installed.", reason);
                return false;
            }
        }
        base = reinterpret_cast<uintptr_t>(module);
        for (const auto& signature : Offsets::selected->signatures) {
            if (std::memcmp(reinterpret_cast<const void*>(address(signature.rva)), signature.bytes, 16)) {
                logger::critical("FSMP entry 0x{:x} differs in memory; Banner Physics disabled", signature.rva);
                base = 0;
                return false;
            }
        }
        return true;
    }

    static void (*originalUpdate)(hdt::SkyrimPhysicsWorld*, float) = nullptr;
    static void (*originalSpring)(btTypedConstraint*, btTypedConstraint::btConstraintInfo2*) = nullptr;
    static void (*originalWind)(hdt::SkinnedMeshWorld*, float) = nullptr;

    static bool gameFocused() {
        DWORD process = 0;
        const auto window = GetForegroundWindow();
        return window && GetWindowThreadProcessId(window, &process) && process == GetCurrentProcessId();
    }

    static void update(hdt::SkyrimPhysicsWorld* world, float dt) {
        auto* actors = pointer<hdt::ActorManager* (*)()>(Offsets::selected->actors)();
        const int old = actors->activeSkeletons;
        const bool admit = old == 0 && gameFocused() && hdt::BannerRuntime::GetSingleton().HasBanners();
        if (admit) {
            actors->activeSkeletons = 1;
        }
        originalUpdate(world, dt);
        if (admit) {
            actors->activeSkeletons = old;
        }
    }

    static void spring(btTypedConstraint* baseConstraint, btTypedConstraint::btConstraintInfo2* info) {
        originalSpring(baseConstraint, info);
        auto* c = static_cast<hdt::StiffSpringConstraint*>(baseConstraint);
        if (!hdt::BannerRuntime::GetSingleton().OwnsSpring(c)) {
            return;
        }
        const auto a = c->m_boneA->m_rigToLocal * c->getRigidBodyA().getWorldTransform();
        const auto b = c->m_boneB->m_rigToLocal * c->getRigidBodyB().getWorldTransform();
        const auto distance = (a.getOrigin() - b.getOrigin()).length();
        if (distance < c->m_minDistance || distance > c->m_maxDistance) {
            info->m_constraintError[0] = -info->m_constraintError[0];
        }
    }

    static void wind(hdt::SkinnedMeshWorld* world, float dt) {
        if (!gameFocused()) {
            originalWind(world, dt);
            return;
        }
        auto& runtime = hdt::BannerRuntime::GetSingleton();
        auto forces = runtime.CaptureWindForces();
        originalWind(world, dt);
        runtime.ApplyWindTwist(forces);
    }

    struct FrameSink : RE::BSTEventSink<Events::FrameEvent> {
        RE::BSEventNotifyControl ProcessEvent(const Events::FrameEvent*,
                                              RE::BSTEventSource<Events::FrameEvent>*) override {
            if (gameFocused()) {
                hdt::BannerRuntime::GetSingleton().BeforeFrame();
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    struct SyncSink : RE::BSTEventSink<Events::FrameSyncEvent> {
        bool after = false;

        RE::BSEventNotifyControl ProcessEvent(const Events::FrameSyncEvent*,
                                              RE::BSTEventSource<Events::FrameSyncEvent>*) override {
            if (!gameFocused()) {
                return RE::BSEventNotifyControl::kContinue;
            }
            if (after) {
                hdt::BannerRuntime::GetSingleton().AfterWrite();
            } else {
                hdt::BannerRuntime::GetSingleton().BeforeWrite();
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    struct ShutdownSink : RE::BSTEventSink<Events::ShutdownEvent> {
        RE::BSEventNotifyControl ProcessEvent(const Events::ShutdownEvent*,
                                              RE::BSTEventSource<Events::ShutdownEvent>*) override {
            hdt::BannerRuntime::GetSingleton().Shutdown();
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    bool connect(hdt::PluginInterface* api) {
        static bool connected = false;
        if (connected) {
            return true;
        }
        if (!api) {
            return false;
        }
        const auto& v = api->getVersionInfo();
        if (v.interfaceVersion.major != 2 || v.bulletVersion.major != 3 || v.bulletVersion.minor != 24 ||
            v.bulletVersion.patch != 0) {
            logger::critical("Incompatible FSMP addon/Bullet ABI; no hooks installed");
            return false;
        }
        if (!verifyStock()) {
            return false;
        }
        const auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            return false;
        }

        struct Hook {
            uintptr_t rva;
            void* target;
            void** original;
        };

        Hook hooks[] = {
            {Offsets::selected->doUpdate, reinterpret_cast<void*>(&update), reinterpret_cast<void**>(&originalUpdate)},
            {Offsets::selected->springInfo, reinterpret_cast<void*>(&spring),
             reinterpret_cast<void**>(&originalSpring)},
            {Offsets::selected->wind, reinterpret_cast<void*>(&wind), reinterpret_cast<void**>(&originalWind)}};
        unsigned created = 0;
        for (auto& hook : hooks) {
            if (MH_CreateHook(reinterpret_cast<void*>(address(hook.rva)), hook.target, hook.original) != MH_OK) {
                break;
            }
            ++created;
        }
        if (created != std::size(hooks)) {
            for (unsigned i = 0; i < created; ++i) {
                MH_RemoveHook(reinterpret_cast<void*>(address(hooks[i].rva)));
            }
            logger::critical("Could not prepare FSMP hooks; integration disabled");
            return false;
        }
        for (auto& hook : hooks) {
            MH_QueueEnableHook(reinterpret_cast<void*>(address(hook.rva)));
        }
        if (MH_ApplyQueued() != MH_OK) {
            for (auto& hook : hooks) {
                MH_DisableHook(reinterpret_cast<void*>(address(hook.rva)));
                MH_RemoveHook(reinterpret_cast<void*>(address(hook.rva)));
            }
            return false;
        }
        static FrameSink frame;
        static SyncSink before, after;
        after.after = true;
        static ShutdownSink Shutdown;
        pointer<Events::Sources::FrameEventSource* (*)()>(Offsets::selected->frameSource)()->PrependEventSink(&frame);
        auto* sync = pointer<Events::Sources::FrameSyncEventSource* (*)()>(Offsets::selected->syncSource)();
        sync->PrependEventSink(&before);
        sync->AddEventSink(&after);
        pointer<Events::Sources::ShutdownEventEventSource* (*)()>(Offsets::selected->shutdownSource)()
            ->PrependEventSink(&Shutdown);
        connected = true;
        return true;
    }
}

namespace hdt {
    static_assert(sizeof(SkyrimPhysicsWorld) == 1024);
    static_assert(sizeof(SkyrimSystem) == 192);
    static_assert(sizeof(SkyrimBone) == 1072);
    static_assert(sizeof(SkyrimSystemCreator) == 560);
    static_assert(sizeof(StiffSpringConstraint) == 144);

    SkyrimPhysicsWorld* SkyrimPhysicsWorld::get() {
        return BP::pointer<SkyrimPhysicsWorld* (*)()>(BP::Offsets::selected->world)();
    }

    std::unique_lock<std::mutex> SkyrimPhysicsWorld::lockSimulation() {
        auto fn = BP::member<decltype(&SkyrimPhysicsWorld::lockSimulation)>(BP::Offsets::selected->lock);
        return (this->*fn)();
    }

    DefaultBBP* DefaultBBP::instance() {
        return BP::pointer<DefaultBBP* (*)()>(BP::Offsets::selected->bbp)();
    }

    DefaultBBP::PhysicsFile_t DefaultBBP::scanBBP(RE::NiNode* root) {
        auto fn = BP::member<decltype(&DefaultBBP::scanBBP)>(BP::Offsets::selected->scan);
        return (this->*fn)(root);
    }

    SkyrimSystemCreator::SkyrimSystemCreator() {}

    RE::BSTSmartPointer<SkyrimSystem> SkyrimSystemCreator::createOrUpdateSystem(
        RE::NiNode* skeleton, RE::NiAVObject* model, DefaultBBP::PhysicsFile_t* file,
        std::unordered_map<RE::BSFixedString, RE::BSFixedString>&& rename, SkyrimSystem* old) {
        auto fn = BP::member<decltype(&SkyrimSystemCreator::createOrUpdateSystem)>(BP::Offsets::selected->create);
        return (this->*fn)(skeleton, model, file, std::move(rename), old);
    }

    SkinnedMeshBone* SkyrimSystem::findBone(const RE::BSFixedString& name) {
        auto fn = BP::member<decltype(&SkyrimSystem::findBone)>(BP::Offsets::selected->findBone);
        return (this->*fn)(name);
    }
}

namespace hdt {
    btQuaternion convertNi(const RE::NiMatrix3& rhs) {
        btMatrix3x3 mat;
        mat[0][0] = rhs.entry[0][0];
        mat[0][1] = rhs.entry[0][1];
        mat[0][2] = rhs.entry[0][2];
        mat[1][0] = rhs.entry[1][0];
        mat[1][1] = rhs.entry[1][1];
        mat[1][2] = rhs.entry[1][2];
        mat[2][0] = rhs.entry[2][0];
        mat[2][1] = rhs.entry[2][1];
        mat[2][2] = rhs.entry[2][2];
        btQuaternion q;
        mat.getRotation(q);
        return q;
    }

    RE::NiTransform convertBt(const btQsTransform& rhs) {
        RE::NiTransform ret;

        ret.rotate = convertBt(btMatrix3x3(rhs.getBasis()));
        ret.translate = convertBt(rhs.getOrigin());
        ret.scale = rhs.getScale();

        return ret;
    }

    RE::NiMatrix3 convertBt(const btQuaternion& rhs) {
        btMatrix3x3 mat(rhs.normalized());
        return convertBt(mat);
    }

    RE::NiPoint3 convertBt(const btVector3& rhs) {
        RE::NiPoint3 ret;
        ret.x = rhs[0];
        ret.y = rhs[1];
        ret.z = rhs[2];
        return ret;
    }

    RE::NiMatrix3 convertBt(const btMatrix3x3& rhs) {
        RE::NiMatrix3 ret;
        ret.entry[0][0] = rhs[0][0];
        ret.entry[0][1] = rhs[0][1];
        ret.entry[0][2] = rhs[0][2];
        ret.entry[1][0] = rhs[1][0];
        ret.entry[1][1] = rhs[1][1];
        ret.entry[1][2] = rhs[1][2];
        ret.entry[2][0] = rhs[2][0];
        ret.entry[2][1] = rhs[2][1];
        ret.entry[2][2] = rhs[2][2];
        return ret;
    }
}
