#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec4 aColor;
layout (location = 3) in vec2 aTexCoords;

out vec3 Normal;
out vec3 FragPos;
out vec4 VertexColor;
out vec2 TexCoords;
out float FogDistance;

// Uniforms for transformation matrices
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat3 normalMatrix;

void main()
{
    // Calculate the final position in clip space
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    Normal = normalMatrix * aNormal;
    FragPos = vec3(model * vec4(aPos, 1.0));
    FogDistance = abs((view * model * vec4(aPos, 1.0)).z);
    VertexColor = aColor;
    TexCoords = aTexCoords;
}