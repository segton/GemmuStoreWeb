#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 direction;
uniform vec2 texelSize;

uniform float blurScale;

void main()
{
    vec2 offset = direction * texelSize * blurScale;

    vec4 color = texture(texture0, fragTexCoord) * 0.227027;
    color += texture(texture0, fragTexCoord + offset * 1.384615) * 0.316216;
    color += texture(texture0, fragTexCoord - offset * 1.384615) * 0.316216;
    color += texture(texture0, fragTexCoord + offset * 3.230769) * 0.070270;
    color += texture(texture0, fragTexCoord - offset * 3.230769) * 0.070270;

    finalColor = vec4(color.rgb, 1.0);
}