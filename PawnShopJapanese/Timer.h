#pragma once
#include "raylib.h"
#include <functional>


class Timer
{
public:
    Timer() = default;

    Timer(float duration, bool repeat = false, bool autostart = false,
        std::function<void()> func = nullptr)
        : duration(duration), repeat(repeat), func(func)
    {
        if (autostart)
            activate();
    }

    void activate()
    {
        active = true;
        startTime = GetTime();
    }

    void deactivate()
    {
        active = false;
        startTime = 0.0;

        if (repeat)
            activate();
    }

    void update()
    {
        if (!active)
            return;

        if (GetTime() - startTime >= duration)
        {
            if (func && startTime > 0.0)
                func();

            deactivate();
        }
    }

private:
    float duration = 0.0f;
    double startTime = 0.0;
    bool active = false;
    bool repeat = false;
    std::function<void()> func = nullptr;
};
