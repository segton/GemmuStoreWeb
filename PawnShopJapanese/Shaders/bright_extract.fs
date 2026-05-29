#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;

uniform float threshold;
uniform float knee;

out vec4 finalColor;

void main()
{
    vec3 color = texture(texture0, fragTexCoord).rgb;

    float brightness = max(max(color.r, color.g), color.b);

    // Soft threshold.
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 0.00001);

    float contribution = max(brightness - threshold, soft);
    contribution /= max(brightness, 0.00001);

    vec3 bloomColor = color * contribution;

    finalColor = vec4(bloomColor, 1.0);
}