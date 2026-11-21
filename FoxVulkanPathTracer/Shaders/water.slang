// Water: the shallow water equations (the Navier-Stokes equations averaged over the depth of the water) on a
// staggered grid, and the surface they give. See Scene/WaterSimulation.h. Compiled once per pass (PASS):
//
//   0 velocity  per cell face: the velocity carried along by itself (semi-Lagrangian advection), spread by
//               viscosity, pushed by gravity down the slope of the surface, slowed by the bed (friction) and
//               driven by the wind
//   1 outflow   per cell: how much of what would flow out this step it holds (the depth never goes below zero)
//   2 depth     per cell: the water that flows in minus what flows out (mass conservation), plus drops
//   3 surface   per cell: its vertex of the surface mesh, its normal, its foam, and where it was the step before
//   4 foam      per cell, once a frame: foam carried by the current, fading, and made where the water breaks
//               or churns (converging flow, steep wave faces, the shore, drops, spray landing)
//   5 emit      per cell, once a frame: spray droplets thrown up where foam is made, by splashes, and off crests in
//               strong wind
//   6 particles per droplet, once a frame: flight (gravity, air drag, the wind), landing back on the water as foam,
//               and its little octahedron in the scene's vertex buffers
//
// Depth h lives at cell centres; the velocity's x component u on the faces between cells along x, its z
// component v on the faces along z. Cell (i, j) covers [i, i+1] x [j, j+1] cells from the water's corner.

// Mirrors Fox::Graphics::Vulkan::Vertex (Vertex.h)
struct Vertex
{
    float4 position;
    float4 normal;
    float2 uv;
    float2 padding;
    float4 tangent;
};

// Mirrors WaterSimulation::Constants
struct Constants
{
    uint width;          // Cells along x
    uint depth;          // Cells along z
    uint firstVertex;    // Of the surface, in the scene's vertex buffers
    uint firstPrevious;  // Of the surface, in the previous positions
    float cellSize;      // Metres
    float dt;            // Seconds, this step
    float gravity;
    float viscosity;     // m^2/s
    float friction;      // Bed friction coefficient (dimensionless, Chezy-like)
    float windX;         // Wind stress on the water over its density (m^2/s^2), along x and z
    float windZ;
    float gust;          // 0..1: how much the wind varies over the water and in time
    float time;
    uint dropCount;
    float originX;       // World position of the water's corner (x, z) and its bed reference
    float originY;
    float originZ;
    float maxSpeed;      // Metres per second; faster is clamped (stability)
    float depthCorrection; // Metres added to every wet cell this step (what rounding took from the volume)
    float frameDt;       // Seconds, the whole frame (foam and spray)
    float foamAmount;    // Scales how much foam is made
    float foamLifetime;  // Seconds for foam to fade to a third
    float sprayAmount;   // Scales how many droplets are thrown up
    float dropletSize;   // Metres (radius)
    uint particleCount;  // Droplets there is room for
    uint particleFirstVertex;   // Their octahedra in the scene's vertex buffers (6 vertices each)
    uint particleFirstPrevious; // And their previous positions
    uint frameIndex;
    float windSpeed;     // m/s
    float windAngle;     // Radians, where it blows to
    float pad0;
    float pad1;
};

[[vk::push_constant]] ConstantBuffer<Constants> constants;

[[vk::binding(0, 0)]] StructuredBuffer<float> gBed;          // Height of the ground under each cell (world y)
[[vk::binding(1, 0)]] RWStructuredBuffer<float> gDepthIn;    // Water depth (metres) at cell centres
[[vk::binding(2, 0)]] RWStructuredBuffer<float> gDepthOut;
[[vk::binding(3, 0)]] RWStructuredBuffer<float> gUIn;        // (width + 1) x depth faces
[[vk::binding(4, 0)]] RWStructuredBuffer<float> gUOut;
[[vk::binding(5, 0)]] RWStructuredBuffer<float> gVIn;        // width x (depth + 1) faces
[[vk::binding(6, 0)]] RWStructuredBuffer<float> gVOut;
[[vk::binding(7, 0)]] RWStructuredBuffer<float> gOutflowScale; // Per cell
[[vk::binding(8, 0)]] StructuredBuffer<float4> gDrops;       // x, z (metres from the corner), radius, amount (metres)
[[vk::binding(9, 0)]] RWStructuredBuffer<Vertex> gVertices;      // The scene's, read by the hit shaders
[[vk::binding(10, 0)]] RWStructuredBuffer<Vertex> gBuildVertices; // The scene's, the structure's input
[[vk::binding(11, 0)]] RWStructuredBuffer<float4> gPrevious;   // Previous positions (motion vectors)
[[vk::binding(12, 0)]] RWStructuredBuffer<float> gFoamIn;     // Foam per cell, 0..1
[[vk::binding(13, 0)]] RWStructuredBuffer<float> gFoamOut;
[[vk::binding(14, 0)]] RWStructuredBuffer<uint> gLanded;      // Per cell: spray that landed (thousandths of a cell of foam)
[[vk::binding(15, 0)]] RWStructuredBuffer<float4> gParticles; // 2 per droplet: position + life (seconds), velocity + radius
[[vk::binding(16, 0)]] RWStructuredBuffer<uint> gParticleCounter; // [0] droplets ever thrown (the ring's next slot), [1] flying this frame
[[vk::binding(17, 0)]] RWStructuredBuffer<float> gChurn;      // Per cell: how hard the water breaks this frame

static const float WET = 1e-3f; // Metres: shallower is dry

uint Cell(int i, int j)
{
    i = clamp(i, 0, int(constants.width) - 1);
    j = clamp(j, 0, int(constants.depth) - 1);
    return uint(j) * constants.width + uint(i);
}

float Depth(int i, int j) { return gDepthIn[Cell(i, j)]; }
float Surface(int i, int j) { uint c = Cell(i, j); return gBed[c] + gDepthIn[c]; }

// Velocity components at any point (metres from the corner), bilinear on their staggered grids
float SampleU(float x, float z)
{
    float fx = clamp(x / constants.cellSize, 0.0f, float(constants.width));
    float fz = clamp(z / constants.cellSize - 0.5f, 0.0f, float(constants.depth - 1));
    int i = min(int(fx), int(constants.width) - 1);
    int j = min(int(fz), int(constants.depth) - 2);
    j = max(j, 0);
    float tx = fx - i, tz = fz - j;
    uint stride = constants.width + 1;
    int j1 = min(j + 1, int(constants.depth) - 1);
    float a = lerp(gUIn[j * stride + i], gUIn[j * stride + i + 1], tx);
    float b = lerp(gUIn[j1 * stride + i], gUIn[j1 * stride + i + 1], tx);
    return lerp(a, b, tz);
}

float SampleV(float x, float z)
{
    float fx = clamp(x / constants.cellSize - 0.5f, 0.0f, float(constants.width - 1));
    float fz = clamp(z / constants.cellSize, 0.0f, float(constants.depth));
    int i = max(min(int(fx), int(constants.width) - 2), 0);
    int j = min(int(fz), int(constants.depth) - 1);
    float tx = fx - i, tz = fz - j;
    uint stride = constants.width;
    int i1 = min(i + 1, int(constants.width) - 1);
    float a = lerp(gVIn[j * stride + i], gVIn[j * stride + i1], tx);
    float b = lerp(gVIn[(j + 1) * stride + i], gVIn[(j + 1) * stride + i1], tx);
    return lerp(a, b, tz);
}

// Random numbers from a counter (PCG)
uint Pcg(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float Random(inout uint seed)
{
    seed = Pcg(seed);
    return float(seed) / 4294967296.0f;
}

// The velocity at a cell's centre: the average of its faces (current state)
float2 CellVelocity(int i, int j)
{
    uint su = constants.width + 1;
    i = clamp(i, 0, int(constants.width) - 1);
    j = clamp(j, 0, int(constants.depth) - 1);
    float u = 0.5f * (gUIn[uint(j) * su + uint(i)] + gUIn[uint(j) * su + uint(i + 1)]);
    float v = 0.5f * (gVIn[uint(j) * constants.width + uint(i)] + gVIn[uint(j + 1) * constants.width + uint(i)]);
    return float2(u, v);
}

// Smooth noise for gusts: the wind is never quite even
float Hash(float2 p) { return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f); }
float Noise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(Hash(i), Hash(i + float2(1, 0)), u.x), lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), u.x), u.y);
}

// One face's new velocity component. depthLeft/Right: the cells on either side; surfaceLeft/Right: their water levels
float UpdateFace(float velocity, float advected, float laplacian, float depthLeft, float depthRight, float surfaceLeft, float surfaceRight,
    float bedLeft, float bedRight, float wind, float2 position)
{
    float dt = constants.dt;
    bool wetLeft = depthLeft > WET, wetRight = depthRight > WET;
    if (!wetLeft && !wetRight)
        return 0.0f;
    // A dry cell whose ground is above the water next to it is a wall
    if ((!wetLeft && bedLeft > surfaceRight) || (!wetRight && bedRight > surfaceLeft))
        return 0.0f;

    float u = advected;
    // Viscosity (explicit diffusion; the step is kept within its stable range on the CPU)
    u += constants.viscosity * dt * laplacian;
    // Gravity: the water runs down the slope of its surface
    u -= constants.gravity * dt * (surfaceRight - surfaceLeft) / constants.cellSize;
    // Wind stress on the surface (per unit of water density), spread over the depth, varying over the water
    float faceDepth = max(0.5f * (depthLeft + depthRight), 0.02f);
    float gust = lerp(1.0f, 2.0f * Noise(position * 0.15f + constants.time * 0.4f), constants.gust);
    u += wind * gust * dt / faceDepth;
    // Bed friction, stronger in shallow water: u * |u| * friction / depth (implicit, so it never overshoots)
    u /= 1.0f + dt * constants.friction * abs(u) / faceDepth;
    return clamp(u, -constants.maxSpeed, constants.maxSpeed);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int W = int(constants.width);
    int D = int(constants.depth);
    float dx = constants.cellSize;

#if PASS == 0
    // --- Velocity, on the faces ---
    uint uCount = uint(W + 1) * uint(D);
    uint vCount = uint(W) * uint(D + 1);
    if (id.x < uCount)
    {
        int i = int(id.x % uint(W + 1)); // Face between cells i-1 and i
        int j = int(id.x / uint(W + 1));
        if (i == 0 || i == W)
        {
            gUOut[id.x] = 0.0f; // The water's edges are walls
            return;
        }
        float2 p = float2(i * dx, (j + 0.5f) * dx);
        float2 velocity = float2(gUIn[id.x], SampleV(p.x, p.y));
        float advected = SampleU(p.x - velocity.x * constants.dt, p.y - velocity.y * constants.dt);
        uint stride = uint(W + 1);
        float laplacian = (gUIn[id.x - 1] + gUIn[id.x + 1]
            + gUIn[uint(max(j - 1, 0)) * stride + uint(i)] + gUIn[uint(min(j + 1, D - 1)) * stride + uint(i)] - 4.0f * gUIn[id.x]) / (dx * dx);
        uint left = Cell(i - 1, j), right = Cell(i, j);
        gUOut[id.x] = UpdateFace(gUIn[id.x], advected, laplacian, gDepthIn[left], gDepthIn[right],
            gBed[left] + gDepthIn[left], gBed[right] + gDepthIn[right], gBed[left], gBed[right], constants.windX, p);
    }
    else if (id.x < uCount + vCount)
    {
        uint f = id.x - uCount;
        int i = int(f % uint(W));
        int j = int(f / uint(W)); // Face between cells j-1 and j
        if (j == 0 || j == D)
        {
            gVOut[f] = 0.0f;
            return;
        }
        float2 p = float2((i + 0.5f) * dx, j * dx);
        float2 velocity = float2(SampleU(p.x, p.y), gVIn[f]);
        float advected = SampleV(p.x - velocity.x * constants.dt, p.y - velocity.y * constants.dt);
        uint stride = uint(W);
        float laplacian = (gVIn[f - stride] + gVIn[f + stride]
            + gVIn[uint(j) * stride + uint(max(i - 1, 0))] + gVIn[uint(j) * stride + uint(min(i + 1, W - 1))] - 4.0f * gVIn[f]) / (dx * dx);
        uint left = Cell(i, j - 1), right = Cell(i, j);
        gVOut[f] = UpdateFace(gVIn[f], advected, laplacian, gDepthIn[left], gDepthIn[right],
            gBed[left] + gDepthIn[left], gBed[right] + gDepthIn[right], gBed[left], gBed[right], constants.windZ, p);
    }

#elif PASS == 1
    // --- Outflow: the share of its outflow a cell can give without going below zero ---
    if (id.x >= uint(W * D))
        return;
    int i = int(id.x % uint(W)), j = int(id.x / uint(W));
    float h = gDepthIn[id.x];
    uint su = uint(W + 1);
    float uLeft = gUOut[uint(j) * su + uint(i)], uRight = gUOut[uint(j) * su + uint(i + 1)];
    float vBack = gVOut[uint(j) * uint(W) + uint(i)], vFront = gVOut[uint(j + 1) * uint(W) + uint(i)];
    float outflow = (max(uRight, 0.0f) + max(-uLeft, 0.0f) + max(vFront, 0.0f) + max(-vBack, 0.0f)) * h * constants.dt / dx;
    gOutflowScale[id.x] = outflow > h ? h / outflow : 1.0f;

#elif PASS == 2
    // --- Depth: in minus out (each face carries the depth of the cell it flows from) ---
    if (id.x >= uint(W * D))
        return;
    int i = int(id.x % uint(W)), j = int(id.x / uint(W));
    uint su = uint(W + 1);

    // Flux through a face, positive along +x or +z: velocity * upwind depth, limited by the upwind cell
    float flux = 0.0f;
    {
        float u = gUOut[uint(j) * su + uint(i)]; // Left face: from cell i-1 into i when positive
        uint from = u > 0.0f ? Cell(i - 1, j) : id.x;
        flux += (i > 0 ? u * gDepthIn[from] * gOutflowScale[from] : 0.0f);
    }
    {
        float u = gUOut[uint(j) * su + uint(i + 1)]; // Right face
        uint from = u > 0.0f ? id.x : Cell(i + 1, j);
        flux -= (i < W - 1 ? u * gDepthIn[from] * gOutflowScale[from] : 0.0f);
    }
    {
        float v = gVOut[uint(j) * uint(W) + uint(i)]; // Back face
        uint from = v > 0.0f ? Cell(i, j - 1) : id.x;
        flux += (j > 0 ? v * gDepthIn[from] * gOutflowScale[from] : 0.0f);
    }
    {
        float v = gVOut[uint(j + 1) * uint(W) + uint(i)]; // Front face
        uint from = v > 0.0f ? id.x : Cell(i, j + 1);
        flux -= (j < D - 1 ? v * gDepthIn[from] * gOutflowScale[from] : 0.0f);
    }
    float h = gDepthIn[id.x] + flux * constants.dt / dx;

    // Drops and splashes: a dip with a ring around it (as much water lifted as pushed down), on wet water only
    if (h > WET)
    {
        float2 p = float2((i + 0.5f) * dx, (j + 0.5f) * dx);
        for (uint d = 0; d < constants.dropCount; d++)
        {
            float4 drop = gDrops[d];
            float r2 = dot(p - drop.xy, p - drop.xy) / (drop.z * drop.z);
            if (r2 < 9.0f)
                h -= drop.w * (1.0f - r2) * exp(-r2);
        }
    }
    if (h > WET)
        h += constants.depthCorrection;
    gDepthOut[id.x] = max(h, 0.0f);

#elif PASS == 3
    // --- Surface: a vertex per cell centre. Dry cells sink just under the ground, so the shore is where the
    // water meets it ---
    if (id.x >= uint(W * D))
        return;
    int i = int(id.x % uint(W)), j = int(id.x / uint(W));
    float h = gDepthIn[id.x];
    bool wet = h > WET;
    float bed = gBed[id.x];
    float level = wet ? bed + h : bed - 0.05f;

    // The normal from the slope of the water around it (dry neighbours count as level with it)
    float own = bed + h;
    float left = Depth(i - 1, j) > WET ? Surface(i - 1, j) : own;
    float right = Depth(i + 1, j) > WET ? Surface(i + 1, j) : own;
    float back = Depth(i, j - 1) > WET ? Surface(i, j - 1) : own;
    float front = Depth(i, j + 1) > WET ? Surface(i, j + 1) : own;
    float3 normal = normalize(float3((left - right) / (2.0f * dx), 1.0f, (back - front) / (2.0f * dx)));

    Vertex vertex;
    vertex.position = float4(constants.originX + (i + 0.5f) * dx, level, constants.originZ + (j + 0.5f) * dx, 1.0f);
    vertex.normal = float4(normal, 0.0f);
    vertex.uv = float2((i + 0.5f) / W, (j + 0.5f) / D);
    vertex.padding = float2(wet ? gFoamIn[id.x] : 0.0f, 0.0f); // Foam, for the closest hit shader
    vertex.tangent = float4(0.0f, 0.0f, 0.0f, 0.0f);

    uint index = constants.firstVertex + id.x;
    gPrevious[constants.firstPrevious + id.x] = float4(gVertices[index].position.xyz, 1.0f);
    gVertices[index] = vertex;
    gBuildVertices[index] = vertex;

#elif PASS == 4
    // --- Foam ---
    if (id.x >= uint(W * D))
        return;
    int i = int(id.x % uint(W)), j = int(id.x / uint(W));
    float h = gDepthIn[id.x];
    uint landed = gLanded[id.x];
    gLanded[id.x] = 0;
    if (h <= WET)
    {
        gFoamOut[id.x] = 0.0f;
        gChurn[id.x] = 0.0f;
        return;
    }

    // Carried by the current (semi-Lagrangian, bilinear between cell centres)
    float2 velocity = CellVelocity(i, j);
    float2 from = float2(i, j) - velocity * constants.frameDt / dx;
    from = clamp(from, float2(0.0f, 0.0f), float2(W - 1, D - 1));
    int fi = min(int(from.x), W - 2), fj = min(int(from.y), D - 2);
    fi = max(fi, 0);
    fj = max(fj, 0);
    float2 f = from - float2(fi, fj);
    float carried = lerp(lerp(gFoamIn[Cell(fi, fj)], gFoamIn[Cell(fi + 1, fj)], f.x),
                         lerp(gFoamIn[Cell(fi, fj + 1)], gFoamIn[Cell(fi + 1, fj + 1)], f.x), f.y);

    // Where the water breaks or churns
    float speed = length(velocity);
    float2 right = CellVelocity(i + 1, j), left = CellVelocity(i - 1, j), front = CellVelocity(i, j + 1), back = CellVelocity(i, j - 1);
    float divergence = ((right.x - left.x) + (front.y - back.y)) / (2.0f * dx);
    float own = gBed[id.x] + h;
    float slope = length(float2(Surface(i + 1, j) - Surface(i - 1, j), Surface(i, j + 1) - Surface(i, j - 1))) / (2.0f * dx);
    bool shore = Depth(i + 1, j) <= WET || Depth(i - 1, j) <= WET || Depth(i, j + 1) <= WET || Depth(i, j - 1) <= WET;
    float churn = saturate((slope - 0.2f) * 3.0f) * (0.3f + speed)   // A steep wave face: about to break
                + max(-divergence, 0.0f) * speed * 0.5f               // Converging flow: a wave front, a bore
                + (shore ? saturate(speed - 0.15f) * 1.5f : 0.0f)     // Running against the shore
                + max(speed - 1.5f, 0.0f) * 0.3f;                     // Fast, turbulent water
    float2 p = float2((i + 0.5f) * dx, (j + 0.5f) * dx);
    for (uint d = 0; d < constants.dropCount; d++)
    {
        float4 drop = gDrops[d];
        float r2 = dot(p - drop.xy, p - drop.xy) / (drop.z * drop.z);
        churn += exp(-r2) * drop.w * 60.0f; // A drop's impact
    }
    churn *= constants.foamAmount;
    gChurn[id.x] = churn;

    float decay = exp(-constants.frameDt / max(constants.foamLifetime, 0.05f));
    float foam = carried * decay + churn * constants.frameDt + float(landed) / 1000.0f;
    gFoamOut[id.x] = saturate(foam);

#elif PASS == 5
    // --- Emit spray ---
    if (id.x >= uint(W * D) || constants.particleCount == 0)
        return;
    int i = int(id.x % uint(W)), j = int(id.x / uint(W));
    float h = gDepthIn[id.x];
    if (h <= WET)
        return;
    float churn = gChurn[id.x] / max(constants.foamAmount, 1e-3f);
    // Spume: strong wind tears spray off the crests
    float2 velocity = CellVelocity(i, j);
    float slope = length(float2(Surface(i + 1, j) - Surface(i - 1, j), Surface(i, j + 1) - Surface(i, j - 1))) / (2.0f * dx);
    float spume = max(constants.windSpeed - 9.0f, 0.0f) * (0.02f + slope) * 0.4f;
    float rate = (churn * 3.0f + spume) * constants.sprayAmount * (dx * dx) * 150.0f; // Droplets per second from this cell

    uint seed = Pcg(id.x * 9781u + constants.frameIndex * 6271u);
    float expected = rate * constants.frameDt;
    uint count = uint(expected) + (Random(seed) < frac(expected) ? 1u : 0u);
    count = min(count, 12u);
    if (count == 0)
        return;
    uint first;
    InterlockedAdd(gParticleCounter[0], count, first);
    float2 wind = float2(cos(constants.windAngle), sin(constants.windAngle)) * constants.windSpeed;
    float energy = saturate(churn * 0.5f + spume * 0.2f);
    for (uint k = 0; k < count; k++)
    {
        uint slot = (first + k) % constants.particleCount;
        float3 position = float3(constants.originX + (i + Random(seed)) * dx, gBed[id.x] + h + 0.01f, constants.originZ + (j + Random(seed)) * dx);
        // Up out of the water, scattered, carried along with it (and with the wind for spume)
        float angle = Random(seed) * 6.2831853f;
        float spread = Random(seed);
        float up = lerp(0.8f, 4.5f, energy) * (0.5f + Random(seed));
        float2 sideways = velocity * 0.8f + float2(cos(angle), sin(angle)) * spread * up * 0.5f + wind * 0.1f * saturate(spume);
        float radius = constants.dropletSize * (0.4f + 1.2f * Random(seed));
        gParticles[slot * 2 + 0] = float4(position, 2.0f + 2.0f * Random(seed));
        gParticles[slot * 2 + 1] = float4(sideways.x, up, sideways.y, radius);
    }

#elif PASS == 6
    // --- Droplets ---
    if (id.x >= constants.particleCount)
        return;
    float4 a = gParticles[id.x * 2 + 0];
    float4 b = gParticles[id.x * 2 + 1];
    float3 position = a.xyz;
    float life = a.w;
    float3 velocity = b.xyz;
    float radius = b.w;
    float dt = constants.frameDt;

    if (life > 0.0f)
    {
        // Gravity, and air drag towards the wind (quadratic: small droplets slow and drift quickest)
        float2 wind = float2(cos(constants.windAngle), sin(constants.windAngle)) * constants.windSpeed;
        float3 relative = velocity - float3(wind.x, 0.0f, wind.y);
        float drag = 0.0006f / max(radius, 1e-4f); // ~ (air density / water density) * drag coefficient / radius
        velocity -= relative * length(relative) * drag * dt;
        velocity.y -= constants.gravity * dt;
        position += velocity * dt;
        life -= dt;

        // Back in the water (or on the ground): it lands, as foam where water is
        float2 cellPosition = float2(position.x - constants.originX, position.z - constants.originZ) / dx;
        int ci = int(floor(cellPosition.x)), cj = int(floor(cellPosition.y));
        bool inside = ci >= 0 && cj >= 0 && ci < W && cj < D;
        if (!inside)
            life = 0.0f;
        else
        {
            uint c = uint(cj * W + ci);
            float h = gDepthIn[c];
            float ground = gBed[c] + max(h, 0.0f);
            if (velocity.y < 0.0f && position.y < ground)
            {
                if (h > WET)
                {
                    uint ignored;
                    InterlockedAdd(gLanded[c], uint(clamp(radius * 4000.0f, 1.0f, 200.0f)), ignored);
                }
                life = 0.0f;
            }
        }
        gParticles[id.x * 2 + 0] = float4(position, life);
        gParticles[id.x * 2 + 1] = float4(velocity, radius);
        if (life > 0.0f)
        {
            uint ignoredCount;
            InterlockedAdd(gParticleCounter[1], 1u, ignoredCount); // Read back: no flying droplets, no structure rebuild
        }
    }

    // Its octahedron. While it is not flying its corners are NaN: inactive triangles, which the structure's build
    // skips (most droplets are not flying at any moment)
    float size = radius;
    if (life <= 0.0f)
        position = float3(asfloat(0x7fc00000u), 0.0f, 0.0f);
    const float3 corners[6] = { float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0), float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1) };
    for (uint v = 0; v < 6; v++)
    {
        Vertex vertex;
        vertex.position = float4(position + corners[v] * size, 1.0f);
        vertex.normal = float4(corners[v], 0.0f);
        vertex.uv = float2(0.0f, 0.0f);
        vertex.padding = float2(0.0f, 0.0f);
        vertex.tangent = float4(0.0f, 0.0f, 0.0f, 0.0f);
        uint index = constants.particleFirstVertex + id.x * 6 + v;
        float3 previous = gVertices[index].position.xyz;
        if (isnan(previous.x))
            previous = vertex.position.xyz; // Just thrown: no motion yet
        gPrevious[constants.particleFirstPrevious + id.x * 6 + v] = float4(previous, 1.0f);
        gVertices[index] = vertex;
        gBuildVertices[index] = vertex;
    }
#endif
}
