#version 330 core
layout (location = 0) in vec3 aPos; // the position variable has attribute position 0
layout (location = 1) in vec3 aNormal; // the normal variable has attribute position 1
layout (location = 2) in vec4 aColor; // the color variable has attribute position 2

out vec3 Normal;
out vec3 FragPos;
out vec4 VertexColor;

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
    VertexColor = aColor;
}