#version 300 es
precision highp float;
precision highp int;

uniform vec4 outlineColor;
out vec4 finalColor;

void main()
{
    finalColor = outlineColor;
}