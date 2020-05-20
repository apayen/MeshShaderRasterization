#version 450
#extension GL_ARB_separate_shader_objects : require

layout(location=0) in Interpolant
{
    vec3 albedo;
    vec3 normal;
} IN;

layout(location=0) out vec4 rt0;
layout(location=1) out vec4 rt1;

void main()
{
    rt0 = vec4(IN.albedo, 0);
    rt1 = vec4(IN.normal, 1);
}
