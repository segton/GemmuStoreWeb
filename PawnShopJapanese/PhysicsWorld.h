#pragma once

#include "raylib.h"
#include <memory>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystem.h>

#ifndef __EMSCRIPTEN__
#include <Jolt/Core/JobSystemThreadPool.h>
#else
#include <Jolt/Core/JobSystemSingleThreaded.h>
#endif
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

class PhysicsWorld
{
public:
    PhysicsWorld();
    ~PhysicsWorld();

    void Step(float dt);

    JPH::BodyID AddStaticBox(const Vector3& position, const Vector3& halfExtents, const Quaternion& rotation);
    JPH::BodyID AddDynamicBox(const Vector3& position, const Vector3& halfExtents);

    JPH::BodyID AddKinematicBox(
        const Vector3& position,
        const Vector3& halfExtents,
        const Quaternion& rotation
    );

    void MoveKinematicBody(
        JPH::BodyID id,
        const Vector3& position,
        const Quaternion& rotation,
        float dt
    );

    void RemoveBody(JPH::BodyID id);

    Vector3 GetBodyPosition(JPH::BodyID id) const;

    void SetBodyPosition(JPH::BodyID id, const Vector3& position);
    void SetBodyLinearVelocity(JPH::BodyID id, const Vector3& velocity);
    void SetBodyMotionType(JPH::BodyID id, JPH::EMotionType motionType);
    Quaternion GetBodyRotation(JPH::BodyID id) const;
    void SetBodyRotation(JPH::BodyID id, const Quaternion& q);

    JPH::PhysicsSystem* GetPhysicsSystem();
    void UpdateCharacterVirtual(JPH::CharacterVirtual& character, float dt, const JPH::Vec3& gravity);
    void SetBodyAngularVelocity(JPH::BodyID id, const Vector3& velocity);

    void SetBodyGravityFactor(JPH::BodyID id, float factor);
    void SetBodyIsSensor(JPH::BodyID id, bool isSensor);

private:
    bool initialized = false;

    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystem> jobSystem;
    JPH::PhysicsSystem physicsSystem;
};