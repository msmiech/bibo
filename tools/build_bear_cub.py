"""
Builds the player character of BIBO, a young brown bear (cub), and exports it as Model/BearCub/BearCub.glb.

Run with Blender 4.2 or newer (tested with 5.2) in the folder of the repository:

    blender --background --factory-startup --python tools/build_bear_cub.py

Optional arguments after "--": --preview <folder> renders preview images of the model (rest pose and test poses).

The model is generated, so every step can be repeated and tweaked:
1. The body is a signed distance field of soft-blended primitives (spheres, ellipsoids, tapered capsules) in PRIMITIVES.
   The mouth is carved out of it (CARVINGS). Its surface is extracted with surface nets and remeshed into evenly sized quads.
2. The mouth is modelled open (JAW_OPEN_DEGREES), so that its inside is resolved well; the game closes it with the jaw bone.
   The tongue, the teeth, the eyes, their eyelids and the nose are separate meshes.
3. The skeleton (bone_table) is made for procedural animation and inverse kinematics: a spine chain, a head with ears, jaw,
   tongue, eyes and eyelids, legs with two IK bones each (upperarm/forearm, thigh/shin) plus a paw, and a tail.
   The skin weights of the body are computed by Blender (automatic weights, bone heat), the jaw weights are set by its shape.
4. Vertex colors (COLOR_0): RGB is the fur color, A is the fur length (0 = no fur, 1 = longest fur, see fur::MAX_LENGTH in the game).
   The game grows the fur strands from the surface, so the body is a bit smaller than the bear with its fur.
   The eyes store the direction from the center of the eye instead (eye space, gaze along +Z): the game draws iris and pupil with it.
5. Materials (by name): BearFur, BearEye, BearNose, BearMouth (inside of the mouth), BearTongue, BearTeeth.

Coordinates: Blender Z is up, the bear looks along -Y, its left side is +X (exported as glTF: Y up, looking along +Z).
The origin is on the ground between the feet, under the center of the body.
"""
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

REPOSITORY = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT_DIR = os.path.join(REPOSITORY, "Model", "BearCub")
GLB_FILE = os.path.join(OUTPUT_DIR, "BearCub.glb")
BLEND_FILE = os.path.join(OUTPUT_DIR, "BearCub.blend")

GRID_SPACING = 0.015  # distance field sampling (game units)
TARGET_FACES = 13000  # quads of the remeshed body

# The jaw hinges around the X axis through this point; the model is made with the mouth this far open (the game closes it)
HINGE = np.array([0.0, -1.12, 1.5])
JAW_OPEN_DEGREES = 24.0
MOUTH_HALF_WIDTH = 0.13


def srgb(r, g, b):
    """Color in sRGB (0-255) to linear RGB, like the vertex colors are stored"""
    def channel(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return np.array([channel(r), channel(g), channel(b)])


# Palette of a young brown bear: warm brown with lighter tips, cream muzzle and collar, dark paw pads and lips
COLORS = {
    "fur": srgb(112, 76, 50),
    "back": srgb(88, 58, 38),
    "belly": srgb(138, 102, 72),
    "muzzle": srgb(190, 156, 116),
    "face": srgb(126, 88, 58),
    "inner_ear": srgb(168, 124, 98),
    "collar": srgb(208, 184, 150),
    "paw": srgb(78, 54, 38),
    "pad": srgb(52, 38, 34),
    "lips": srgb(44, 30, 28),
    "lid_margin": srgb(30, 22, 20),
    "lid_inside": srgb(92, 52, 48),
    "nose": srgb(28, 22, 21),
    "nostril": srgb(8, 6, 6),
    "mouth": srgb(120, 52, 56),
    "tongue": srgb(206, 102, 112),
    "teeth": srgb(238, 226, 196),
    "claw": srgb(214, 196, 162),
    "claw_root": srgb(96, 78, 62),
}


# ---------------------------------------------------------------------------
# Signed distance functions (vectorized with numpy, p is an (N, 3) array)
# ---------------------------------------------------------------------------
def sd_sphere(p, center, radius):
    return np.linalg.norm(p - center, axis=1) - radius


def sd_ellipsoid(p, center, radii, rotation=None):
    """Approximate distance to an ellipsoid (Inigo Quilez), rotation is a 3x3 matrix from local to world"""
    q = p - center
    if rotation is not None:
        q = q @ rotation  # world to local (q_local = R^T q)
    k0 = np.linalg.norm(q / radii, axis=1)
    k1 = np.linalg.norm(q / (radii * radii), axis=1)
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)


def sd_round_cone(p, a, b, r1, r2):
    """Capsule from a (radius r1) to b (radius r2) (Inigo Quilez)"""
    ba = b - a
    l2 = ba @ ba
    rr = r1 - r2
    a2 = l2 - rr * rr
    il2 = 1.0 / l2
    pa = p - a
    y = pa @ ba
    z = y - l2
    x = pa * l2 - y[:, None] * ba
    x2 = np.einsum("ij,ij->i", x, x)
    y2 = y * y * l2
    z2 = z * z * l2
    k = np.sign(rr) * rr * rr * x2
    return np.where(np.sign(z) * a2 * z2 > k, np.sqrt(x2 + z2) * il2 - r2,
                    np.where(np.sign(y) * a2 * y2 < k, np.sqrt(x2 + y2) * il2 - r1,
                             (np.sqrt(np.maximum(x2 * a2 * il2, 0.0)) + y * rr) * il2 - r1))


def smooth_min(a, b, k):
    if k <= 0.0:
        return np.minimum(a, b)
    h = np.maximum(k - np.abs(a - b), 0.0) / k
    return np.minimum(a, b) - h * h * k * 0.25


def smooth_max(a, b, k):
    return -smooth_min(-a, -b, k)


def rotation_xyz(degrees):
    rx, ry, rz = (math.radians(a) for a in degrees)
    return np.array(Matrix.Rotation(rz, 3, "Z") @ Matrix.Rotation(ry, 3, "Y") @ Matrix.Rotation(rx, 3, "X"))


def rotation_x(degrees):
    a = math.radians(degrees)
    return np.array([[1.0, 0.0, 0.0], [0.0, math.cos(a), -math.sin(a)], [0.0, math.sin(a), math.cos(a)]])


JAW_OPEN = rotation_x(JAW_OPEN_DEGREES)  # positive rotation around +X moves the front (-Y) down


def open_jaw(points):
    """Points of the closed lower jaw to where they are in the model (mouth open)"""
    return (np.atleast_2d(points) - HINGE) @ JAW_OPEN.T + HINGE


def closed_jaw(points):
    """Points of the model to where they would be with the mouth closed"""
    return (np.atleast_2d(points) - HINGE) @ JAW_OPEN + HINGE


class Primitive:
    """
    Part of the body. blend: how softly it merges with the parts before it.
    color, fur: color name and fur length (0..1) of the surface that belongs to this part.
    jaw: the part belongs to the lower jaw, its shape is given with the mouth closed.
    """
    def __init__(self, name, kind, blend, color, fur, jaw=False, **shape):
        self.name, self.kind, self.blend, self.color, self.fur, self.jaw, self.shape = name, kind, blend, color, fur, jaw, shape

    def distance(self, p):
        if self.jaw:
            p = closed_jaw(p)
        s = self.shape
        if self.kind == "sphere":
            return sd_sphere(p, np.array(s["center"]), s["radius"])
        if self.kind == "ellipsoid":
            rotation = rotation_xyz(s["rotation"]) if "rotation" in s else None
            return sd_ellipsoid(p, np.array(s["center"]), np.array(s["radii"]), rotation)
        if self.kind == "cone":
            return sd_round_cone(p, np.array(s["a"]), np.array(s["b"]), s["r1"], s["r2"])
        raise ValueError(self.kind)

    def bounds(self):
        s = self.shape
        if self.kind == "sphere":
            c, r = np.array(s["center"]), s["radius"]
            return c - r, c + r
        if self.kind == "ellipsoid":
            c, r = np.array(s["center"]), max(s["radii"])
            return c - r, c + r
        a, b, r = np.array(s["a"]), np.array(s["b"]), max(s["r1"], s["r2"])
        return np.minimum(a, b) - r, np.maximum(a, b) + r

    def mirrored(self):
        """Same part on the right side (-X)"""
        s = dict(self.shape)
        for key in ("center", "a", "b"):
            if key in s:
                s[key] = (-s[key][0], s[key][1], s[key][2])
        if "rotation" in s:
            rx, ry, rz = s["rotation"]
            s["rotation"] = (rx, -ry, -rz)
        return Primitive(self.name.replace(".L", ".R"), self.kind, self.blend, self.color, self.fur, self.jaw, **s)


def both_sides(*primitives):
    result = []
    for primitive in primitives:
        result += [primitive, primitive.mirrored()]
    return result


# The cub: round chubby body, a big head with a broad skull, brows, a bear's muzzle and round ears, stubby legs with big paws.
CRANIUM_CENTER = (0.0, -0.98, 1.74)
MUZZLE_TIP = (0.0, -1.56, 1.6)
PRIMITIVES = [
    # Body
    Primitive("torso", "ellipsoid", 0.0, "fur", 0.8, center=(0.0, 0.0, 0.98), radii=(0.56, 0.64, 0.5)),
    Primitive("chest", "sphere", 0.3, "fur", 0.85, center=(0.0, -0.4, 1.06), radius=0.5),
    Primitive("rump", "sphere", 0.3, "fur", 0.8, center=(0.0, 0.46, 1.0), radius=0.47),
    Primitive("belly", "ellipsoid", 0.25, "belly", 0.7, center=(0.0, -0.02, 0.84), radii=(0.48, 0.5, 0.4)),
    Primitive("hump", "ellipsoid", 0.25, "back", 1.0, center=(0.0, -0.32, 1.4), radii=(0.34, 0.36, 0.18)),
    Primitive("neck", "cone", 0.25, "fur", 1.0, a=(0.0, -0.54, 1.22), b=(0.0, -0.88, 1.5), r1=0.38, r2=0.34),
    # Head: broad skull, brow ridges over the eyes, a straight nose bridge into the muzzle
    Primitive("cranium", "ellipsoid", 0.18, "fur", 0.7, center=CRANIUM_CENTER, radii=(0.42, 0.42, 0.38)),
    *both_sides(Primitive("brow.L", "ellipsoid", 0.07, "face", 0.45, center=(0.17, -1.29, 1.9), radii=(0.12, 0.08, 0.055), rotation=(0.0, 0.0, -18.0))),
    *both_sides(Primitive("cheek.L", "sphere", 0.1, "fur", 1.0, center=(0.27, -1.07, 1.6), radius=0.23)),
    Primitive("bridge", "cone", 0.08, "face", 0.35, a=(0.0, -1.12, 1.79), b=(0.0, -1.5, 1.69), r1=0.12, r2=0.075),
    Primitive("muzzle", "cone", 0.09, "muzzle", 0.28, a=(0.0, -1.15, 1.64), b=MUZZLE_TIP, r1=0.19, r2=0.115),
    # Lower jaw (with the mouth closed, the lips meet at the height of the hinge)
    Primitive("jaw", "cone", 0.07, "muzzle", 0.26, jaw=True, a=(0.0, -1.14, 1.44), b=(0.0, -1.52, 1.445), r1=0.15, r2=0.088),
    Primitive("chin", "ellipsoid", 0.05, "muzzle", 0.25, jaw=True, center=(0.0, -1.44, 1.41), radii=(0.085, 0.09, 0.05)),
    *both_sides(Primitive("ear.L", "ellipsoid", 0.06, "fur", 0.6, center=(0.36, -0.99, 2.13), radii=(0.14, 0.065, 0.14), rotation=(0.0, -26.0, 0.0))),
    # Front legs
    *both_sides(
        Primitive("upperarm.L", "cone", 0.16, "fur", 0.75, a=(0.36, -0.46, 1.0), b=(0.39, -0.49, 0.48), r1=0.22, r2=0.17),
        Primitive("forearm.L", "cone", 0.06, "fur", 0.6, a=(0.39, -0.49, 0.48), b=(0.4, -0.52, 0.17), r1=0.17, r2=0.15),
        Primitive("hand.L", "ellipsoid", 0.08, "paw", 0.45, center=(0.4, -0.59, 0.1), radii=(0.18, 0.23, 0.1))),
    # Hind legs (plantigrade: the whole long foot is on the ground)
    *both_sides(
        Primitive("thigh.L", "ellipsoid", 0.18, "fur", 0.8, center=(0.34, 0.46, 0.82), radii=(0.25, 0.34, 0.36)),
        Primitive("shin.L", "cone", 0.08, "fur", 0.6, a=(0.37, 0.38, 0.46), b=(0.38, 0.52, 0.17), r1=0.17, r2=0.14),
        Primitive("foot.L", "ellipsoid", 0.08, "paw", 0.45, center=(0.38, 0.41, 0.09), radii=(0.18, 0.29, 0.09))),
    # Stubby tail
    Primitive("tail", "sphere", 0.08, "fur", 1.0, center=(0.0, 0.97, 1.1), radius=0.13),
]

# The inside of the mouth: a cavity between palate and tongue (the same shape in the upper and in the lower jaw,
# so it is closed with the mouth), and the wedge that the opened jaw leaves between the lips
CAVITY = dict(center=np.array([0.0, -1.31, 1.5]), radii=np.array([0.095, 0.18, 0.035]))


def mouth_wedge_distance(p):
    """Between the plane of the upper lip (through the hinge) and the plane of the opened lower lip, in front of the hinge"""
    q = p - HINGE
    above_upper = q[:, 2] + 0.004
    lower_normal = JAW_OPEN @ np.array([0.0, 0.0, 1.0])
    below_lower = -(q @ lower_normal) + 0.004
    behind_hinge = q[:, 1] + 0.05
    beside = np.abs(q[:, 0]) - MOUTH_HALF_WIDTH
    return np.maximum.reduce([above_upper, below_lower, behind_hinge, beside])


def carving_distance(p):
    """Solid that is removed from the body (the inside of the mouth)"""
    cavity_upper = sd_ellipsoid(p, CAVITY["center"], CAVITY["radii"])
    cavity_lower = sd_ellipsoid(closed_jaw(p), CAVITY["center"], CAVITY["radii"])
    return smooth_min(smooth_min(cavity_upper, cavity_lower, 0.01), mouth_wedge_distance(p), 0.015)


def union_distance(p, primitives=PRIMITIVES):
    d = primitives[0].distance(p)
    for primitive in primitives[1:]:
        d = smooth_min(d, primitive.distance(p), primitive.blend)
    return d


def body_distance(p):
    return smooth_max(union_distance(p), -carving_distance(p), 0.012)


def body_gradient(p, eps=1e-3):
    g = np.zeros_like(p)
    for axis in range(3):
        offset = np.zeros(3)
        offset[axis] = eps
        g[:, axis] = body_distance(p + offset) - body_distance(p - offset)
    return g / (2.0 * eps)


def project_to_surface(p, iterations=4):
    """Moves points onto the surface (d = 0) along the gradient (Newton steps)"""
    for _ in range(iterations):
        d = body_distance(p)
        g = body_gradient(p)
        p = p - (d / np.maximum(np.einsum("ij,ij->i", g, g), 1e-9))[:, None] * g
    return p


# ---------------------------------------------------------------------------
# Surface extraction
# ---------------------------------------------------------------------------
def sample_grid():
    lower = np.min([primitive.bounds()[0] for primitive in PRIMITIVES], axis=0) - 0.1
    upper = np.max([primitive.bounds()[1] for primitive in PRIMITIVES], axis=0) + 0.1
    lower[0], upper[0] = -max(-lower[0], upper[0]), max(-lower[0], upper[0])  # symmetric around x = 0
    counts = np.ceil((upper - lower) / GRID_SPACING).astype(int) + 1
    xs, ys, zs = (lower[i] + GRID_SPACING * np.arange(counts[i]) for i in range(3))
    distance = np.empty(counts)
    for i, x in enumerate(xs):  # one slice at a time keeps the memory small
        gy, gz = np.meshgrid(ys, zs, indexing="ij")
        points = np.stack([np.full(gy.size, x), gy.ravel(), gz.ravel()], axis=1)
        distance[i] = body_distance(points).reshape(gy.shape)
    return distance, lower


def surface_nets(distance, origin, spacing):
    """Quads on the zero crossing of the distance field (one vertex per cell that the surface passes through)"""
    inside = distance < 0.0
    nx, ny, nz = distance.shape
    corners = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0), (0, 0, 1), (1, 0, 1), (0, 1, 1), (1, 1, 1)]
    config = np.zeros((nx - 1, ny - 1, nz - 1), dtype=np.uint8)
    for bit, (i, j, k) in enumerate(corners):
        config |= inside[i:nx - 1 + i, j:ny - 1 + j, k:nz - 1 + k].astype(np.uint8) << bit
    active = (config != 0) & (config != 255)
    cells = np.argwhere(active)
    vertex_of_cell = -np.ones(config.shape, dtype=np.int64)
    vertex_of_cell[active] = np.arange(len(cells))

    edges = [(0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)]
    position_sum = np.zeros((len(cells), 3))
    crossing_count = np.zeros(len(cells))
    for a, b in edges:
        ca, cb = np.array(corners[a]), np.array(corners[b])
        da = distance[cells[:, 0] + ca[0], cells[:, 1] + ca[1], cells[:, 2] + ca[2]]
        db = distance[cells[:, 0] + cb[0], cells[:, 1] + cb[1], cells[:, 2] + cb[2]]
        crossing = (da < 0.0) != (db < 0.0)
        t = np.where(crossing, da / np.where(crossing, da - db, 1.0), 0.0)
        point = cells + ca + t[:, None] * (cb - ca)
        position_sum[crossing] += point[crossing]
        crossing_count[crossing] += 1
    vertices = origin + spacing * position_sum / crossing_count[:, None]

    faces = []
    for axis in range(3):
        u, v = (axis + 1) % 3, (axis + 2) % 3
        # grid edges along the axis between point i and i + 1 whose ends are on different sides of the surface
        start = [slice(None)] * 3
        end = [slice(None)] * 3
        start[axis], end[axis] = slice(0, -1), slice(1, None)
        crossing = inside[tuple(start)] != inside[tuple(end)]
        # the four cells around the edge are at u - 1 / u and v - 1 / v, so the edges at the border of the grid are skipped
        interior = [slice(None)] * 3
        interior[u], interior[v] = slice(1, crossing.shape[u] - 1), slice(1, crossing.shape[v] - 1)
        crossing_interior = np.zeros_like(crossing)
        crossing_interior[tuple(interior)] = crossing[tuple(interior)]
        edge_points = np.argwhere(crossing_interior)
        starts_inside = inside[edge_points[:, 0], edge_points[:, 1], edge_points[:, 2]]
        quad = []
        for du, dv in ((-1, -1), (0, -1), (0, 0), (-1, 0)):
            cell = edge_points.copy()
            cell[:, u] += du
            cell[:, v] += dv
            quad.append(vertex_of_cell[cell[:, 0], cell[:, 1], cell[:, 2]])
        quad = np.stack(quad, axis=1)
        quad[~starts_inside] = quad[~starts_inside][:, ::-1]  # the normal points out of the body
        faces.append(quad)
    faces = np.concatenate(faces)
    assert (faces >= 0).all()
    return vertices, faces


# ---------------------------------------------------------------------------
# Scene
# ---------------------------------------------------------------------------
def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def select_only(*objects, active=None):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = active or objects[0]


def link(name, mesh):
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def mesh_from_bmesh(name, bm, smooth=True):
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    for polygon in mesh.polygons:
        polygon.use_smooth = smooth
    return link(name, mesh)


def create_body_mesh():
    print("Sampling the distance field...")
    distance, origin = sample_grid()
    print("Extracting the surface...")
    vertices, faces = surface_nets(distance, origin, GRID_SPACING)
    vertices = project_to_surface(vertices)
    mesh = bpy.data.meshes.new("BearCubBody")
    mesh.from_pydata(vertices.tolist(), [], faces.tolist())
    mesh.validate()
    body = link("BearCub", mesh)
    select_only(body)
    print(f"Surface nets: {len(mesh.vertices)} vertices, {len(mesh.polygons)} faces")

    # Where parts almost touch, surface nets can create edges with more than two faces: the voxel remesher (OpenVDB)
    # always creates a closed manifold surface, which QuadriFlow needs
    mesh.remesh_voxel_size = GRID_SPACING
    bpy.ops.object.voxel_remesh()
    # Only the outer surface: tiny closed bubbles (e.g. where the cavity of the mouth almost touches the skin) are removed
    bm = bmesh.new()
    bm.from_mesh(body.data)
    components = []
    unvisited = set(bm.verts)
    while unvisited:
        stack = [unvisited.pop()]
        component = set(stack)
        while stack:
            for edge in stack.pop().link_edges:
                other = edge.verts[1] if edge.verts[0] in component else edge.verts[0]
                if other in unvisited:
                    unvisited.remove(other)
                    component.add(other)
                    stack.append(other)
        components.append(component)
    components.sort(key=len, reverse=True)
    if len(components) > 1:
        print(f"Removed {len(components) - 1} small closed parts inside of the body")
    bmesh.ops.delete(bm, geom=[v for c in components[1:] for v in c], context="VERTS")
    # QuadriFlow also rejects (almost) zero length edges, e.g. where the corners of the mouth pinch the surface
    bmesh.ops.dissolve_degenerate(bm, dist=2e-4, edges=bm.edges)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(body.data)
    bm.free()
    print("Remeshing (QuadriFlow)...")
    result = bpy.ops.object.quadriflow_remesh(target_faces=TARGET_FACES, seed=7, use_mesh_symmetry=False,
                                              use_preserve_sharp=False, use_preserve_boundary=False, smooth_normals=True)
    if result != {"FINISHED"}:
        raise RuntimeError("QuadriFlow could not remesh the body")
    mesh = body.data
    # QuadriFlow only approximates the surface: relax the quads and put the vertices back onto the surface
    for _ in range(3):
        coordinates = np.empty(len(mesh.vertices) * 3)
        mesh.vertices.foreach_get("co", coordinates)
        coordinates = project_to_surface(coordinates.reshape(-1, 3))
        mesh.vertices.foreach_set("co", coordinates.ravel())
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.smooth_vert(bm, verts=bm.verts, factor=0.5, use_axis_x=True, use_axis_y=True, use_axis_z=True)
        bm.to_mesh(mesh)
        bm.free()
    coordinates = np.empty(len(mesh.vertices) * 3)
    mesh.vertices.foreach_get("co", coordinates)
    mesh.vertices.foreach_set("co", project_to_surface(coordinates.reshape(-1, 3)).ravel())
    mesh.update()
    for polygon in mesh.polygons:
        polygon.use_smooth = True
    print(f"Body: {len(mesh.vertices)} vertices, {len(mesh.polygons)} faces")
    return body


def surface_point(origin, direction, sink=0.0):
    """First point of the body surface on a ray that starts outside of the body, moved into the body by sink"""
    origin, direction = np.array(origin, dtype=float), np.array(direction, dtype=float)
    direction /= np.linalg.norm(direction)
    t = 0.0
    for _ in range(200):
        d = body_distance((origin + t * direction)[None, :])[0]
        if d < 1e-4:
            break
        t += d
    return origin + (t + sink) * direction


def surface_point_from_inside(inside, outward, sink=0.0):
    """Where a ray from a point inside of the body leaves it, moved back into the body by sink"""
    outward = np.array(outward, dtype=float) / np.linalg.norm(outward)
    return surface_point(np.array(inside, dtype=float) + 3.0 * outward, -outward, sink)


class EyeFrame:
    """Center of an eye and its axes: gaze (forward), up and side"""
    def __init__(self, center, gaze):
        self.center = np.array(center, dtype=float)
        self.gaze = np.array(gaze, dtype=float) / np.linalg.norm(gaze)
        up = np.array([0.0, 0.0, 1.0])
        self.up = up - self.gaze * (up @ self.gaze)
        self.up /= np.linalg.norm(self.up)
        self.side = np.cross(self.up, self.gaze)

    def local(self, direction):
        return np.stack([direction @ self.side, direction @ self.up, direction @ self.gaze], axis=-1)


def create_eye(name, frame, radius):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=40, v_segments=28, radius=radius)
    bmesh.ops.translate(bm, verts=bm.verts, vec=Vector(frame.center))
    return mesh_from_bmesh(name, bm)


def create_eyelid(name, frame, radius, upper):
    """
    Shell around the eye (outer skin, inner side and the rim between them). It covers the eye from the top (or bottom)
    down to its edge; the game rotates it around the center of the eye to close the eye.
    """
    axis = frame.up if upper else -frame.up
    start, edge = (-40.0, 54.0) if upper else (-30.0, 65.0)  # angle from the up (down) axis towards the gaze
    thetas = np.radians(np.linspace(start, edge, 14))
    phis = np.radians(np.linspace(-108.0, 108.0, 25))
    outer, inner = 1.16 * radius, 1.05 * radius

    def direction(theta, phi):
        return math.cos(theta) * axis + math.sin(theta) * (math.cos(phi) * frame.gaze + math.sin(phi) * frame.side)

    bm = bmesh.new()
    grid_outer = [[bm.verts.new(Vector(frame.center + outer * direction(t, f))) for f in phis] for t in thetas]
    grid_inner = [[bm.verts.new(Vector(frame.center + inner * direction(t, f))) for f in phis] for t in thetas]
    for i in range(len(thetas) - 1):
        for j in range(len(phis) - 1):
            bm.faces.new((grid_outer[i][j], grid_outer[i + 1][j], grid_outer[i + 1][j + 1], grid_outer[i][j + 1]))
            bm.faces.new((grid_inner[i][j], grid_inner[i][j + 1], grid_inner[i + 1][j + 1], grid_inner[i + 1][j]))
    # rims: at the edge of the lid (visible), at the other end and at the sides (inside of the head)
    last = len(thetas) - 1
    for j in range(len(phis) - 1):
        for i in (0, last):
            bm.faces.new((grid_outer[i][j], grid_outer[i][j + 1], grid_inner[i][j + 1], grid_inner[i][j]))
    for i in range(len(thetas) - 1):
        for j in (0, len(phis) - 1):
            bm.faces.new((grid_outer[i][j], grid_inner[i][j], grid_inner[i + 1][j], grid_outer[i + 1][j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return mesh_from_bmesh(name, bm)


def create_nose(center):
    """Wide rounded nose of a bear with two nostrils at the front"""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=20, radius=1.0)
    for vertex in bm.verts:
        x, y, z = vertex.co
        narrowing = 1.0 - 0.3 * max(0.0, -z)  # narrower towards the bottom
        flat = 1.0 - 0.25 * max(0.0, -y)  # flatter front
        position = Vector((x * narrowing, y * flat, z))
        # nostrils: dents in the lower front, left and right
        for side in (-1.0, 1.0):
            nostril = math.exp(-((x - 0.42 * side) ** 2 + (z + 0.15) ** 2) / 0.035) * max(0.0, -y)
            position *= 1.0 - 0.28 * nostril
        vertex.co = Vector((0.105 * position.x, 0.065 * position.y, 0.072 * position.z))
    bmesh.ops.translate(bm, verts=bm.verts, vec=Vector(center))
    return mesh_from_bmesh("Nose", bm)


def nostril_amount(points, center):
    q = (points - np.array(center)) / np.array([0.105, 0.065, 0.072])
    amount = np.zeros(len(points))
    for side in (-1.0, 1.0):
        amount += np.exp(-((q[:, 0] - 0.42 * side) ** 2 + (q[:, 2] + 0.15) ** 2) / 0.02) * np.clip(-q[:, 1], 0.0, 1.0)
    return np.clip(amount * 1.5, 0.0, 1.0)


TONGUE_CENTER = np.array([0.0, -1.32, 1.483])  # with the mouth closed, on the floor of the mouth
TONGUE_HALF = np.array([0.066, 0.16, 0.019])


def create_tongue():
    """Flat rounded tongue with a groove along the middle, made with the mouth closed and opened like the jaw"""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=28, v_segments=16, radius=1.0)
    for vertex in bm.verts:
        x, y, z = vertex.co
        tip = max(0.0, -y)  # front half
        width = 1.0 - 0.25 * tip * tip
        groove = 0.35 * math.exp(-x * x / 0.03) if z > 0.0 else 0.0
        position = np.array([x * width, y, z - groove * z]) * TONGUE_HALF + TONGUE_CENTER
        vertex.co = Vector(open_jaw(position)[0])
    return mesh_from_bmesh("Tongue", bm)


def create_tooth(name, root, tip, radius, upper):
    """A tooth from its root (in the gum) to its tip, made with the mouth closed; the lower teeth are opened like the jaw"""
    root, tip = np.array(root, dtype=float), np.array(tip, dtype=float)
    length = np.linalg.norm(tip - root)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=10, radius1=radius, radius2=0.15 * radius, depth=length)
    rotation = Vector((0.0, 0.0, 1.0)).rotation_difference(Vector(tip - root)).to_matrix()  # the cone points along +Z
    for vertex in bm.verts:
        point = np.array(rotation @ vertex.co) + 0.5 * (root + tip)
        vertex.co = Vector(point if upper else open_jaw(point)[0])
    return mesh_from_bmesh(name, bm), upper


def create_teeth():
    """
    Canines and small incisors. With the mouth closed they are hidden in the other jaw, when it opens they show:
    the gum line is the plane of the lips (the height of the hinge), the teeth reach across it into the mouth.
    """
    lip = HINGE[2]
    teeth = []
    for side in (-1.0, 1.0):
        s = "L" if side > 0 else "R"
        # the lower canines stand in front of the upper ones, both a bit inside of the lips
        teeth.append(create_tooth(f"Canine.U.{s}", (0.066 * side, -1.53, lip + 0.025), (0.062 * side, -1.535, lip - 0.03), 0.017, True))
        teeth.append(create_tooth(f"Canine.L.{s}", (0.054 * side, -1.552, lip - 0.025), (0.05 * side, -1.548, lip + 0.024), 0.015, False))
    for i, x in enumerate(np.linspace(-0.04, 0.04, 6)):
        y = -1.597 + 0.012 * (x / 0.04) ** 2
        teeth.append(create_tooth(f"Incisor.U.{i}", (x, y, lip + 0.012), (x, y + 0.002, lip - 0.014), 0.0085, True))
    for i, x in enumerate(np.linspace(-0.03, 0.03, 4)):
        y = -1.57 + 0.01 * (x / 0.03) ** 2
        teeth.append(create_tooth(f"Incisor.L.{i}", (x, y, lip - 0.012), (x, y - 0.002, lip + 0.013), 0.008, False))
    return teeth


# Toes: five in an arc at the front of every paw, with a curved claw each (front claws are longer)
def toe_positions(paw_center, paw_radii, front):
    toes = []
    for i, offset in enumerate(np.linspace(-1.0, 1.0, 5)):
        dx = 0.115 * offset
        y = paw_center[1] - paw_radii[1] + (0.05 if front else 0.045) + 0.035 * offset * offset
        z = paw_center[2] - 0.012 - 0.01 * offset * offset
        toes.append(np.array([paw_center[0] + dx, y, z]))
    return toes


FRONT_PAW = ((0.4, -0.59, 0.1), (0.18, 0.23, 0.1))
HIND_PAW = ((0.38, 0.41, 0.09), (0.18, 0.29, 0.09))
TOES = []  # (position, front, side, bone)
for paw, front, bone in ((FRONT_PAW, True, "hand"), (HIND_PAW, False, "foot")):
    for side in (1.0, -1.0):
        center = (paw[0][0] * side, paw[0][1], paw[0][2])
        for toe in toe_positions(center, paw[1], front):
            TOES.append((toe, front, side, f"{bone}.{'L' if side > 0 else 'R'}"))
PRIMITIVES += [Primitive(f"toe.{i}", "ellipsoid", 0.03, "paw", 0.4, center=tuple(toe), radii=(0.045, 0.05, 0.046))
               for i, (toe, _, _, _) in enumerate(TOES)]


def create_claw(name, base, forward, length, radius):
    """Curved claw: a tapered tube along a curve that goes forward and bends down, flattened from the sides"""
    forward = np.array(forward, dtype=float) / np.linalg.norm(forward)
    down = np.array([0.0, 0.0, -1.0])
    side = np.cross(forward, down)
    p0, p1, p2 = base, base + forward * 0.65 * length, base + forward * 0.85 * length + down * 0.6 * length
    rings, segments = 9, 10
    bm = bmesh.new()
    ring_vertices = []
    for i in range(rings):
        t = i / (rings - 1)
        center = (1 - t) ** 2 * p0 + 2 * t * (1 - t) * p1 + t * t * p2
        tangent = 2 * (1 - t) * (p1 - p0) + 2 * t * (p2 - p1)
        tangent /= np.linalg.norm(tangent)
        up = np.cross(side, tangent)
        r = radius * (1.0 - t) ** 0.75 + 0.0008
        ring = []
        for j in range(segments):
            a = 2.0 * math.pi * j / segments
            ring.append(bm.verts.new(Vector(center + side * (0.75 * r * math.cos(a)) + up * (1.15 * r * math.sin(a)))))
        ring_vertices.append(ring)
    for i in range(rings - 1):
        for j in range(segments):
            k = (j + 1) % segments
            bm.faces.new((ring_vertices[i][j], ring_vertices[i][k], ring_vertices[i + 1][k], ring_vertices[i + 1][j]))
    bm.faces.new(list(reversed(ring_vertices[0])))
    bm.faces.new(ring_vertices[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return mesh_from_bmesh(name, bm)


def create_claws():
    claws = []
    for index, (toe, front, side, bone) in enumerate(TOES):
        offset = (toe[0] - side * (FRONT_PAW[0][0] if front else HIND_PAW[0][0])) / 0.115  # -1..1 across the paw
        forward = np.array([0.35 * offset * 0.115 / 0.12, -1.0, 0.0])
        base = toe + np.array([0.0, -0.035, 0.008])
        claw = create_claw(f"Claw.{index}", base, forward, 0.09 if front else 0.068, 0.018 if front else 0.015)
        claws.append((claw, bone, base))
    return claws


def closed_union_distance(p):
    """Solid body with the mouth closed (the lower jaw where the game puts it at rest)"""
    d = None
    for primitive in PRIMITIVES:
        distance = primitive.distance(open_jaw(p) if primitive.jaw else p)
        d = distance if d is None else smooth_min(d, distance, primitive.blend)
    return d


def report_clipping(name, closed_points):
    """Points of the tongue or the teeth (with the mouth closed) outside of the head would show through the skin"""
    outside = closed_union_distance(closed_points) > 0.002
    print(f"{name}: {int(outside.sum())} of {len(closed_points)} points outside of the head with the mouth closed")
    return int(outside.sum())


def bone_table(eyes):
    """Bones: name, head, tail, parent"""
    left_eye = eyes[0]
    left = [
        ("ear.L", (0.32, -0.99, 2.01), (0.43, -0.99, 2.28), "head"),
        ("eye.L", tuple(left_eye.center), tuple(left_eye.center + 0.12 * left_eye.gaze), "head"),
        ("eyelid.upper.L", tuple(left_eye.center), tuple(left_eye.center + 0.1 * left_eye.gaze + 0.02 * left_eye.up), "head"),
        ("eyelid.lower.L", tuple(left_eye.center), tuple(left_eye.center + 0.1 * left_eye.gaze - 0.02 * left_eye.up), "head"),
        ("upperarm.L", (0.37, -0.46, 1.02), (0.39, -0.49, 0.48), "chest"),
        ("forearm.L", (0.39, -0.49, 0.48), (0.4, -0.52, 0.15), "upperarm.L"),
        ("hand.L", (0.4, -0.52, 0.15), (0.4, -0.8, 0.07), "forearm.L"),
        ("thigh.L", (0.35, 0.52, 0.98), (0.37, 0.38, 0.46), "pelvis"),
        ("shin.L", (0.37, 0.38, 0.46), (0.38, 0.52, 0.15), "thigh.L"),
        ("foot.L", (0.38, 0.52, 0.15), (0.38, 0.14, 0.06), "shin.L"),
    ]
    right = [(n.replace(".L", ".R"), (-h[0], h[1], h[2]), (-t[0], t[1], t[2]), p.replace(".L", ".R")) for n, h, t, p in left]
    jaw_tail = open_jaw(HINGE + np.array([0.0, -0.42, -0.04]))[0]
    tongue = [open_jaw(np.array([0.0, y, 1.485]))[0] for y in (-1.17, -1.28, -1.38, -1.47)]
    return [
        ("root", (0.0, 0.0, 0.0), (0.0, -0.4, 0.0), None),
        ("pelvis", (0.0, 0.54, 1.04), (0.0, 0.1, 1.07), "root"),
        ("spine", (0.0, 0.1, 1.07), (0.0, -0.28, 1.11), "pelvis"),
        ("chest", (0.0, -0.28, 1.11), (0.0, -0.58, 1.27), "spine"),
        ("neck", (0.0, -0.58, 1.27), (0.0, -0.88, 1.5), "chest"),
        ("head", (0.0, -0.88, 1.5), (0.0, -1.46, 1.62), "neck"),
        ("jaw", tuple(HINGE), tuple(jaw_tail), "head"),
        ("tongue.1", tuple(tongue[0]), tuple(tongue[1]), "jaw"),
        ("tongue.2", tuple(tongue[1]), tuple(tongue[2]), "tongue.1"),
        ("tongue.3", tuple(tongue[2]), tuple(tongue[3]), "tongue.2"),
        ("tail", (0.0, 0.88, 1.08), (0.0, 1.16, 1.15), "pelvis"),
    ] + left + right


# Bones that only move separate meshes (the body gets no weights from them)
NON_BODY_BONES = {"root", "eye.L", "eye.R", "eyelid.upper.L", "eyelid.lower.L", "eyelid.upper.R", "eyelid.lower.R",
                  "tongue.1", "tongue.2", "tongue.3"}


def create_armature(eyes):
    data = bpy.data.armatures.new("BearCubRig")
    rig = link("BearCubRig", data)
    select_only(rig)
    bpy.ops.object.mode_set(mode="EDIT")
    for name, head, tail, parent in bone_table(eyes):
        bone = data.edit_bones.new(name)
        bone.head, bone.tail = Vector(head), Vector(tail)
        # The local Z axis of every bone points up (or forward for vertical bones), the X axis to the left of the bear
        direction = (bone.tail - bone.head).normalized()
        up = Vector((0.0, 0.0, 1.0)) if abs(direction.z) < 0.7 else Vector((0.0, -1.0, 0.0))
        bone.align_roll(up)
        if parent:
            bone.parent = data.edit_bones[parent]
        bone.use_connect = False
        bone.use_deform = name not in NON_BODY_BONES
    bpy.ops.object.mode_set(mode="OBJECT")
    data.display_type = "STICK"
    return rig


def make_material(name, roughness):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    bsdf = nodes.get("Principled BSDF")
    color = nodes.new("ShaderNodeVertexColor")
    color.layer_name = "Col"
    material.node_tree.links.new(color.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = roughness
    return material


def assign_weights(obj, weights):
    """weights: bone name -> array with one weight per vertex"""
    for name, values in weights.items():
        group = obj.vertex_groups.get(name) or obj.vertex_groups.new(name=name)
        for index, weight in enumerate(values):
            if weight > 1e-4:
                group.add([index], float(weight), "REPLACE")


def mesh_points(obj):
    coordinates = np.empty(len(obj.data.vertices) * 3)
    obj.data.vertices.foreach_get("co", coordinates)
    return coordinates.reshape(-1, 3)


def fix_unweighted_vertices(body, rig):
    """Bone heat can fail for single vertices, they get the bone whose segment is closest"""
    groups = {group.index: group.name for group in body.vertex_groups}
    bones = [(b.name, Vector(b.head_local), Vector(b.tail_local)) for b in rig.data.bones if b.use_deform]
    bone_names = {name for name, _, _ in bones}
    fixed = 0
    for vertex in body.data.vertices:
        total = sum(g.weight for g in vertex.groups if groups.get(g.group) in bone_names)
        if total > 1e-4:
            continue
        best, best_distance = None, 1e9
        for name, head, tail in bones:
            segment = tail - head
            t = max(0.0, min(1.0, (vertex.co - head).dot(segment) / segment.length_squared))
            distance = (vertex.co - (head + t * segment)).length
            if distance < best_distance:
                best, best_distance = name, distance
        group = body.vertex_groups.get(best) or body.vertex_groups.new(name=best)
        group.add([vertex.index], 1.0, "REPLACE")
        fixed += 1
    print(f"Vertices without weights after bone heat: {fixed}")


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def set_jaw_weights(body):
    """The lower jaw moves with the jaw bone: everything below the upper lip in front of the hinge (smooth at the corners)"""
    p = mesh_points(body)
    q = p - HINGE
    jaw = (smoothstep(0.004, -0.03, q[:, 2]) * smoothstep(0.03, -0.08, q[:, 1]) * smoothstep(MOUTH_HALF_WIDTH + 0.2, MOUTH_HALF_WIDTH + 0.08, np.abs(q[:, 0]))
           * smoothstep(-0.42, -0.25, q[:, 2]))
    group_names = {group.index: group.name for group in body.vertex_groups}
    jaw_group = body.vertex_groups.get("jaw") or body.vertex_groups.new(name="jaw")
    for vertex in body.data.vertices:
        w = float(jaw[vertex.index])
        others = [(g.group, g.weight) for g in vertex.groups if group_names.get(g.group) != "jaw"]
        total = sum(weight for _, weight in others)
        for group, weight in others:
            body.vertex_groups[group_names[group]].add([vertex.index], (1.0 - w) * weight / max(total, 1e-6), "REPLACE")
        if w > 1e-4:
            jaw_group.add([vertex.index], w, "REPLACE")
        else:
            jaw_group.remove([vertex.index])


KINDS = ["fur", "eye", "eyelid", "nose", "tongue", "teeth", "claw"]


def tag_kind(obj, kind):
    """Remembers what a part is, the tag survives joining the parts into one mesh"""
    attribute = obj.data.attributes.new(name="kind", type="INT", domain="POINT")
    attribute.data.foreach_set("value", [KINDS.index(kind)] * len(obj.data.vertices))


def paint_vertex_colors(obj, eyes, eye_radius, nose_center):
    """RGB: fur color (eyes: direction in eye space), A: fur length (0..1)"""
    mesh = obj.data
    count = len(mesh.vertices)
    p = mesh_points(obj)
    n = np.empty(count * 3)
    mesh.vertices.foreach_get("normal", n)
    n = n.reshape(-1, 3)

    # Soft membership of every vertex in the primitives (smooth color transitions between the parts)
    distances = np.stack([primitive.distance(p) for primitive in PRIMITIVES], axis=1)
    weights = np.exp(-(distances - distances.min(axis=1, keepdims=True)) / 0.035)
    weights /= weights.sum(axis=1, keepdims=True)
    part_colors = np.stack([COLORS[primitive.color] for primitive in PRIMITIVES])
    part_fur = np.array([primitive.fur for primitive in PRIMITIVES])
    color = weights @ part_colors
    fur = weights @ part_fur
    names = [primitive.name for primitive in PRIMITIVES]

    def membership(*parts):
        return weights[:, [names.index(part) for part in parts]].sum(axis=1)

    def blend(mask, target, amount=1.0):
        nonlocal color
        mask = np.clip(mask, 0.0, 1.0)[:, None] * amount
        color = color * (1.0 - mask) + target * mask

    body = membership("torso", "chest", "rump", "belly", "hump", "neck")
    # Darker back, lighter belly (countershading)
    blend(body * smoothstep(0.3, 0.9, n[:, 2]), COLORS["back"], 0.8)
    blend(body * smoothstep(-0.2, -0.8, n[:, 2]), COLORS["belly"], 0.8)
    # Light collar of young brown bears: a soft crescent on the chest that fades out towards the shoulders
    neck_start = np.array([0.0, -0.54, 1.22])
    neck_axis = np.array([0.0, -0.88, 1.5]) - neck_start
    along = (p - neck_start) @ (neck_axis / np.linalg.norm(neck_axis))
    band = smoothstep(-0.02, 0.08, along) * (1.0 - smoothstep(0.16, 0.26, along))
    front = smoothstep(0.1, 0.8, -n[:, 1] - 0.6 * n[:, 2])
    blend(band * front, COLORS["collar"], 0.6)
    # Lighter inner ears (the side facing forward)
    ears = membership("ear.L", "ear.R")
    inner = ears * smoothstep(0.2, 0.7, -n[:, 1])
    blend(inner, COLORS["inner_ear"])
    fur = fur * (1.0 - 0.6 * np.clip(inner, 0.0, 1.0))
    # Paw pads: bare skin under the paws
    paws = membership("hand.L", "hand.R", "foot.L", "foot.R", *[name for name in names if name.startswith("toe.")])
    pads = paws * smoothstep(-0.55, -0.85, n[:, 2]) * smoothstep(0.08, 0.02, p[:, 2])
    blend(pads, COLORS["pad"])
    fur = fur * (1.0 - np.clip(pads, 0.0, 1.0))
    # Short fur on the snout and around the eyes (it must not grow over them)
    muzzle = membership("muzzle", "jaw", "chin")
    fur = np.minimum(fur, 0.22 + 0.78 * (1.0 - np.clip(muzzle * 1.5, 0.0, 1.0)))
    for eye in eyes:
        distance = np.linalg.norm(p - eye.center, axis=1)
        fur = np.minimum(fur, 0.05 + 0.95 * smoothstep(1.3 * eye_radius, 2.8 * eye_radius, distance))
        blend(1.0 - smoothstep(1.2 * eye_radius, 2.2 * eye_radius, distance), COLORS["face"] * 0.7, 0.6)
    # Inside of the mouth: where the body was carved (the solid parts reach further), dark lips around it
    carved = union_distance(p) < -0.01
    lips = smoothstep(0.035, 0.008, carving_distance(p)) * (1.0 - carved)
    blend(lips, COLORS["lips"])
    fur = fur * (1.0 - 0.85 * lips)

    kind_index = np.empty(count, dtype=np.int32)
    mesh.attributes["kind"].data.foreach_get("value", kind_index)
    kind_of_vertex = np.array(KINDS)[kind_index]
    on_body = kind_of_vertex == "fur"
    color[on_body & carved] = COLORS["mouth"]
    fur[on_body & carved] = 0.0
    # No fur at all on the faces of the inside of the mouth (also at their border to the lips)
    mouth_slots = {i for i, slot in enumerate(obj.material_slots) if slot.material and slot.material.name == "BearMouth"}
    for polygon in mesh.polygons:
        if polygon.material_index in mouth_slots:
            fur[list(polygon.vertices)] = 0.0

    for kind in ("nose", "tongue", "teeth"):
        color[kind_of_vertex == kind] = COLORS[kind]
        fur[kind_of_vertex == kind] = 0.0
    # Claws: dark at the root, light towards the tip
    claw = kind_of_vertex == "claw"
    toe_centers = np.array([toe for toe, _, _, _ in TOES])
    nearest_toe = np.min(np.linalg.norm(p[claw][:, None, :] - toe_centers[None, :, :], axis=2), axis=1)
    tip = smoothstep(0.035, 0.085, nearest_toe)[:, None]
    color[claw] = COLORS["claw_root"] * (1.0 - tip) + COLORS["claw"] * tip
    fur[claw] = 0.0
    nose = kind_of_vertex == "nose"
    color[nose] = COLORS["nose"] * (1.0 - nostril_amount(p[nose], nose_center))[:, None] + COLORS["nostril"] * nostril_amount(p[nose], nose_center)[:, None]
    # Eyes: direction from the center of the eye in eye space (iris and pupil are drawn by the game)
    eye_vertices = kind_of_vertex == "eye"
    for eye in eyes:
        mine = eye_vertices & (np.sign(p[:, 0]) == np.sign(eye.center[0]))
        direction = p[mine] - eye.center
        direction /= np.linalg.norm(direction, axis=1, keepdims=True)
        color[mine] = eye.local(direction) * 0.5 + 0.5
        fur[mine] = 0.0
    # Eyelids: skin with short fur outside, a dark rim at the edge, pink inside
    lid_vertices = kind_of_vertex == "eyelid"
    for eye in eyes:
        mine = lid_vertices & (np.sign(p[:, 0]) == np.sign(eye.center[0]))
        radius = np.linalg.norm(p[mine] - eye.center, axis=1)
        outside = radius > 1.12 * eye_radius
        lid_color = np.where(outside[:, None], COLORS["face"] * 0.8, COLORS["lid_inside"])
        lid_fur = np.where(outside, 0.12, 0.0)
        # the rim: close to the edge of the lid (the points furthest towards the gaze from the up or down axis)
        direction = (p[mine] - eye.center) / radius[:, None]
        towards_gaze = np.abs(direction @ eye.up)
        rim = smoothstep(0.62, 0.5, towards_gaze) * (direction @ eye.gaze > 0.2)
        lid_color = lid_color * (1.0 - rim[:, None]) + COLORS["lid_margin"] * rim[:, None]
        color[mine] = lid_color
        fur[mine] = lid_fur * (1.0 - rim)

    rgba = np.concatenate([color, np.clip(fur, 0.0, 1.0)[:, None]], axis=1)
    attribute = mesh.color_attributes.new(name="Col", type="FLOAT_COLOR", domain="POINT")
    attribute.data.foreach_set("color", rgba.ravel())
    mesh.color_attributes.active_color = attribute
    mesh.color_attributes.render_color_index = mesh.color_attributes.find("Col")


def build():
    clear_scene()
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    body = create_body_mesh()

    # Eyes under the brows, looking forward and a bit outward; the eyelids are shells around them
    eye_radius = 0.06
    center = surface_point_from_inside(CRANIUM_CENTER, (0.42, -0.84, 0.2), sink=0.42 * eye_radius)
    outward = math.radians(12.0)
    eyes = [EyeFrame(center, (math.sin(outward), -math.cos(outward), 0.0)),
            EyeFrame((-center[0], center[1], center[2]), (-math.sin(outward), -math.cos(outward), 0.0))]
    eye_objects = [create_eye(f"Eye.{side}", eye, eye_radius) for side, eye in zip("LR", eyes)]
    lid_objects = []
    for side, eye in zip("LR", eyes):
        lid_objects.append((create_eyelid(f"Eyelid.upper.{side}", eye, eye_radius, True), f"eyelid.upper.{side}"))
        lid_objects.append((create_eyelid(f"Eyelid.lower.{side}", eye, eye_radius, False), f"eyelid.lower.{side}"))
    nose_center = surface_point_from_inside(np.array(MUZZLE_TIP), (0.0, -1.0, 0.45), sink=0.03)
    nose = create_nose(nose_center)
    tongue = create_tongue()
    teeth = create_teeth()
    claws = create_claws()
    report_clipping("Tongue", closed_jaw(mesh_points(tongue)))
    report_clipping("Upper teeth", np.concatenate([mesh_points(obj) for obj, upper in teeth if upper]))
    report_clipping("Lower teeth", closed_jaw(np.concatenate([mesh_points(obj) for obj, upper in teeth if not upper])))

    rig = create_armature(eyes)

    print("Computing skin weights (bone heat)...")
    select_only(body, rig, active=rig)
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    fix_unweighted_vertices(body, rig)
    set_jaw_weights(body)

    for obj, bone in zip(eye_objects, ("eye.L", "eye.R")):
        assign_weights(obj, {bone: np.ones(len(obj.data.vertices))})
    for obj, bone in lid_objects:
        assign_weights(obj, {bone: np.ones(len(obj.data.vertices))})
    assign_weights(nose, {"head": np.ones(len(nose.data.vertices))})
    for obj, bone, _ in claws:
        assign_weights(obj, {bone: np.ones(len(obj.data.vertices))})
    for obj, upper in teeth:
        assign_weights(obj, {("head" if upper else "jaw"): np.ones(len(obj.data.vertices))})
    # Tongue: smooth along its length from the root to the tip
    along = (closed_jaw(mesh_points(tongue))[:, 1] - TONGUE_CENTER[1]) / TONGUE_HALF[1]  # +1 root, -1 tip
    root, middle, tip = smoothstep(0.0, 0.8, along), 1.0 - np.abs(np.clip(along, -1.0, 1.0)) ** 1.5, smoothstep(0.0, -0.8, along)
    total = root + middle + tip
    assign_weights(tongue, {"tongue.1": root / total, "tongue.2": middle / total, "tongue.3": tip / total})

    materials = {"fur": make_material("BearFur", 0.8), "eye": make_material("BearEye", 0.05), "nose": make_material("BearNose", 0.25),
                 "mouth": make_material("BearMouth", 0.3), "tongue": make_material("BearTongue", 0.3), "teeth": make_material("BearTeeth", 0.4),
                 "claw": make_material("BearClaws", 0.45)}
    parts = [(body, "fur"), (nose, "nose"), (tongue, "tongue")] + [(obj, "eye") for obj in eye_objects] + [(obj, "eyelid") for obj, _ in lid_objects] + [(obj, "teeth") for obj, _ in teeth] + [(obj, "claw") for obj, _, _ in claws]
    for obj, kind in parts:
        obj.data.materials.clear()
        obj.data.materials.append(materials["fur" if kind == "eyelid" else kind])  # eyelids are skin with fur
        tag_kind(obj, kind)
    # Inside of the mouth: its own (wet) material
    body.data.materials.append(materials["mouth"])
    centers = np.array([polygon.center for polygon in body.data.polygons])
    carved = union_distance(centers) < -0.01
    for polygon, inside in zip(body.data.polygons, carved):
        if inside:
            polygon.material_index = 1

    # One mesh for the whole bear (one primitive per material in the glTF file)
    lid_count = sum(len(obj.data.vertices) for obj, _ in lid_objects)
    select_only(*[obj for obj, _ in parts], active=body)
    bpy.ops.object.join()
    for bone in NON_BODY_BONES - {"root"}:
        rig.data.bones[bone].use_deform = True
    paint_vertex_colors(body, eyes, eye_radius, nose_center)
    body.data.attributes.remove(body.data.attributes["kind"])

    select_only(rig)
    bpy.ops.export_scene.gltf(filepath=GLB_FILE, export_format="GLB", export_yup=True, export_skins=True,
                              export_animations=False, export_vertex_color="ACTIVE", export_all_vertex_colors=False,
                              export_normals=True, export_texcoords=False, export_tangents=False,
                              export_materials="EXPORT", export_influence_nb=4, export_def_bones=False, use_selection=False)
    bpy.context.preferences.filepaths.save_version = 0  # no BearCub.blend1 backup next to it
    bpy.ops.wm.save_as_mainfile(filepath=BLEND_FILE, compress=True)
    print("Wrote", GLB_FILE, "and", BLEND_FILE, f"({len(body.data.vertices)} vertices, {lid_count} in the eyelids)")
    return body, rig


def render_previews(folder, body, rig):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "VERTEX"
    scene.display.shading.show_cavity = True
    scene.render.resolution_x, scene.render.resolution_y = 900, 700
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("World")
    scene.world = world
    camera = link("Camera", bpy.data.cameras.new("Camera"))
    scene.camera = camera
    camera.data.lens = 50

    def shoot(name, location, target=(0.0, -0.2, 1.0)):
        camera.location = Vector(location)
        direction = Vector(target) - camera.location
        camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(folder, name + ".png")
        bpy.ops.render.render(write_still=True)

    shoot("front", (2.2, -6.5, 2.2))
    shoot("side", (7.0, -0.2, 1.4))
    shoot("face", (0.9, -3.6, 1.9), target=(0.0, -1.3, 1.65))
    shoot("profile", (3.0, -1.4, 1.7), target=(0.0, -1.35, 1.65))
    pose = rig.pose.bones
    for bone in pose:
        bone.rotation_mode = "XYZ"
    # Closed mouth (as in the game at rest)
    pose["jaw"].rotation_euler = Matrix.Rotation(math.radians(JAW_OPEN_DEGREES), 3, "X").to_euler()  # the local X axis of the jaw points to the right
    bpy.context.view_layer.update()
    shoot("face_closed", (0.9, -3.6, 1.9), target=(0.0, -1.3, 1.65))
    shoot("profile_closed", (3.0, -1.4, 1.7), target=(0.0, -1.35, 1.65))
    shoot("paws", (1.4, -2.2, 0.6), target=(0.3, -0.6, 0.08))
    # Open mouth (yawning) with the teeth
    pose["jaw"].rotation_euler = Matrix.Rotation(math.radians(JAW_OPEN_DEGREES - 38.0), 3, "X").to_euler()
    bpy.context.view_layer.update()
    shoot("face_open", (0.5, -3.2, 1.75), target=(0.0, -1.4, 1.55))


if __name__ == "__main__":
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    body, rig = build()
    if "--preview" in arguments:
        render_previews(arguments[arguments.index("--preview") + 1], body, rig)
