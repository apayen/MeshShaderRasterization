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

void main()
{
    if (gl_LocalInvocationID.x == 0)
    {
        OUT.modelToWorldMatrix = mat3x4(1, 0, 0, 0,
                                        0, 1, 0, 0,
                                        0, 0, 1, 0);
        OUT.megatile[0] = packMegatile(ivec3(0, 0, 0));
        gl_TaskCountNV = 64;
    }
}
