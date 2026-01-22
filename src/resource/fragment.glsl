#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;

uniform vec4 objectColor; // A color passed in from the application
uniform sampler2D tileTexture;

void main()
{
    // Ambient lighting
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * vec3(1.0, 1.0, 1.0);

    // Diffuse lighting (60 degrees from horizontal)
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(vec3(0.0, 0.866, 0.5)); // Y=sin(60), Z=cos(60)
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * vec3(1.0, 1.0, 1.0);

    // Triplanar Mapping
    vec3 blending = abs(normalize(Normal));
    blending = normalize(max(blending, 0.00001)); // Force weights to sum to 1.0 (approx)
    float b = (blending.x + blending.y + blending.z);
    blending /= b;

    vec4 xaxis = texture(tileTexture, FragPos.yz);
    vec4 yaxis = texture(tileTexture, FragPos.xz);
    vec4 zaxis = texture(tileTexture, FragPos.xy);
    vec4 texColor = xaxis * blending.x + yaxis * blending.y + zaxis * blending.z;

    vec3 result = (ambient + diffuse) * objectColor.rgb * texColor.rgb;
    FragColor = vec4(result, objectColor.a);
}