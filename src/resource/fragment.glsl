#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec4 VertexColor;
in vec2 TexCoords;
in float FogDistance;

uniform vec4 flatColor;
uniform sampler2D tileTexture;
uniform sampler2D texture_diffuse1;
uniform bool useFlatColor;
uniform bool useVertexColor;
uniform bool useTexture;
uniform bool useUiTexture;
uniform bool useAlphaTexture;
uniform bool useFog;
uniform sampler2D uiTexture;
uniform vec4 fogColor;
uniform float fogStart;
uniform float fogEnd;

void main()
{
    vec4 outputColor;
    if (useFlatColor) {
        if (useVertexColor) {
            outputColor = VertexColor;
        } else {
            outputColor = flatColor;
        }
        outputColor.a *= flatColor.a;
        if (useUiTexture) {
            vec4 sampled = texture(uiTexture, TexCoords);
            if (useAlphaTexture) {
                outputColor.a *= sampled.r;
            } else {
                outputColor.rgb *= sampled.rgb;
                outputColor.a *= sampled.a;
            }
        }
    } else {
        vec3 norm = normalize(Normal);
        vec3 lightDir = normalize(vec3(0.5, 0.866, 0.5));
        float diff = max(dot(norm, lightDir), 0.0);
        float ambientStrength = 0.1;
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
        outputColor = vec4(result, alpha);
    }

    if (useFog) {
        float fogFactor = clamp((fogEnd - FogDistance) / (fogEnd - fogStart), 0.0, 1.0);
        outputColor.rgb = mix(fogColor.rgb, outputColor.rgb, fogFactor);
    }
    FragColor = outputColor;
}