#pragma once

#if defined(_WIN32)
#define NOGDI
#define NOUSER
#endif
#include "raylib.h"
#include "raymath.h"
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <functional>

#include "Timer.h"


constexpr int WINDOW_WIDTH = 1920;
constexpr int WINDOW_HEIGHT = 1080;
constexpr Color BG_COLOR = BLACK;
constexpr float PLAYER_SPEED = 8.f;
constexpr float LASER_SPEED = 9.f;
constexpr int METEOR_SPEED_MIN = 3;
constexpr int METEOR_SPEED_MAX = 4;

constexpr float METEOR_TIMER_DURATION = 0.4f;

constexpr int FONT_SIZE = 60;
constexpr int FONT_PADDING = 60;

#define GRAVITY         32.0f
#define MAX_SPEED       7.0f
#define CROUCH_SPEED     2.2f
#define JUMP_FORCE      12.0f
#define MAX_ACCEL      110.0f
// Grounded drag
#define FRICTION         0.86f
// Increasing air drag, increases strafing speed
#define AIR_DRAG         0.98f
// Responsiveness for turning movement direction to looked direction
#define CONTROL         15.0f
#define CROUCH_HEIGHT    0.0f
#define STAND_HEIGHT     1.0f
#define BOTTOM_HEIGHT    0.5f

#define NORMALIZE_INPUT  0

#define MAX_LIGHTS 12
static Vector2 sensitivity = { 0.001f, 0.001f };


static Vector2 lookRotation = { 0 };
static float headTimer = 0.0f;
static float walkLerp = 0.0f;
static float headLerp = STAND_HEIGHT;
static Vector2 lean = { 0 };
//lighting
