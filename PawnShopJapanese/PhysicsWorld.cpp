#include "PhysicsWorld.h"

#include <cstdarg>
#include <cstdio>

#include <cmath>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>

#include <Jolt/Physics/Body/Body.h>
#ifndef __EMSCRIPTEN__
#include <thread>
#endif
#include <algorithm>

using namespace JPH;



static void TraceImpl(const char* inFMT, ...)
{
    char buffer[1024];

    va_list list;
    va_start(list, inFMT);
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);

    TraceLog(LOG_INFO, "%s", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, unsigned int inLine)
{
    TraceLog(LOG_ERROR, "Jolt assert failed! Expr: %s | Msg: %s | File: %s:%u",
        inExpression ? inExpression : "",
        inMessage ? inMessage : "",
        inFile ? inFile : "",
        inLine);

    return false;
}
#endif

namespace Layers
{
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING = 1;
    static constexpr ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers
{
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint NUM_LAYERS = 2;
}

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
{
public:
    uint GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override
    {
        switch (inLayer)
        {
        case Layers::NON_MOVING: return BroadPhaseLayers::NON_MOVING;
        case Layers::MOVING:     return BroadPhaseLayers::MOVING;
        default:                 return BroadPhaseLayers::NON_MOVING;
        }
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override
    {
        switch (inLayer.GetValue())
        {
        case 0: return "NON_MOVING";
        case 1: return "MOVING";
        default: return "UNKNOWN";
        }
    }
#endif
};
class ObjectVsBroadPhaseLayerFilterImpl final : public ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
    {
        switch (inLayer1)
        {
        case Layers::NON_MOVING:
            return inLayer2 == BroadPhaseLayers::MOVING;

        case Layers::MOVING:
            return true;

        default:
            return false;
        }
    }
};

class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter
{
public:
    bool ShouldCollide(ObjectLayer inLayer1, ObjectLayer inLayer2) const override
    {
        if (inLayer1 == Layers::NON_MOVING && inLayer2 == Layers::NON_MOVING)
            return false;

        return true;
    }
};



class CharacterBroadPhaseLayerFilterImpl final : public BroadPhaseLayerFilter
{
public:
    bool ShouldCollide(BroadPhaseLayer inLayer) const override
    {
        return inLayer == BroadPhaseLayers::NON_MOVING || inLayer == BroadPhaseLayers::MOVING;
    }
};

class CharacterObjectLayerFilterImpl final : public ObjectLayerFilter
{
public:
    bool ShouldCollide(ObjectLayer inLayer) const override
    {
        return inLayer == Layers::NON_MOVING || inLayer == Layers::MOVING;
    }
};

class CharacterBodyFilterImpl final : public BodyFilter
{
public:
    bool ShouldCollide(const BodyID& inBodyID) const override
    {
        return true;
    }

    bool ShouldCollideLocked(const Body& inBody) const override
    {
        return true;
    }
};

class CharacterShapeFilterImpl final : public ShapeFilter
{
public:
    bool ShouldCollide(const Shape* inShape2, const SubShapeID& inSubShapeIDOfShape2) const override
    {
        return true;
    }

    bool ShouldCollide(
        const Shape* inShape1,
        const SubShapeID& inSubShapeIDOfShape1,
        const Shape* inShape2,
        const SubShapeID& inSubShapeIDOfShape2
    ) const override
    {
        return true;
    }
};

static BPLayerInterfaceImpl sBroadPhaseLayerInterface;
static ObjectVsBroadPhaseLayerFilterImpl sObjectVsBroadPhaseLayerFilter;
static ObjectLayerPairFilterImpl sObjectLayerPairFilter;
static CharacterBroadPhaseLayerFilterImpl sCharacterBroadPhaseLayerFilter;
static CharacterObjectLayerFilterImpl sCharacterObjectLayerFilter;
static CharacterBodyFilterImpl sCharacterBodyFilter;
static CharacterShapeFilterImpl sCharacterShapeFilter;

PhysicsWorld::PhysicsWorld()
{
    RegisterDefaultAllocator();

    Trace = TraceImpl;
#ifdef JPH_ENABLE_ASSERTS
    AssertFailed = AssertFailedImpl;
#endif

    Factory::sInstance = new Factory();
    RegisterTypes();

    tempAllocator = std::make_unique<TempAllocatorImpl>(10 * 1024 * 1024);

#ifndef __EMSCRIPTEN__
    const uint32 numThreads = std::max(1u, std::thread::hardware_concurrency());

    jobSystem = std::make_unique<JobSystemThreadPool>(
        cMaxPhysicsJobs,
        cMaxPhysicsBarriers,
        numThreads > 1 ? numThreads - 1 : 1
    );
#else
    jobSystem = std::make_unique<JobSystemSingleThreaded>(
        cMaxPhysicsJobs
    );
#endif

    const uint cMaxBodies = 8192;
    const uint cNumBodyMutexes = 0;
    const uint cMaxBodyPairs = 8192;
    const uint cMaxContactConstraints = 8192;

    physicsSystem.Init(
        cMaxBodies,
        cNumBodyMutexes,
        cMaxBodyPairs,
        cMaxContactConstraints,
        sBroadPhaseLayerInterface,
        sObjectVsBroadPhaseLayerFilter,
        sObjectLayerPairFilter
    );

    initialized = true;
}

PhysicsWorld::~PhysicsWorld()
{
    if (initialized)
    {
        UnregisterTypes();
        delete Factory::sInstance;
        Factory::sInstance = nullptr;
    }
}

void PhysicsWorld::Step(float dt)
{
    if (!initialized) return;

    constexpr int cCollisionSteps = 1;
    physicsSystem.Update(dt, cCollisionSteps, tempAllocator.get(), jobSystem.get());
}

BodyID PhysicsWorld::AddStaticBox(
    const Vector3& position,
    const Vector3& halfExtents,
    const Quaternion& rotation
)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        !std::isfinite(halfExtents.x) || !std::isfinite(halfExtents.y) || !std::isfinite(halfExtents.z))
    {
        TraceLog(LOG_WARNING, "AddStaticBox failed: non-finite position or half extents.");
        return BodyID();
    }

    const float minHalf = 0.005f;

    if (halfExtents.x < minHalf || halfExtents.y < minHalf || halfExtents.z < minHalf)
    {
        TraceLog(
            LOG_WARNING,
            "AddStaticBox skipped: invalid half extents %.4f %.4f %.4f",
            halfExtents.x,
            halfExtents.y,
            halfExtents.z
        );

        return BodyID();
    }

    BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();

    BoxShapeSettings shapeSettings(
        Vec3(
            halfExtents.x,
            halfExtents.y,
            halfExtents.z
        )
    );

    ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();

    if (shapeResult.HasError())
    {
        TraceLog(
            LOG_WARNING,
            "AddStaticBox failed to create shape: %s",
            shapeResult.GetError().c_str()
        );

        return BodyID();
    }

    RefConst<Shape> shape = shapeResult.Get();

    BodyCreationSettings settings(
        shape,
        RVec3(position.x, position.y, position.z),
        JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
        EMotionType::Static,
        Layers::NON_MOVING
    );

    BodyID id = bodyInterface.CreateAndAddBody(settings, EActivation::DontActivate);

    if (id.IsInvalid())
    {
        TraceLog(LOG_WARNING, "AddStaticBox failed: CreateAndAddBody returned invalid BodyID.");
    }

    return id;
}

BodyID PhysicsWorld::AddDynamicBox(const Vector3& position, const Vector3& halfExtents)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        !std::isfinite(halfExtents.x) || !std::isfinite(halfExtents.y) || !std::isfinite(halfExtents.z))
    {
        TraceLog(LOG_WARNING, "AddDynamicBox failed: non-finite position or half extents.");
        return BodyID();
    }

    const float minHalf = 0.005f;

    if (halfExtents.x < minHalf || halfExtents.y < minHalf || halfExtents.z < minHalf)
    {
        TraceLog(
            LOG_WARNING,
            "AddDynamicBox skipped: invalid half extents %.4f %.4f %.4f",
            halfExtents.x,
            halfExtents.y,
            halfExtents.z
        );

        return BodyID();
    }

    BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();

    BoxShapeSettings shapeSettings(
        Vec3(
            halfExtents.x,
            halfExtents.y,
            halfExtents.z
        )
    );

    ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();

    if (shapeResult.HasError())
    {
        TraceLog(
            LOG_WARNING,
            "AddDynamicBox failed to create shape: %s",
            shapeResult.GetError().c_str()
        );

        return BodyID();
    }

    RefConst<Shape> shape = shapeResult.Get();

    BodyCreationSettings settings(
        shape,
        RVec3(position.x, position.y, position.z),
        Quat::sIdentity(),
        EMotionType::Dynamic,
        Layers::MOVING
    );

    return bodyInterface.CreateAndAddBody(settings, EActivation::Activate);
}

JPH::BodyID PhysicsWorld::AddKinematicBox(
    const Vector3& position,
    const Vector3& halfExtents,
    const Quaternion& rotation
)
{
    JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();

    JPH::BoxShapeSettings boxShapeSettings(
        JPH::Vec3(
            halfExtents.x,
            halfExtents.y,
            halfExtents.z
        )
    );

    JPH::ShapeSettings::ShapeResult shapeResult = boxShapeSettings.Create();

    if (shapeResult.HasError())
    {
        TraceLog(
            LOG_WARNING,
            "Failed to create kinematic box shape: %s",
            shapeResult.GetError().c_str()
        );

        return JPH::BodyID();
    }

    JPH::ShapeRefC shape = shapeResult.Get();

    JPH::Quat joltRotation(
        rotation.x,
        rotation.y,
        rotation.z,
        rotation.w
    );

    JPH::BodyCreationSettings bodySettings(
        shape,
        JPH::Vec3(position.x, position.y, position.z),
        joltRotation,
        JPH::EMotionType::Kinematic,
        Layers::MOVING // change this if your project uses a different layer name
    );

    bodySettings.mGravityFactor = 0.0f;

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);

    if (body == nullptr)
    {
        TraceLog(LOG_WARNING, "Failed to create kinematic customer body.");
        return JPH::BodyID();
    }

    JPH::BodyID bodyId = body->GetID();

    bodyInterface.AddBody(bodyId, JPH::EActivation::Activate);

    return bodyId;
}

void PhysicsWorld::MoveKinematicBody(
    JPH::BodyID id,
    const Vector3& position,
    const Quaternion& rotation,
    float dt
)
{
    if (id.IsInvalid())
        return;

    JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();

    JPH::Quat joltRotation(
        rotation.x,
        rotation.y,
        rotation.z,
        rotation.w
    );

    bodyInterface.MoveKinematic(
        id,
        JPH::Vec3(position.x, position.y, position.z),
        joltRotation,
        dt
    );
}

void PhysicsWorld::RemoveBody(JPH::BodyID id)
{
    if (id.IsInvalid())
        return;

    JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();

    bodyInterface.RemoveBody(id);
    bodyInterface.DestroyBody(id);
}

Vector3 PhysicsWorld::GetBodyPosition(BodyID id) const
{
    if (id.IsInvalid())
        return { 0.0f, 0.0f, 0.0f };

    const BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
    RVec3 p = bodyInterface.GetCenterOfMassPosition(id);

    return {
        static_cast<float>(p.GetX()),
        static_cast<float>(p.GetY()),
        static_cast<float>(p.GetZ())
    };
}

void PhysicsWorld::SetBodyPosition(BodyID id, const Vector3& position)
{
    if (id.IsInvalid()) return;

    BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
    bodyInterface.SetPosition(id, RVec3(position.x, position.y, position.z), EActivation::Activate);
}

void PhysicsWorld::SetBodyLinearVelocity(BodyID id, const Vector3& velocity)
{
    if (id.IsInvalid()) return;

    BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
    bodyInterface.SetLinearVelocity(id, Vec3(velocity.x, velocity.y, velocity.z));
}

void PhysicsWorld::SetBodyMotionType(BodyID id, EMotionType motionType)
{
    if (id.IsInvalid()) return;

    BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
    bodyInterface.SetMotionType(id, motionType, EActivation::Activate);
}
Quaternion PhysicsWorld::GetBodyRotation(BodyID id) const
{
    if (id.IsInvalid()) return { 0, 0, 0, 1 };

    const BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
    Quat q = bodyInterface.GetRotation(id);

    return {
        q.GetX(),
        q.GetY(),
        q.GetZ(),
        q.GetW()
    };
}

void PhysicsWorld::SetBodyRotation(BodyID id, const Quaternion& q)
{
    if (id.IsInvalid()) return;

    BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
    bodyInterface.SetRotation(id, JPH::Quat(q.x, q.y, q.z, q.w), EActivation::Activate);
}

JPH::PhysicsSystem* PhysicsWorld::GetPhysicsSystem()
{
    return &physicsSystem;
}


void PhysicsWorld::UpdateCharacterVirtual(JPH::CharacterVirtual& character, float dt, const JPH::Vec3& gravity)
{
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;

    character.ExtendedUpdate(
        dt,
        gravity,
        settings,
        sCharacterBroadPhaseLayerFilter,
        sCharacterObjectLayerFilter,
        sCharacterBodyFilter,
        sCharacterShapeFilter,
        *tempAllocator
    );
}

void PhysicsWorld::SetBodyAngularVelocity(JPH::BodyID id, const Vector3& velocity)
{
    auto& bi = physicsSystem.GetBodyInterface();
    bi.SetAngularVelocity(id, JPH::Vec3(velocity.x, velocity.y, velocity.z));
}

void PhysicsWorld::SetBodyGravityFactor(JPH::BodyID id, float factor)
{
    if (id.IsInvalid())
        return;

    auto& bi = physicsSystem.GetBodyInterface();
    bi.SetGravityFactor(id, factor);
}


void PhysicsWorld::SetBodyIsSensor(JPH::BodyID id, bool isSensor)
{
    if (id.IsInvalid())
        return;

    auto& bi = physicsSystem.GetBodyInterface();
    bi.SetIsSensor(id, isSensor);
}
