#version 450
#extension GL_ARB_separate_shader_objects : require
#extension GL_NV_mesh_shader : require

layout(local_size_x=32) in;

taskNV out Task
{
    mat3x4 modelToWorldMatrix;
    uint   megatile[64];
} OUT;

uint packMegatile(ivec3 megatile)
{
    // assuming a max texture size of 64k, we would need:
    // * 4 bits for mip
    // * 10 bits per axis (1024 megatiles of 64 quads)
    // for hex reading convenience, let's pack 12-12-8
    return (megatile.x << 20) | (megatile.y << 8) | megatile.z;
}

void exportMegatile(uint index, ivec2 base, uint mip)
{
    OUT.megatile[index] = packMegatile(ivec3(ivec2(index >> 3, index & 0x7) + base, mip));
}

void main()
{
    ivec2 base = ivec2(gl_WorkGroupID.x & 0xf, gl_WorkGroupID.x >> 4) * 8;
    for (uint i = 0; i < 2; ++i)
    {
        exportMegatile(i * 32 + gl_LocalInvocationID.x, base, 0);
    }

    if (gl_LocalInvocationID.x == 0)
    {
        OUT.modelToWorldMatrix = mat3x4(1, 0, 0, -0.5,
                                        0, 1, 0, -0.5,
                                        0, 0, 1, 0);
        gl_TaskCountNV = 64 * 64;
    }
}
