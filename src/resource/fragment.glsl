#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec4 VertexColor;
in vec2 TexCoords;

uniform vec4 flatColor;
uniform sampler2D tileTexture;
uniform sampler2D texture_diffuse1;
uniform bool useFlatColor;
uniform bool useVertexColor;
uniform bool useTexture;

void main()
{
    if (useFlatColor) {
        if (useVertexColor) {
            FragColor = VertexColor;
        } else {
            FragColor = flatColor;
        }
        FragColor.rgb *= 1.2;
        FragColor.a *= flatColor.a;
        return;
    }

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(vec3(0.5, 0.866, 0.5));
    float diff = max(dot(norm, lightDir), 0.0);
    float ambientStrength = 0.3; 
    vec3 lightIntensity = vec3(ambientStrength + diff);

    vec4 texColor;
    if (useTexture) {
        texColor = texture(texture_diffuse1, TexCoords);
    } else {
        vec3 blending = abs(normalize(Normal));
        blending = normalize(max(blending, 0.00001));
        float b = (blending.x + blending.y + blending.z);
        blending /= b;

        vec4 xaxis = texture(tileTexture, FragPos.yz);
        vec4 yaxis = texture(tileTexture, FragPos.xz);
        vec4 zaxis = texture(tileTexture, FragPos.xy);
        texColor = xaxis * blending.x + yaxis * blending.y + zaxis * blending.z;
    }

    vec3 result = lightIntensity * texColor.rgb;
    if (flatColor.rgb != vec3(1.0, 1.0, 1.0) || flatColor.a != 1.0) {
        result *= flatColor.rgb;
    }

    float alpha = texColor.a * flatColor.a;
    if (alpha < 0.1) {
        discard;
    }
    
    FragColor = vec4(result, alpha);
}