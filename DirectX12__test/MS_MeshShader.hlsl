struct MeshOut
{
    float4 pos : SV_Position;
    float4 color : COLOR;
};

[outputtopology("triangle")]
[numthreads(1,1,1)]
void MeshMain(
    uint3 tid : SV_DispatchThreadID,
    out vertices MeshOut verts[3],
    out indices uint3 tris[1])
{
    // Triangle vertices
    SetMeshOutputCounts(3, 1);
    
    verts[0].pos = float4(0.0f, 0.6f, 0.0f, 1.0f);
    verts[1].pos = float4(0.6f, -0.6f, 0.0f, 1.0f);
    verts[2].pos = float4(-0.6f, -0.6f, 0.0f, 1.0f);
    
    verts[0].color = float4(1.0f, 0.0f, 0.0f, 1.0f); // Red
    verts[1].color = float4(0.0f, 1.0f, 0.0f, 1.0f); // Green
    verts[2].color = float4(0.0f, 0.0f, 1.0f, 1.0f); // Blue
    
    tris[0] = uint3(0, 1, 2); // Triangle made of the three vertices
}

float4 MeshPS(MeshOut input) : SV_Target
{
    return input.color; // Output the vertex color
}