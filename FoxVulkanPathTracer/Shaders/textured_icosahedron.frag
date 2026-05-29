#version 460

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;

layout(location = 0) out vec4 outColor;

layout(binding = 3) uniform sampler2D uTextureddd;

void main() {
    vec4 texColor = texture(uTextureddd, vUv); 
    outColor = texColor * vec4(vNormal, 1.0);;
}