#ifndef FSMP_BRIDGE_H
#define FSMP_BRIDGE_H
#include "hdtSkyrimPhysicsWorld.h"
#include "PluginAPI.h"

namespace BP {
    bool verifyStock();
    bool connect(hdt::PluginInterface*);
    bool loading();
    bool isSpring(const hdt::BoneScaleConstraint*);
    uintptr_t address(uintptr_t rva);
}

#endif
