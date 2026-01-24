#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;

uniform vec4 objectColor;
uniform sampler2D tileTexture;
uniform bool useFlatColor;

void main()
{
    if (useFlatColor) {
        FragColor = objectColor;
        return;
    }

    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * vec3(1.0, 1.0, 1.0);

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(vec3(0.0, 0.866, 0.5)); // Y=sin(60), Z=cos(60)
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * vec3(1.0, 1.0, 1.0);

    vec3 blending = abs(normalize(Normal));
    blending = normalize(max(blending, 0.00001));
    float b = (blending.x + blending.y + blending.z);
    blending /= b;

    vec4 xaxis = texture(tileTexture, FragPos.yz);
    vec4 yaxis = texture(tileTexture, FragPos.xz);
    vec4 zaxis = texture(tileTexture, FragPos.xy);
    vec4 texColor = xaxis * blending.x + yaxis * blending.y + zaxis * blending.z;

    vec3 result = (ambient + diffuse) * objectColor.rgb * texColor.rgb;
    FragColor = vec4(result, objectColor.a);
}