#version 460

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;

layout(location = 0) out vec4 outColor;

layout(binding = 5) uniform sampler2D uTexture;

void main() {
    vec4 texColor = texture(uTexture, vUv); 
    outColor = texColor;
}