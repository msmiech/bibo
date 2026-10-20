#version 430 core
/*
* Fur strands of the bear (and the hedgehogs), generated on the fly: there are no vertex buffers, every vertex is computed from gl_VertexID.
* A strand grows from a point on a triangle of the skinned mesh (barycentric coordinates in the strand buffer) and is
* a quadratic Bezier curve: it leaves the skin along the normal, is combed backwards and down and bends with the
* guide hairs of the triangle (simulated on the CPU, see Fur.hpp). It is drawn as a ribbon that faces the camera.
* All strands are one triangle strip: the first and the last vertex of a strand are repeated, the degenerate
* triangles in between connect the strands without being drawn.
* With spines = 1 the strands are the spines of a hedgehog: straight, stiff, lying flatter on the skin and tapering to a point.
*/

struct SkinVertex
{
	vec4 position; //world space
	vec4 normal;
	vec4 bend; //offset of the guide hair tip in fur lengths
	vec4 comb; //direction the fur is combed in (tangent)
};
layout(std430, binding = 0) readonly buffer SkinVertices { SkinVertex skin[]; };
layout(std430, binding = 1) readonly buffer VertexColors { vec4 colors[]; }; //rgb: fur color, a: fur length (0..1)

struct Strand
{
	uvec4 vertices; //xyz: the vertices of the triangle, w: random seed
	vec4 parameters; //xy: barycentric weights of the first two vertices, z: length scale, w: shade
};
layout(std430, binding = 2) readonly buffer Strands { Strand strands[]; };

uniform mat4 view;
uniform mat4 projection;
uniform vec3 worldEye;
uniform int segments; //of every strand
uniform float maxFurLength; //world units for fur length 1
uniform float strandWidth; //at the root (world units)
uniform float pixelSize; //world units per pixel at a distance of 1 from the camera
uniform int spines; //1: hedgehog spines instead of fur

out vec3 worldPosition;
out vec3 strandTangent;
out vec3 skinNormal;
out vec3 furColor;
out float strandParameter; //0 at the root, 1 at the tip
out float coverage; //how much of the pixel the strand covers

uint hash(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

float random(uint seed, uint n)
{
	return float(hash(seed + n * 0x9e3779b9u) >> 8) / 16777216.0;
}

void main()
{
	int verticesPerStrand = 2 * (segments + 1) + 2;
	int strandIndex = gl_VertexID / verticesPerStrand;
	int vertexIndex = clamp(gl_VertexID % verticesPerStrand - 1, 0, 2 * segments + 1);
	int segment = vertexIndex / 2;
	float side = (vertexIndex % 2 == 0) ? -0.5 : 0.5;

	Strand strand = strands[strandIndex];
	uint seed = strand.vertices.w;
	vec3 weights = vec3(strand.parameters.xy, 1.0 - strand.parameters.x - strand.parameters.y);
	SkinVertex a = skin[strand.vertices.x];
	SkinVertex b = skin[strand.vertices.y];
	SkinVertex c = skin[strand.vertices.z];
	vec3 root = weights.x * a.position.xyz + weights.y * b.position.xyz + weights.z * c.position.xyz;
	vec3 normal = normalize(weights.x * a.normal.xyz + weights.y * b.normal.xyz + weights.z * c.normal.xyz);
	vec3 bend = weights.x * a.bend.xyz + weights.y * b.bend.xyz + weights.z * c.bend.xyz;
	vec3 comb = weights.x * a.comb.xyz + weights.y * b.comb.xyz + weights.z * c.comb.xyz;
	comb = comb - normal * dot(comb, normal);
	comb = dot(comb, comb) > 1e-8 ? normalize(comb) : normalize(cross(normal, vec3(0.37, 0.71, 0.6)));
	vec4 color = weights.x * colors[strand.vertices.x] + weights.y * colors[strand.vertices.y] + weights.z * colors[strand.vertices.z];
	float furLength = color.a * maxFurLength * strand.parameters.z;

	//Every strand is combed a bit differently and stands up more or less
	bool stiff = spines == 1;
	float angle = (random(seed, 1u) - 0.5) * (stiff ? 0.7 : 1.1);
	vec3 combed = cos(angle) * comb + sin(angle) * cross(normal, comb);
	float lift = stiff ? 0.3 + 0.25 * random(seed, 2u) : 0.42 + 0.3 * random(seed, 2u);
	vec3 restDirection = normalize(normal * lift + combed * (1.0 - lift) * 1.5);
	vec3 tipDirection = normalize(restDirection + bend);

	//Bezier curve: the root is a bit inside of the skin, so that no gaps show where the strand leaves it
	vec3 p0 = root - normal * (0.08 * furLength);
	vec3 p1 = stiff ? 0.5 * (p0 + root + tipDirection * furLength) : root + normalize(normal + restDirection) * (0.45 * furLength) + bend * (0.12 * furLength);
	vec3 p2 = root + tipDirection * furLength;
	float t = float(segment) / float(segments);
	vec3 position = mix(mix(p0, p1, t), mix(p1, p2, t), t);
	vec3 tangent = mix(p1 - p0, p2 - p1, t);
	tangent = dot(tangent, tangent) > 1e-12 ? normalize(tangent) : normal;

	//Ribbon across the strand, facing the camera. Strands thinner than a pixel are drawn one pixel wide but transparent
	//(otherwise they would flicker), the tip tapers off.
	vec3 toEye = worldEye - position;
	float distance = length(toEye);
	vec3 across = cross(tangent, toEye);
	across = dot(across, across) > 1e-12 ? normalize(across) : cross(tangent, normal);
	float width = strandWidth * (1.0 - (stiff ? 0.92 : 0.8) * t);
	float onePixel = pixelSize * distance;
	coverage = clamp(width / onePixel, 0.0, 1.0);
	position += across * (side * max(width, onePixel));
	if (furLength < 1e-4)
	{
		position = root; //no fur here: degenerate, nothing is drawn
	}

	worldPosition = position;
	strandTangent = tangent;
	skinNormal = normal;
	furColor = color.rgb * (0.85 + 0.3 * strand.parameters.w);
	strandParameter = t;
	gl_Position = projection * view * vec4(position, 1.0);
}
