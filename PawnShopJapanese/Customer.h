#pragma once

#include "raylib.h"
#include "raymath.h"
#include <string>
#include <cmath>
#include "rlgl.h"


static void DrawCustomerModelWithShader(
    Model& model,
    Vector3 position,
    Vector3 rotationAxis,
    float rotationAngle,
    Vector3 scale,
    Shader shader
)
{
    std::vector<Shader> oldShaders;
    oldShaders.resize(model.materialCount);

    for (int i = 0; i < model.materialCount; i++)
    {
        oldShaders[i] = model.materials[i].shader;
        model.materials[i].shader = shader;
    }

    DrawModelEx(model, position, rotationAxis, rotationAngle, scale, WHITE);

    for (int i = 0; i < model.materialCount; i++)
    {
        model.materials[i].shader = oldShaders[i];
    }
}

static void RecalculateAnimatedNormals(Model& model)
{
    for (int m = 0; m < model.meshCount; m++)
    {
        Mesh& mesh = model.meshes[m];

        if (!mesh.animVertices)
            continue;

        if (!mesh.animNormals)
        {
            mesh.animNormals = (float*)MemAlloc(mesh.vertexCount * 3 * sizeof(float));
        }

        // Clear normals
        for (int i = 0; i < mesh.vertexCount * 3; i++)
        {
            mesh.animNormals[i] = 0.0f;
        }

        // Accumulate face normals into each vertex normal
        for (int tri = 0; tri < mesh.triangleCount; tri++)
        {
            int i0;
            int i1;
            int i2;

            if (mesh.indices)
            {
                i0 = mesh.indices[tri * 3 + 0];
                i1 = mesh.indices[tri * 3 + 1];
                i2 = mesh.indices[tri * 3 + 2];
            }
            else
            {
                i0 = tri * 3 + 0;
                i1 = tri * 3 + 1;
                i2 = tri * 3 + 2;
            }

            Vector3 p0 = {
                mesh.animVertices[i0 * 3 + 0],
                mesh.animVertices[i0 * 3 + 1],
                mesh.animVertices[i0 * 3 + 2]
            };

            Vector3 p1 = {
                mesh.animVertices[i1 * 3 + 0],
                mesh.animVertices[i1 * 3 + 1],
                mesh.animVertices[i1 * 3 + 2]
            };

            Vector3 p2 = {
                mesh.animVertices[i2 * 3 + 0],
                mesh.animVertices[i2 * 3 + 1],
                mesh.animVertices[i2 * 3 + 2]
            };

            Vector3 e1 = Vector3Subtract(p1, p0);
            Vector3 e2 = Vector3Subtract(p2, p0);

            Vector3 faceNormal = Vector3CrossProduct(e1, e2);

            if (Vector3Length(faceNormal) <= 0.000001f)
                continue;

            faceNormal = Vector3Normalize(faceNormal);

            int ids[3] = { i0, i1, i2 };

            for (int k = 0; k < 3; k++)
            {
                int i = ids[k];

                mesh.animNormals[i * 3 + 0] += faceNormal.x;
                mesh.animNormals[i * 3 + 1] += faceNormal.y;
                mesh.animNormals[i * 3 + 2] += faceNormal.z;
            }
        }

        // Normalize accumulated normals
        for (int i = 0; i < mesh.vertexCount; i++)
        {
            Vector3 n = {
                mesh.animNormals[i * 3 + 0],
                mesh.animNormals[i * 3 + 1],
                mesh.animNormals[i * 3 + 2]
            };

            if (Vector3Length(n) <= 0.000001f)
            {
                n = { 0.0f, 1.0f, 0.0f };
            }
            else
            {
                n = Vector3Normalize(n);
            }

            mesh.animNormals[i * 3 + 0] = n.x;
            mesh.animNormals[i * 3 + 1] = n.y;
            mesh.animNormals[i * 3 + 2] = n.z;
        }

        // Upload animated normals to GPU.
        // raylib normal buffer index is 2.
        UpdateMeshBuffer(
            mesh,
            2,
            mesh.animNormals,
            mesh.vertexCount * 3 * sizeof(float),
            0
        );
    }
}

enum class CustomerAnimState
{
    Idle,
    Walking,
    Emote,
    Point,
    LeftHand,
    LeftLook,
    Think,
    Give,
    Dance,
    Twerk
};
enum class CustomerDialogueAnim
{
    None,
    Talk,
    Think,
    PresentItem,
    Agree,
    Disagree
};


struct CustomerAnimSet
{
    ModelAnimation* animations = nullptr;
    int animationCount = 0;

    int idleIndex = 0;
    int walkIndex = 1;
    int emoteIndex = 2;
    int pointIndex = 3;
    int leftHandIndex = 4;
    int leftLookIndex = 5;
    int thinkIndex = 6;
    int giveIndex = 7;
    int danceIndex = 8;
    int twerkIndex = 9;
};

enum class CustomerAIState
{
    None,

    Entering,

    BrowserBrowsing,
    BrowserGoingToItem,
    BrowserInspectingItem,
    BrowserGoingToQueue,
    BrowserQueueing,
    BrowserAtCounter,
    BrowserPurchasing,

    SellerGoingToQueue,
    SellerQueueing,
    SellerAtCounter,
    SellerSelling,

    Leaving,
    Despawning
};

enum class CustomerRole
{
    Browser,
    Seller
};

class Customer
{
public:



    Model* model = nullptr;

    Vector3 position{ 0.0f, 0.0f, 0.0f };
    Vector3 targetPosition{ 0.0f, 0.0f, 0.0f };

    float yawDeg = 0.0f;
    float moveSpeed = 0.75f;

    bool hasMoveTarget = false;

    CustomerAnimState animState = CustomerAnimState::Idle;

    Vector3 scale{ 0.01f, 0.01f, 0.01f };
    // Default for your old human/Mixamo-style models.
    Vector3 drawRotationAxis{ 1.0f, 0.0f, 0.0f };
    float drawRotationAngleDeg = 90.0f;

    bool showDebug = false;
    float animFrame = 0.0f;
    float animFPS = 60.f;

    CustomerAnimSet* animSet = nullptr;

    std::string dialogueScriptId = "default_customer";

    std::string poiGroup = "any";



    float pbrMetallicValue = 0.0f;
    float pbrRoughnessValue = 0.65f;
    float pbrRoughnessScale = 1.0f;
    float pbrReflectionStrength = 0.0f;
    float pbrEmissivePower = 3.0f;

    bool usePOINavigation = true;

    int currentPOIIndex = -1;
    int targetPOIIndex = -1;
    int destinationPOIIndex = -1;

    std::vector<int> poiRoute;
    int poiRouteCursor = 0;

    float poiWaitTimer = 0.0f;

    float turnSpeedDeg = 360.0f;
    float moveStopDistance = 0.08f;

    std::vector<Vector3> pathWaypoints;
    int pathWaypointCursor = 0;

    float movementPauseTimer = 0.0f;
    float repathTimer = 0.0f;

    bool editorFrozen = false;

    CustomerRole role = CustomerRole::Browser;
    CustomerAIState aiState = CustomerAIState::None;

    int assignedItemPOIIndex = -1;
    int assignedQueueSlotIndex = -1;

    bool hasPickedItem = false;
    bool pendingDespawn = false;

    int browseVisitsRemaining = 0;

    std::string customerTypeId;

    // Trade item carried/sold/bought by this customer.
    std::string tradeItemId = "";

    int carriedItemScenePropIndex = -1;
    int counterItemScenePropIndex = -1;

    bool counterItemPlaced = false;
    bool counterItemPlacementAttempted = false;
    bool tradeItemChosen = false;

    bool counterServiceCompleted = false;
    bool waitingForPlayerToTakeCounterItem = false;
    bool transactionApplied = false;

    bool waitingForPlayerToScanItem = false;
    bool waitingForPlayerToReturnScannedItem = false;

public:


    Customer() = default;

    Customer(Model* customerModel, Vector3 startPos, CustomerAnimSet* set)
        : model(customerModel), position(startPos), targetPosition(startPos), animSet(set)
    {
    }

    void MoveTo(Vector3 newTarget)
    {
        targetPosition = newTarget;
        hasMoveTarget = true;
        SetAnimState(CustomerAnimState::Walking);
    }

    int GetAnimIndexForState(CustomerAnimState state) const
    {
        if (animSet == nullptr)
            return -1;

        int index = -1;

        switch (state)
        {
        case CustomerAnimState::Idle:
            index = animSet->idleIndex;
            break;

        case CustomerAnimState::Walking:
            index = animSet->walkIndex;
            break;

        case CustomerAnimState::Emote:
            index = animSet->emoteIndex;
            break;

        case CustomerAnimState::Point:
            index = animSet->pointIndex;
            break;

        case CustomerAnimState::LeftHand:
            index = animSet->leftHandIndex;
            break;

        case CustomerAnimState::LeftLook:
            index = animSet->leftLookIndex;
            break;

        case CustomerAnimState::Think:
            index = animSet->thinkIndex;
            break;

        case CustomerAnimState::Give:
            index = animSet->giveIndex;
            break;

        case CustomerAnimState::Dance:
            index = animSet->danceIndex;
            break;

        case CustomerAnimState::Twerk:
            index = animSet->twerkIndex;
            break;
        }

        return GetSafeAnimIndex(index);
    }

    void FaceTowards(Vector3 target, float dt, float speedDeg = 360.0f)
    {
        Vector3 toTarget = Vector3Subtract(target, position);
        toTarget.y = 0.0f;

        if (Vector3Length(toTarget) <= 0.001f)
            return;

        Vector3 dir = Vector3Normalize(toTarget);
        float desiredYaw = atan2f(dir.x, dir.z) * RAD2DEG;

        yawDeg = MoveTowardsAngleDeg(
            yawDeg,
            desiredYaw,
            speedDeg * dt
        );
    }

    void PlayAnimState(CustomerAnimState state)
    {
        if (IsLoopingAnimState(state))
        {
            oneShotAnimationActive = false;
            SetAnimState(state);
        }
        else
        {
            PlayOneShotAnimation(state);
        }
    }

    void SetAnimState(CustomerAnimState newState)
    {
        // Prevent repeated calls from restarting the same blend every frame.
        if (!isBlending && animState == newState)
            return;

        // If already blending toward this state, do not restart the blend.
        if (isBlending && targetAnimState == newState)
            return;

        StartAnimBlend(newState, 0.22f, false);
    }

    void StartAnimBlend(
        CustomerAnimState newState,
        float duration,
        bool freezePreviousFrame
    )
    {
        // If already fully in this state, no need to restart.
        if (!isBlending && animState == newState)
            return;

        // If already blending toward this exact state, do not restart.
        if (isBlending && targetAnimState == newState)
            return;

        CustomerAnimState sourceState = animState;
        float sourceFrame = animFrame;

        // Important:
        // If we interrupt an existing blend, the visible animation is closer to
        // the current target animation than animFrame. Use targetAnimFrame instead.
        if (isBlending)
        {
            sourceState = targetAnimState;
            sourceFrame = targetAnimFrame;
        }

        previousAnimState = sourceState;

        int previousIndex = GetAnimIndexForState(previousAnimState);

        if (previousIndex >= 0 &&
            animSet != nullptr &&
            previousIndex < animSet->animationCount)
        {
            ModelAnimation& previousAnim = animSet->animations[previousIndex];

            int previousPlayableFrameCount =
                GetPlayableFrameCount(previousAnimState, previousAnim);

            previousAnimFrame = sourceFrame;
            WrapAnimFrame(previousAnimFrame, previousPlayableFrameCount);
        }
        else
        {
            previousAnimFrame = 0.0f;
        }

        targetAnimState = newState;
        targetAnimFrame = 0.0f;

        animState = newState;
        animFrame = 0.0f;

        isBlending = true;
        freezePreviousAnimDuringBlend = freezePreviousFrame;

        blendTimer = 0.0f;
        blendDuration = fmaxf(duration, 0.01f);
    }

    void PlayEmote()
    {
        PlayOneShotAnimation(CustomerAnimState::Emote);
    }

    void PlayOneShotAnimation(
        CustomerAnimState state,
        float blendInDuration = 0.26f
    )
    {
        returnStateAfterOneShot = hasMoveTarget
            ? CustomerAnimState::Walking
            : CustomerAnimState::Idle;

        oneShotAnimationActive = true;

        StartAnimBlend(
            state,
            blendInDuration,
            false
        );
    }

    int GetSafeAnimIndex(int preferredIndex) const
    {
        if (animSet == nullptr)
            return -1;

        if (animSet->animationCount <= 0)
            return -1;

        if (preferredIndex >= 0 &&
            preferredIndex < animSet->animationCount)
        {
            return preferredIndex;
        }

        if (animSet->idleIndex >= 0 &&
            animSet->idleIndex < animSet->animationCount)
        {
            return animSet->idleIndex;
        }

        return 0;
    }

    void DrawAnimatedVertexDebug() const
    {
        if (!model || model->meshCount <= 0)
            return;

        const Mesh& mesh = model->meshes[0];

        const float* verts = mesh.animVertices ? mesh.animVertices : mesh.vertices;
        if (!verts)
            return;

        Vector3 minP{ FLT_MAX, FLT_MAX, FLT_MAX };
        Vector3 maxP{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

        int drawn = 0;
        int bad = 0;

        for (int i = 0; i < mesh.vertexCount; i++)
        {
            float x = verts[i * 3 + 0];
            float y = verts[i * 3 + 1];
            float z = verts[i * 3 + 2];

            if (!isfinite(x) || !isfinite(y) || !isfinite(z))
            {
                bad++;
                continue;
            }

            Vector3 p = {
                position.x + x,
                position.y + y,
                position.z + z
            };

            minP.x = fminf(minP.x, p.x);
            minP.y = fminf(minP.y, p.y);
            minP.z = fminf(minP.z, p.z);

            maxP.x = fmaxf(maxP.x, p.x);
            maxP.y = fmaxf(maxP.y, p.y);
            maxP.z = fmaxf(maxP.z, p.z);

            if (i % 250 == 0)
            {
                DrawSphere(p, 0.08f, RED);
                drawn++;
            }
        }

        static bool logged = false;
        if (!logged)
        {
            TraceLog(
                LOG_INFO,
                "ANIM VERTEX WORLD BOUNDS min(%.3f %.3f %.3f), max(%.3f %.3f %.3f), drawn=%i, bad=%i",
                minP.x, minP.y, minP.z,
                maxP.x, maxP.y, maxP.z,
                drawn,
                bad
            );

            logged = true;
        }

        BoundingBox animBox{};
        animBox.min = minP;
        animBox.max = maxP;
        DrawBoundingBox(animBox, RED);
    }

    void ApplyDialogueCue(CustomerAnimState cueAnim)
    {
        switch (cueAnim)
        {
        case CustomerAnimState::Idle:
        case CustomerAnimState::Walking:
        case CustomerAnimState::Dance:
        case CustomerAnimState::Twerk:
            oneShotAnimationActive = false;
            SetAnimState(cueAnim);
            break;

        case CustomerAnimState::Emote:
        case CustomerAnimState::Point:
        case CustomerAnimState::LeftHand:
        case CustomerAnimState::LeftLook:
        case CustomerAnimState::Think:
        case CustomerAnimState::Give:
            PlayOneShotAnimation(cueAnim, 0.28f);
            break;
        }
    }

    void ForceAnimState(CustomerAnimState newState)
    {
        oneShotAnimationActive = false;

        isBlending = false;
        freezePreviousAnimDuringBlend = false;

        previousAnimState = newState;
        targetAnimState = newState;
        animState = newState;

        animFrame = 0.0f;
        previousAnimFrame = 0.0f;
        targetAnimFrame = 0.0f;

        blendTimer = 0.0f;
    }

    void EndDialogue()
    {
        CustomerAnimState returnState = hasMoveTarget
            ? CustomerAnimState::Walking
            : CustomerAnimState::Idle;

        oneShotAnimationActive = false;

        // Blend out of the current dialogue pose instead of snapping.
        // freezePreviousFrame=true keeps the final dialogue pose stable
        // while fading back to idle/walk.
        StartAnimBlend(
            returnState,
            0.30f,
            true
        );
    }

    void LogAnimatedVertexBoundsOncePerSecond()
    {
        if (!model || model->meshCount <= 0)
            return;

        Mesh& mesh = model->meshes[0];

        const float* verts = mesh.animVertices ? mesh.animVertices : mesh.vertices;
        if (!verts)
        {
            TraceLog(LOG_WARNING, "Customer has no vertices or animVertices.");
            return;
        }

        Vector3 minP{ FLT_MAX, FLT_MAX, FLT_MAX };
        Vector3 maxP{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

        int bad = 0;

        for (int i = 0; i < mesh.vertexCount; i++)
        {
            float x = verts[i * 3 + 0];
            float y = verts[i * 3 + 1];
            float z = verts[i * 3 + 2];

            if (!isfinite(x) || !isfinite(y) || !isfinite(z))
            {
                bad++;
                continue;
            }

            Vector3 p = {
                position.x + x * scale.x,
                position.y + y * scale.y,
                position.z + z * scale.z
            };

            minP.x = fminf(minP.x, p.x);
            minP.y = fminf(minP.y, p.y);
            minP.z = fminf(minP.z, p.z);

            maxP.x = fmaxf(maxP.x, p.x);
            maxP.y = fmaxf(maxP.y, p.y);
            maxP.z = fmaxf(maxP.z, p.z);
        }

        TraceLog(
            LOG_INFO,
            "CUSTOMER ANIM FRAME %.2f | WORLD BOUNDS min(%.3f %.3f %.3f), max(%.3f %.3f %.3f), bad=%i",
            animFrame,
            minP.x, minP.y, minP.z,
            maxP.x, maxP.y, maxP.z,
            bad
        );
    }
    void Update(float dt)
    {
        UpdateMovement(dt);
        UpdateAnimation(dt);
    }
    BoundingBox GetScaledBounds() const
    {
        BoundingBox local = GetModelBoundingBox(*model);

        BoundingBox scaled{};
        scaled.min = {
            position.x + local.min.x * scale.x,
            position.y + local.min.y * scale.y,
            position.z + local.min.z * scale.z
        };

        scaled.max = {
            position.x + local.max.x * scale.x,
            position.y + local.max.y * scale.y,
            position.z + local.max.z * scale.z
        };

        return scaled;
    }
    bool CanTalk() const
    {
        return animState == CustomerAnimState::Idle && !hasMoveTarget;
    }

    BoundingBox GetTalkBounds() const
    {
        BoundingBox box{};

        box.min = {
            position.x - 0.45f,
            position.y + 0.10f,
            position.z - 0.45f
        };

        box.max = {
            position.x + 0.45f,
            position.y + 1.85f,
            position.z + 0.45f
        };

        return box;
    }
    void DrawAnimatedWireCPU() const
    {
        if (!model || model->meshCount <= 0)
            return;

        const Mesh& mesh = model->meshes[0];

        const float* verts = mesh.animVertices ? mesh.animVertices : mesh.vertices;
        if (!verts)
            return;

        int triLimit = mesh.triangleCount;
        if (triLimit > 1000) triLimit = 1000; // limit for debug performance

        for (int t = 0; t < triLimit; t++)
        {
            int i0;
            int i1;
            int i2;

            if (mesh.indices)
            {
                i0 = mesh.indices[t * 3 + 0];
                i1 = mesh.indices[t * 3 + 1];
                i2 = mesh.indices[t * 3 + 2];
            }
            else
            {
                i0 = t * 3 + 0;
                i1 = t * 3 + 1;
                i2 = t * 3 + 2;
            }

            Vector3 p0 = {
                position.x + verts[i0 * 3 + 0] * scale.x,
                position.y + verts[i0 * 3 + 1] * scale.y,
                position.z + verts[i0 * 3 + 2] * scale.z
            };

            Vector3 p1 = {
                position.x + verts[i1 * 3 + 0] * scale.x,
                position.y + verts[i1 * 3 + 1] * scale.y,
                position.z + verts[i1 * 3 + 2] * scale.z
            };

            Vector3 p2 = {
                position.x + verts[i2 * 3 + 0] * scale.x,
                position.y + verts[i2 * 3 + 1] * scale.y,
                position.z + verts[i2 * 3 + 2] * scale.z
            };

            DrawLine3D(p0, p1, LIME);
            DrawLine3D(p1, p2, LIME);
            DrawLine3D(p2, p0, LIME);
        }
    }

    void DrawWithShader(Shader shader)
    {
        DrawInternal(&shader);
    }

    void Draw()
    {
        DrawInternal(nullptr);
    }

private:


    bool oneShotAnimationActive = false;
    CustomerAnimState returnStateAfterOneShot = CustomerAnimState::Idle;

    CustomerAnimState previousAnimState = CustomerAnimState::Idle;
    CustomerAnimState targetAnimState = CustomerAnimState::Idle;

    float previousAnimFrame = 0.0f;
    float targetAnimFrame = 0.0f;

    bool isBlending = false;
    float blendTimer = 0.0f;
    float blendDuration = 0.20f;
    bool freezePreviousAnimDuringBlend = false;


    static float NormalizeAngleDeg(float angle)
    {
        while (angle > 180.0f)
            angle -= 360.0f;

        while (angle < -180.0f)
            angle += 360.0f;

        return angle;
    }

    static float MoveTowardsAngleDeg(float current, float target, float maxDelta)
    {
        float delta = NormalizeAngleDeg(target - current);

        if (fabsf(delta) <= maxDelta)
            return target;

        return current + ((delta > 0.0f) ? maxDelta : -maxDelta);
    }

    static void QuaternionToAxisAngleSafeLocal(
        Quaternion q,
        Vector3& axis,
        float& angleDeg
    )
    {
        q = QuaternionNormalize(q);

        float angle = 2.0f * acosf(q.w);
        float s = sqrtf(1.0f - q.w * q.w);

        if (s < 0.0001f)
        {
            axis = { 0.0f, 1.0f, 0.0f };
        }
        else
        {
            axis = {
                q.x / s,
                q.y / s,
                q.z / s
            };
        }

        angleDeg = angle * RAD2DEG;
    }

    void GetDrawRotation(Vector3& axis, float& angleDeg) const
    {
        Quaternion modelBaseRotation = QuaternionIdentity();

        if (fabsf(drawRotationAngleDeg) > 0.0001f)
        {
            modelBaseRotation = QuaternionFromAxisAngle(
                Vector3Normalize(drawRotationAxis),
                drawRotationAngleDeg * DEG2RAD
            );
        }

        Quaternion yawRotation = QuaternionFromAxisAngle(
            Vector3{ 0.0f, 1.0f, 0.0f },
            yawDeg * DEG2RAD
        );

        Quaternion finalRotation = QuaternionMultiply(
            yawRotation,
            modelBaseRotation
        );

        QuaternionToAxisAngleSafeLocal(
            finalRotation,
            axis,
            angleDeg
        );
    }

    void DrawInternal(Shader* overrideShader)
    {
        if (!model) return;

        rlDisableBackfaceCulling();

        Vector3 drawAxis;
        float drawAngleDeg;

        GetDrawRotation(drawAxis, drawAngleDeg);

        if (overrideShader)
        {
            DrawCustomerModelWithShader(
                *model,
                position,
                drawAxis,
                drawAngleDeg,
                scale,
                *overrideShader
            );
        }
        else
        {
            DrawModelEx(
                *model,
                position,
                drawAxis,
                drawAngleDeg,
                scale,
                WHITE
            );
        }

        rlEnableBackfaceCulling();
    }
    void UpdateMovement(float dt)
    {
        if (!hasMoveTarget)
            return;


        if (movementPauseTimer > 0.0f)
        {
            movementPauseTimer -= dt;

            if (animState != CustomerAnimState::Idle)
            {
                SetAnimState(CustomerAnimState::Idle);
            }

            return;
        }

        if (hasMoveTarget && animState != CustomerAnimState::Walking)
        {
            SetAnimState(CustomerAnimState::Walking);
        }

        Vector3 toTarget = Vector3Subtract(targetPosition, position);
        toTarget.y = 0.0f;

        float distance = Vector3Length(toTarget);

        if (distance <= moveStopDistance)
        {
            hasMoveTarget = false;
            SetAnimState(CustomerAnimState::Idle);
            return;
        }

        Vector3 dir = Vector3Normalize(toTarget);

        float desiredYaw = atan2f(dir.x, dir.z) * RAD2DEG;

        yawDeg = MoveTowardsAngleDeg(
            yawDeg,
            desiredYaw,
            turnSpeedDeg * dt
        );

        float step = moveSpeed * dt;

        // Prevent overshooting into the target and causing a visible snap.
        if (step > distance - moveStopDistance)
        {
            step = fmaxf(0.0f, distance - moveStopDistance);
        }

        position = Vector3Add(
            position,
            Vector3Scale(dir, step)
        );
    }
    void UploadAnimatedMeshToGPU()
    {
        if (!model || model->meshCount <= 0)
            return;

        Mesh& mesh = model->meshes[0];

        if (mesh.animVertices)
        {
            UpdateMeshBuffer(
                mesh,
                0,
                mesh.animVertices,
                mesh.vertexCount * 3 * sizeof(float),
                0
            );
        }

        if (mesh.animNormals)
        {
            UpdateMeshBuffer(
                mesh,
                2,
                mesh.animNormals,
                mesh.vertexCount * 3 * sizeof(float),
                0
            );
        }
    }

    bool ShouldSkipFinalLoopFrame(CustomerAnimState state) const
    {
        switch (state)
        {
        case CustomerAnimState::Walking:
        case CustomerAnimState::Dance:
        case CustomerAnimState::Twerk:
            return true;

        default:
            return false;
        }
    }

    int GetPlayableFrameCount(CustomerAnimState state, const ModelAnimation& anim) const
    {
        int frameCount = anim.keyframeCount;

        if (ShouldSkipFinalLoopFrame(state) && frameCount > 2)
        {
            return frameCount - 2;
        }

        return frameCount;
    }

    bool IsLoopingAnimState(CustomerAnimState state) const
    {
        switch (state)
        {
        case CustomerAnimState::Idle:
        case CustomerAnimState::Walking:
        case CustomerAnimState::Dance:
        case CustomerAnimState::Twerk:
            return true;

        default:
            return false;
        }
    }

    bool IsOneShotAnimState(CustomerAnimState state) const
    {
        return !IsLoopingAnimState(state);
    }

    void WrapAnimFrame(float& frame, int playableFrameCount) const
    {
        if (playableFrameCount <= 0)
        {
            frame = 0.0f;
            return;
        }

        while (frame >= playableFrameCount)
        {
            frame -= playableFrameCount;
        }

        while (frame < 0.0f)
        {
            frame += playableFrameCount;
        }
    }

    void UpdateAnimation(float dt)
    {
        if (model == nullptr) return;
        if (animSet == nullptr) return;
        if (animSet->animations == nullptr) return;

        int currentIndex = GetAnimIndexForState(animState);

        if (currentIndex < 0 || currentIndex >= animSet->animationCount)
            return;

        ModelAnimation& currentAnim = animSet->animations[currentIndex];

        if (!IsModelAnimationValid(*model, currentAnim))
            return;

        int currentFrameCount = currentAnim.keyframeCount;

        if (currentFrameCount <= 0)
            return;

        // ---------------------------------------------------------
        // BLENDING PATH
        // ---------------------------------------------------------
        if (isBlending)
        {
            int previousIndex = GetAnimIndexForState(previousAnimState);

            if (previousIndex < 0 || previousIndex >= animSet->animationCount)
            {
                isBlending = false;
                UpdateModelAnimation(*model, currentAnim, animFrame);
                return;
            }

            ModelAnimation& previousAnim = animSet->animations[previousIndex];

            if (!IsModelAnimationValid(*model, previousAnim))
            {
                isBlending = false;
                UpdateModelAnimation(*model, currentAnim, animFrame);
                return;
            }

            int previousFrameCount = previousAnim.keyframeCount;

            if (previousFrameCount <= 0)
            {
                isBlending = false;
                UpdateModelAnimation(*model, currentAnim, animFrame);
                return;
            }
            int previousPlayableFrameCount =
                GetPlayableFrameCount(previousAnimState, previousAnim);

            int currentPlayableFrameCount =
                GetPlayableFrameCount(animState, currentAnim);
            // Usually both animations advance.
            // But for Emote -> Idle, we freeze the previous animation
            // on the final emote pose so it crossfades properly.
            if (!freezePreviousAnimDuringBlend)
            {
                previousAnimFrame += animFPS * dt;

                WrapAnimFrame(previousAnimFrame, previousPlayableFrameCount);
            }

            targetAnimFrame += animFPS * dt;

            WrapAnimFrame(targetAnimFrame, currentPlayableFrameCount);

            blendTimer += dt;

            float t = blendTimer / blendDuration;

            if (t >= 1.0f)
            {
                t = 1.0f;
            }

            // Smoothstep easing.
            // This feels smoother than a raw linear blend.
            float blendFactor = t * t * (3.0f - 2.0f * t);

            UpdateModelAnimationEx(
                *model,
                previousAnim,
                previousAnimFrame,
                currentAnim,
                targetAnimFrame,
                blendFactor
            );

            if (t >= 1.0f)
            {
                isBlending = false;
                freezePreviousAnimDuringBlend = false;
                animFrame = targetAnimFrame;
            }

            return;
        }

        // ---------------------------------------------------------
        // NORMAL NON-BLENDING PATH
        // ---------------------------------------------------------
        int currentPlayableFrameCount =
            GetPlayableFrameCount(animState, currentAnim);

        animFrame += animFPS * dt;

        if (animFrame >= currentPlayableFrameCount)
        {
            if (oneShotAnimationActive)
            {
                oneShotAnimationActive = false;

                // For one-shot animations, hold the real final frame.
                // Do not use playableFrameCount here.
                animFrame = (float)(currentFrameCount - 1);

                StartAnimBlend(
                    returnStateAfterOneShot,
                    0.45f,
                    true
                );

                return;
            }

            WrapAnimFrame(animFrame, currentPlayableFrameCount);
        }

        UpdateModelAnimation(*model, currentAnim, animFrame);
    }

    int GetCurrentAnimIndex() const
    {
        switch (animState)
        {
        case CustomerAnimState::Idle:
            return animSet->idleIndex;

        case CustomerAnimState::Walking:
            return animSet->walkIndex;

        case CustomerAnimState::Emote:
            return animSet->emoteIndex;

        case CustomerAnimState::Point:
            return animSet->pointIndex;

        case CustomerAnimState::LeftHand:
            return animSet->leftHandIndex;
        
        case CustomerAnimState::LeftLook:
            return animSet->leftLookIndex;

        case CustomerAnimState::Think:
            return animSet->thinkIndex;
        }
       

        return animSet->idleIndex;
    }


};
