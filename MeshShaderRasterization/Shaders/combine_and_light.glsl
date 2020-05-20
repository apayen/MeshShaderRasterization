#version 450
#extension GL_ARB_separate_shader_objects : require
#extension GL_ARB_compute_shader : require


layout(set=0, binding=0) uniform sampler2D framebufferDepthTexture;
layout(set=0, binding=1) uniform usampler2D meshShaderDepthTexture;
layout(set=0, binding=2) uniform sampler2DArray albedoTextureArray;
layout(set=0, binding=3) uniform sampler2DArray normalTextureArray;

layout(set=1, binding=0, rgba8) uniform writeonly image2D swapchainImage;

layout(local_size_x=8, local_size_y=8, local_size_z=1) in;
void main()
{
    ivec2 vpos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 swapchainSize = imageSize(swapchainImage);

    if (all(lessThan(vpos, swapchainSize)))
    {
        ivec2 buffersSize = textureSize(framebufferDepthTexture, 0);
        vec3 pos = vec3(vec2(gl_GlobalInvocationID.xy)/vec2(buffersSize), 0);

        float framebufferDepth = textureLod(framebufferDepthTexture, pos.xy, 0).x;
        float meshShaderDepth = uintBitsToFloat(textureLod(meshShaderDepthTexture, pos.xy, 0).x);

        pos.z = step(meshShaderDepth, framebufferDepth);

        vec3 albedo = textureLod(albedoTextureArray, pos, 0).xyz;
        vec3 normal = textureLod(normalTextureArray, pos, 0).xyz;

        imageStore(swapchainImage, vpos, vec4(1-framebufferDepth.x, 1-meshShaderDepth.x, 0, 0));
    }
}
