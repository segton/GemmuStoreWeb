#pragma once
#include "raylib.h"
#include "raymath.h"
#include <iostream>
#include "Settings.h"
#include <vector>
#include <functional>
#include <memory>
#include <cmath>

class BaseModel
{
public:
	Vector3 m_pos{};
	Model m_model{};
	bool m_despawn = false;
    Vector3 m_dir{};

protected:


	float m_speed{};
	BoundingBox m_box{};
public:

	BaseModel() = default;

	BaseModel(Model model, Vector3 pos, Vector3 dir, float speed) : m_model(model), m_pos(pos), m_dir(dir), m_speed(speed)
	{
		m_box = GetMeshBoundingBox(model.meshes[0]);
	}

	virtual void update(float dt)
	{
		move(dt);
		//check despawn
	}
	virtual void draw()
	{
		DrawModel(m_model, m_pos, 1.f, WHITE);
	}


protected:

	virtual void move(float dt)
	{
		m_pos.x += m_dir.x * m_speed * dt;
		m_pos.z += m_dir.z * m_speed * dt;
	}


};

class Floor : public BaseModel
{
public:
	Texture2D texture;

	Floor() = default;

	Floor(Texture2D tex)
	{
		texture = tex;
		m_model = LoadModelFromMesh(GenMeshCube(36.0f, 1.0f, 34.0f));
		m_pos = { 6.5f, -2.f, -8.0f };
		m_dir = { 0.0f, 0.0f, 0.0f };
		m_speed = 0.0f;

		SetMaterialTexture(&m_model.materials[0], MATERIAL_MAP_ALBEDO, texture);
	}

	void draw() override
	{
		DrawModel(m_model, m_pos, 1.0f, WHITE);
	}
};

class Player : public BaseModel
{
public:
    float yaw = 0.0f;
    float pitch = 0.0f;
    float angle = 0.f;

    Vector3 m_velocity{};
    bool m_isGrounded = true;
    float m_eyeHeight = 1.5f;

    int m_sideway = 0;
    int m_forward = 0;
    bool m_jumpPressed = false;
    bool m_crouching = false;

    Player() = default;

    Player(Model model, Vector3 pos, std::function<void(Vector3)> shootLaser)
        : BaseModel(model, pos, { 0,0,0 }, PLAYER_SPEED)
    {
    }

    void input()
    {
        mouseMovement();

        m_sideway = IsKeyDown(KEY_D) - IsKeyDown(KEY_A);
        m_forward = IsKeyDown(KEY_W) - IsKeyDown(KEY_S);
        //m_jumpPressed = IsKeyPressed(KEY_SPACE);
        m_crouching = IsKeyDown(KEY_LEFT_SHIFT);
    }

    void reset(Vector3 pos)
    {
        m_pos = pos;
        m_dir = { 0.0f,0.0f,0.0f };
        m_velocity = { 0.0f,0.0f,0.0f };
        m_isGrounded = true;
        yaw = 0.0f;
        pitch = 0.0f;
    }

    void mouseMovement()
    {
        Vector2 mouseDelta = GetMouseDelta();

        const float sensitivityX = 0.001f;
        const float sensitivityY = 0.001f;

        yaw -= mouseDelta.x * sensitivityX;
        pitch += mouseDelta.y * sensitivityY;
    }

    void update(float dt) override
    {
        input();

        Vector2 inputVec = { (float)m_sideway, (float)-m_forward };

#if NORMALIZE_INPUT
        if ((m_sideway != 0) && (m_forward != 0))
        {
            inputVec = Vector2Normalize(inputVec);
        }
#endif

        if (!m_isGrounded) m_velocity.y -= GRAVITY * dt;

        if (m_isGrounded && m_jumpPressed)
        {
            m_velocity.y = JUMP_FORCE;
            m_isGrounded = false;
        }

        Vector3 front = { sinf(yaw), 0.0f, cosf(yaw) };
        Vector3 right = { cosf(-yaw), 0.0f, sinf(-yaw) };

        Vector3 desiredDir = {
            inputVec.x * right.x + inputVec.y * front.x,
            0.0f,
            inputVec.x * right.z + inputVec.y * front.z
        };

        m_dir = Vector3Lerp(m_dir, desiredDir, CONTROL * dt);

        float drag = m_isGrounded ? FRICTION : AIR_DRAG;
        float dragFactor = powf(drag, dt * 60.0f);

        Vector3 hvel = {
            m_velocity.x * dragFactor,
            0.0f,
            m_velocity.z * dragFactor
        };

        float hvelLength = Vector3Length(hvel);
        if (hvelLength < (MAX_SPEED * 0.01f))
        {
            hvel = { 0.0f, 0.0f, 0.0f };
        }

        float speed = Vector3DotProduct(hvel, m_dir);
        float maxSpeed = m_crouching ? CROUCH_SPEED : MAX_SPEED;
        float accel = Clamp(maxSpeed - speed, 0.0f, MAX_ACCEL * dt);

        hvel.x += m_dir.x * accel;
        hvel.z += m_dir.z * accel;

        m_velocity.x = hvel.x;
        m_velocity.z = hvel.z;

        m_pos.x += m_velocity.x * dt;
        m_pos.y += m_velocity.y * dt;
        m_pos.z += m_velocity.z * dt;

        if (m_pos.y <= 0.0f)
        {
            m_pos.y = 0.0f;
            m_velocity.y = 0.0f;
            m_isGrounded = true;
        }
    }

    void draw() override
    {
        // leave empty for FPS
    }

    void constraints()
    {
    }
};