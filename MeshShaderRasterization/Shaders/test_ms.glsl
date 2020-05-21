#version 450
#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_NV_mesh_shader : require

// we use a tile of 8x8 quads, hence 9x9=81 vertices and 8x8x2=128 triangles
// we dispatch megatiles of 8x8 tiles (or 64x64 quads)
// one task shader may dispatch 

layout(local_size_x=32) in;
layout(triangles) out;
layout(max_vertices=81, max_primitives=128) out;

taskNV in Task
{
    mat3x4 modelToWorldMatrix;
    uint   megatile[64];
} IN;

ivec3 unpackMegatile(uint megatile)
{
    // assuming a max texture size of 64k, we would need:
    // * 4 bits for mip
    // * 10 bits per axis (1024 megatiles of 64 quads)
    // for hex reading convenience, let's pack 12-12-8
    return ivec3(megatile >> 20, (megatile >> 8) & 0xfff, megatile & 0xf);
}

out gl_MeshPerVertexNV
{
    precise vec4 gl_Position;
} gl_MeshVerticesNV[];

#if defined(GBUFFER_PASS)
layout(location=0) out Interpolant
{
    vec3 albedo;
    vec3 normal;
} OUT[];
#endif

shared uint    s_primsToExport;
shared uint    s_pixelsToRaster;
shared u16vec2 s_pixelPos[128];         // packed 2x16
shared u8vec4  s_pixelTriIndices[128];  // packed 3x8
shared uint    s_pixelBary[128];        // packed 2x16f

layout(set=0, binding=0, std140) uniform sceneBuffer
{
    mat4 projectionMatrix;
    vec4 viewportSize;
};

#if defined(DEPTH_PASS)
layout(set=0, binding=1, r32ui) uniform coherent uimage2D depthBuffer;
#elif defined(GBUFFER_PASS)
layout(set=0, binding=1, r32ui) uniform readonly uimage2D depthBuffer;
layout(set=0, binding=2, rgba8) uniform writeonly image2D albedoBuffer;
layout(set=0, binding=3, rgba8) uniform writeonly image2D normalBuffer;
#endif

layout(set=1, binding=0) uniform sampler2D positionTexture;
#if defined(GBUFFER_PASS)
layout(set=1, binding=1 )uniform sampler2D albedoTexture;
layout(set=1, binding=2 )uniform sampler2D normalTexture;
#endif

// courtesy of Tom Forsyth https://www.shadertoy.com/view/wdjfz1
// (adapted to use vec4 instead of Vertex struct)
// -------------------------------------------------------------------
// Test a position against a triangle and return
// the perspective-correct barycentric coordinates in the triangle
// Note the z value in the vertex is ignored, it's the w that matters.
vec2 BaryTri3D ( vec2 pos, vec4 v1, vec4 v2, vec4 v3 )
{
    vec2 posv1 = pos - v1.xy;

    vec2 v21 = v2.xy - v1.xy;
    vec2 v31 = v3.xy - v1.xy;

    float scale = v21.x * v31.y - v21.y * v31.x;
    float rscale = 1.0 / scale;
    float baryi = ( posv1.x * v31.y - posv1.y * v31.x ) * rscale;
    float baryj = ( posv1.x * v21.y - posv1.y * v21.x ) * -rscale;

    // Now interpolate the canonical coordinates (0,0,1,v1.w), (1,0,1,v2.w) and (0,1,1,v3.w)
    // with perspective correction
    // So we project all three by their respective w:
    // (0,0,v1.w) -> (0,     0,     1/v1.w)
    // (1,0,v2.w) -> (1/v2.w,0,     1/v2.w)
    // (0,1,v3.w) -> (0,     1/v3.w,1/v3.w)
    // Then interpolate those values linearly to produce (nx,ny,nw),
    // then divide by nw again.
    vec3 recipw = vec3 ( 1.0/v1.w, 1.0/v2.w, 1.0/v3.w );

    float baryk = 1.0 - baryi - baryj;
    float newi = recipw.y * baryi;
    float newj = recipw.z * baryj;
    //float neww = recipw.x * baryk + recipw.y * baryi + recipw.z * baryj;
    float neww = recipw.x * baryk + newi + newj;

    // ...and project back.
    float rneww = 1.0/neww;
    float perspi = newi * rneww;
    float perspj = newj * rneww;

    return vec2 ( perspi, perspj );
}

void processVertex(uint vertexId, vec2 tileOffset, int mipLevel)
{
    if (vertexId < 81)
    {
        uvec2 pos;
        pos.y = vertexId / 9;
        pos.x = vertexId - (pos.y * 9);

        vec2 uv = (tileOffset + vec2(pos)) / vec2(256, 128);// / vec2(textureSize(positionTexture, mipLevel));

        gl_MeshVerticesNV[vertexId].gl_Position = vec4(vec4(uv-1, 0.25, 1) * IN.modelToWorldMatrix, 2);
        //gl_MeshVerticesNV[vertexId].gl_Position = vec4(textureLod(positionTexture, uv, mipLevel).xy, 0.5, 1);
        //gl_MeshVerticesNV[vertexId].gl_Position = vec4(vec4(textureLod(positionTexture, uv, mipLevel).xyz, 1) * IN.modelToWorldMatrix, 1) * projectionMatrix;
#if defined(GBUFFER_PASS)
        OUT[vertexId].albedo = textureLod(albedoTexture, uv, mipLevel).xyz;
        OUT[vertexId].normal = textureLod(normalTexture, uv, mipLevel).xyz * mat3(IN.modelToWorldMatrix);
#endif
    }
}

void exportTriangleForRaterization(uint ia, uint ib, uint ic)
{
    uvec4 vote = subgroupBallot(true);
    uint  index = s_primsToExport + subgroupBallotExclusiveBitCount(vote);

    gl_PrimitiveIndicesNV[3*index + 0] = ia;
    gl_PrimitiveIndicesNV[3*index + 1] = ib;
    gl_PrimitiveIndicesNV[3*index + 2] = ic;

    s_primsToExport += subgroupBallotBitCount(vote);
}

void processTriangle(uint ia, uint ib, uint ic)
{
    vec4 pa = gl_MeshVerticesNV[ia].gl_Position;
    vec4 pb = gl_MeshVerticesNV[ib].gl_Position;
    vec4 pc = gl_MeshVerticesNV[ic].gl_Position;

    pa.xyz /= pa.w;
    pb.xyz /= pb.w;
    pc.xyz /= pc.w;

    if (determinant(mat2(pb.xy - pa.xy, pc.xy - pa.xy)) <= 0)
    {
        // face culling
        return;
    }

    if (all(lessThan(vec3(pa.z, pb.z, pc.z), vec3(0))) || all(greaterThan(vec3(pa.z, pb.z, pc.z), vec3(1))))
    {
        // early discard triangles that are in front of the near plane or behind the far plane
        return;
    }

    if (any(lessThan(vec3(pa.z, pb.z, pc.z), vec3(0))) || any(greaterThan(vec3(pa.z, pb.z, pc.z), vec3(1))))
    {
        // triangles that crosses near or far clip plane are exported for rasterization
        exportTriangleForRaterization(ia, ib, ic);
        return;
    }

    precise vec4 pixelquad; // xy: min, zw: max of the 3 vertices clipspace xy

    // triminmax (available on AMD) would help here
    if (pa.x > pb.x)
    {
        pixelquad.x = min(pb.x, pc.x);
        pixelquad.z = max(pa.x, pc.x);
    }
    else
    {
        pixelquad.x = min(pa.x, pc.x);
        pixelquad.z = max(pb.x, pc.x);
    }
    if (pa.y > pb.y)
    {
        pixelquad.y = min(pb.y, pc.y);
        pixelquad.w = max(pa.y, pc.y);
    }
    else
    {
        pixelquad.y = min(pa.y, pc.y);
        pixelquad.w = max(pb.y, pc.y);
    }

    if (any(lessThan(pixelquad.xy, vec2(-1))) || any(greaterThan(pixelquad.zw, vec2(1))))
    {
        // discard triangles fully outside of clipspace
        return;
    }

    pixelquad = (pixelquad + 1) / 2;
    pixelquad *= viewportSize.xyxy;
    pixelquad = ceil(pixelquad - 0.5 + vec4(-0.005, -0.005, 0.005, 0.005)); // todo: fixme

    vec2 pixelsize = pixelquad.zw - pixelquad.xy;
    if (min(pixelsize.x, pixelsize.y) <= 0)
    {
        // cull triangles so small they do not even cover one pixel
        return;
    }
    else if (max(pixelsize.x, pixelsize.y) > 1)
    {
        // output the triangle for normal rasterization
        exportTriangleForRaterization(ia, ib, ic);
    }
    else 
    {
        // this pixel will be shader rasterized
        vec2 pixelpos = (pixelquad.xy + pixelquad.zw)/2;

        // discard single pixel outside of clipspace
        if (any(lessThan(pixelpos, vec2(0))) || any(greaterThanEqual(pixelpos, viewportSize.xy)))
        {
            exportTriangleForRaterization(ia, ib, ic);
            return;
        }

        precise vec2 bary = BaryTri3D((pixelpos * viewportSize.zw)*2 - 1, pa, pb, pc);

        // the pixel is actually inside of the triangle
        if (all(greaterThanEqual(bary, vec2(0))) && all(lessThanEqual(bary, vec2(1))))
        {
            // mark the triangle for internal rasterization
            uvec4 vote = subgroupBallot(true);
            uint  index = s_pixelsToRaster + subgroupBallotExclusiveBitCount(vote);

            uvec2 ipixelpos = uvec2(pixelpos);
            s_pixelPos[index] = u16vec2(ipixelpos);
            s_pixelTriIndices[index] = u8vec4(ia, ib, ic, 0);
            s_pixelBary[index] = packHalf2x16(bary);

            s_pixelsToRaster += subgroupBallotBitCount(vote);
        }
    }
}

void processQuad(uint quadId)
{
    uint pos = (quadId >> 3)*9 + (quadId & 0x7);
    processTriangle(pos, pos + 1, pos + 9);
    processTriangle(pos + 1, pos + 10, pos + 9);
}

void rasterPixel(uint index)
{
    ivec2 pixelpos = s_pixelPos[index];

    uint ia = s_pixelTriIndices[index].x;
    uint ib = s_pixelTriIndices[index].y;
    uint ic = s_pixelTriIndices[index].z;

    vec2 bary = unpackHalf2x16(s_pixelBary[index]);

    vec4 pa = gl_MeshVerticesNV[ia].gl_Position;
    vec4 pb = gl_MeshVerticesNV[ib].gl_Position;
    vec4 pc = gl_MeshVerticesNV[ic].gl_Position;

    vec2 zw = mix(pa.zw, mix(pb.zw, pc.zw, bary.y), bary.x);
    float d = zw.x/zw.y;

#if defined(DEPTH_PASS)
    imageAtomicMin(depthBuffer, pixelpos, floatBitsToUint(d));
#elif defined(GBUFFER_PASS)
    float olddepth = uintBitsToFloat(imageLoad(depthBuffer, pixelpos).x);
    if (d == olddepth)
    {
        imageStore(albedoBuffer, pixelpos, vec4(mix(OUT[ia].albedo, mix(OUT[ib].albedo, OUT[ib].albedo, bary.y), bary.x), 0));
        imageStore(normalBuffer, pixelpos, vec4(mix(OUT[ia].normal, mix(OUT[ib].normal, OUT[ib].normal, bary.y), bary.x), 0));
    }
#endif
}

void main()
{
    if (gl_LocalInvocationID.x == 0)
    {
        s_primsToExport = 0;
        s_pixelsToRaster = 0;
    }

    memoryBarrierShared();
    barrier();

    ivec3 megatile = unpackMegatile(IN.megatile[gl_WorkGroupID.x/64]);
    vec2 tilePos = vec2(uvec2(megatile.x << 6, megatile.y << 6) + uvec2((gl_WorkGroupID.x & 0x7) << 3, gl_WorkGroupID.x & 0x38));

    for (int i = 0; i < 3; ++i)
    {
        processVertex(i*32 + gl_LocalInvocationID.x, tilePos, megatile.z);
    }

    memoryBarrierShared();
    barrier();

    for (int i = 0; i < 2; ++i)
    {
        processQuad(i*32 + gl_LocalInvocationID.x);
    }

    memoryBarrierShared();
    barrier();

    uint loops = (s_pixelsToRaster + 31) / 32;
    for (uint i = 0; i < loops; ++i)
    {
        uint index = i*32 + gl_LocalInvocationID.x;
        if (index < s_pixelsToRaster)
        {
            rasterPixel(index);
        }
    }

    if (gl_LocalInvocationID.x == 0)
    {
        gl_PrimitiveCountNV = s_primsToExport;
    }
}
