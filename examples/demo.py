"""assimp-py interactive demo: static + GPU-skinned animated rendering.

Renders the textured cyborg (tests/models/cyborg) next to the animated
fox (tests/models/fox) with real skeletal animation evaluated from
assimp_py keyframe data.

Requirements:
    pip install assimp-py[demo]   # moderngl, pygame, Pillow, numpy

Controls:
    1 / 2 / 3    play fox animation (Survey / Walk / Run)
    Space        pause / resume
    F            toggle wireframe
    B            toggle skeleton overlay
    Mouse drag   orbit camera
    Mouse wheel  zoom
    Esc          quit

Headless verification (no window):
    python examples/demo.py --screenshot demo.png --frames 120
"""
import argparse
import math
import os
import sys
import time
from pathlib import Path

import numpy as np

import assimp_py
import moderngl
import pygame

MODELS_DIR = Path(__file__).resolve().parent.parent / "tests/models"
MAX_BONES = 64


# ---------------------------------------------------------------------------
# Small numpy mat4/quat toolkit (column-vector convention: v' = M @ v)
# ---------------------------------------------------------------------------

def mat4_identity():
    return np.eye(4, dtype=np.float32)


def mat4_from_tuple(t):
    """assimp_py 4x4 tuple-of-tuples (row-major) -> numpy mat4."""
    return np.asarray(t, dtype=np.float32)


def quat_from_array(q):
    """(x, y, z, w) float array -> rotation matrix."""
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w)
    x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),     0.0],
        [2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),     0.0],
        [2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y), 0.0],
        [0.0,                     0.0,                     0.0,                     1.0],
    ], dtype=np.float32)


def compose_trs(pos, rot4, scale):
    """Translate @ Rotate @ Scale."""
    m = quat_from_array(rot4)
    out = mat4_identity()
    out[:3, :3] = m[:3, :3] * scale.reshape(3, 1)
    out[:3, 3] = pos
    return out


def mat4_translate(t):
    m = mat4_identity()
    m[:3, 3] = t
    return m


def mat4_scale_rows(s):
    m = mat4_identity()
    m[0, 0], m[1, 1], m[2, 2] = s
    return m


def mat4_rotate_y(deg):
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    m = mat4_identity()
    m[0, 0], m[0, 2] = c, s
    m[2, 0], m[2, 2] = -s, c
    return m


def mat4_perspective(fovy_deg, aspect, near, far):
    f = 1.0 / math.tan(math.radians(fovy_deg) / 2.0)
    m = np.zeros((4, 4), dtype=np.float32)
    m[0, 0] = f / aspect
    m[1, 1] = f
    m[2, 2] = (far + near) / (near - far)
    m[2, 3] = 2 * far * near / (near - far)
    m[3, 2] = -1.0
    return m


def mat4_look_at(eye, target, up=(0.0, 1.0, 0.0)):
    eye = np.asarray(eye, dtype=np.float32)
    target = np.asarray(target, dtype=np.float32)
    up = np.asarray(up, dtype=np.float32)
    f = target - eye
    f /= np.linalg.norm(f)
    s = np.cross(f, up)
    s /= np.linalg.norm(s)
    u = np.cross(s, f)
    m = mat4_identity()
    m[0, :3], m[1, :3], m[2, :3] = s, u, -f
    m[0, 3] = -s @ eye
    m[1, 3] = -u @ eye
    m[2, 3] = f @ eye
    return m


def uploadable(m):
    """mat4 -> bytes in GL column-major order."""
    return np.ascontiguousarray(m.T, dtype=np.float32).tobytes()


# ---------------------------------------------------------------------------
# Animation evaluation from assimp_py data
# ---------------------------------------------------------------------------

def node_map(scene):
    """Flat dict name -> Node over the whole hierarchy."""
    out = {}

    def walk(node):
        out[node.name] = node
        for child in node.children:
            walk(child)

    walk(scene.root_node)
    return out


def sample_track(times, values, t, value_width, slerp=False):
    """Interpolate a key track at tick t. times/values are numpy arrays."""
    n = times.shape[0]
    if n == 1:
        return values.reshape(-1, value_width)[0]
    i1 = int(np.searchsorted(times, t, side="right"))
    i0 = i1 - 1
    if i1 >= n:
        return values.reshape(-1, value_width)[-1]
    if i1 == 0:
        return values.reshape(-1, value_width)[0]

    t0, t1 = times[i0], times[i1]
    u = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)

    a = values.reshape(-1, value_width)[i0].astype(np.float64)
    b = values.reshape(-1, value_width)[i1].astype(np.float64)

    if slerp:  # quaternion slerp
        dot = float(np.dot(a, b))
        if dot < 0.0:
            b, dot = -b, -dot
        if dot > 0.9995:  # nlerp fallback for tiny angles
            out = a + u * (b - a)
            return (out / np.linalg.norm(out)).astype(np.float32)
        theta = math.acos(min(dot, 1.0))
        s = math.sin(theta)
        out = (math.sin((1 - u) * theta) / s) * a + (math.sin(u * theta) / s) * b
        return out.astype(np.float32)

    return (a + u * (b - a)).astype(np.float32)


class Animator:
    """Evaluates node global transforms + skinning palettes for one scene."""

    def __init__(self, scene):
        self.scene = scene
        self.nodes = node_map(scene)

    def globals_at(self, animation, tick):
        """name -> global matrix for every node at the given tick."""
        channels = {ch.node_name: ch for ch in animation.channels} if animation else {}

        globals_ = {}

        def local_matrix(name):
            node = self.nodes[name]
            ch = channels.get(name)
            if ch is None:
                return mat4_from_tuple(node.transformation)
            pos = sample_track(np.frombuffer(ch.position_key_times, np.float64),
                               np.frombuffer(ch.position_key_values, np.float32), tick, 3)
            rot = sample_track(np.frombuffer(ch.rotation_key_times, np.float64),
                               np.frombuffer(ch.rotation_key_values, np.float32), tick, 4, slerp=True)
            scl = sample_track(np.frombuffer(ch.scaling_key_times, np.float64),
                               np.frombuffer(ch.scaling_key_values, np.float32), tick, 3)
            return compose_trs(pos, rot, scl)

        def walk(node, parent_matrix):
            m = parent_matrix @ local_matrix(node.name)
            globals_[node.name] = m
            for child in node.children:
                walk(child, m)

        walk(self.scene.root_node, mat4_identity())
        return globals_

    def palette(self, mesh, globals_, bones_index):
        """Skinning palette (MAX_BONES, 4, 4): global(node) @ offset_matrix."""
        palette = np.zeros((MAX_BONES, 4, 4), dtype=np.float32)
        for bi, bone in enumerate(mesh.bones):
            name = bone.node_name if bone.node_name else bone.name
            g = globals_.get(name)
            if g is None:
                continue
            palette[bi] = g @ mat4_from_tuple(bone.offset_matrix)
        return palette


def build_skin_attributes(mesh):
    """Per-vertex (4 bone ids, 4 weights) arrays from per-bone weight lists."""
    n = mesh.num_vertices
    ids = np.zeros((n, 4), dtype=np.uint32)
    weights = np.zeros((n, 4), dtype=np.float32)
    counts = np.zeros(n, dtype=np.int32)

    for bi, bone in enumerate(mesh.bones):
        vids = np.frombuffer(bone.weight_vertex_ids, dtype=np.uint32)
        wts = np.frombuffer(bone.weights, dtype=np.float32)
        slot = counts[vids]
        mask = slot < 4  # Process_LimitBoneWeights keeps <= 4 influences
        target_v = vids[mask]
        target_s = slot[mask]
        ids[target_v, target_s] = bi
        weights[target_v, target_s] = wts[mask]
        np.add.at(counts, target_v, 1)

    return ids, weights


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

STATIC_VS = """
#version 330
in vec3 in_position;
in vec3 in_normal;
in vec2 in_uv;
uniform mat4 mvp;
uniform mat4 model;
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_world;
void main() {
    v_normal = mat3(model) * in_normal;
    v_uv = in_uv;
    v_world = (model * vec4(in_position, 1.0)).xyz;
    gl_Position = mvp * vec4(in_position, 1.0);
}
"""

SKIN_VS = """
#version 330
in vec3 in_position;
in vec3 in_normal;
in vec2 in_uv;
in uvec4 in_bone_ids;
in vec4 in_bone_weights;
uniform mat4 mvp;
uniform mat4 model;
uniform mat4 bones[64];
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_world;
void main() {
    mat4 skin =
        in_bone_weights[0] * bones[in_bone_ids[0]] +
        in_bone_weights[1] * bones[in_bone_ids[1]] +
        in_bone_weights[2] * bones[in_bone_ids[2]] +
        in_bone_weights[3] * bones[in_bone_ids[3]];
    mat4 world = model * skin;
    v_normal = mat3(world) * in_normal;
    v_uv = in_uv;
    v_world = (world * vec4(in_position, 1.0)).xyz;
    gl_Position = mvp * world * vec4(in_position, 1.0);
}
"""

LIT_FS = """
#version 330
in vec3 v_normal;
in vec2 v_uv;
in vec3 v_world;
uniform sampler2D diffuse_tex;
uniform vec3 tint;
uniform float use_texture;
uniform vec3 light_dir;
uniform vec3 eye_pos;
out vec4 frag_color;
void main() {
    vec3 n = normalize(v_normal);
    float diff = max(dot(n, -normalize(light_dir)), 0.0);
    vec3 base = mix(tint, texture(diffuse_tex, v_uv).rgb, use_texture);
    vec3 view_dir = normalize(eye_pos - v_world);
    vec3 half_v = normalize(view_dir - normalize(light_dir));
    float spec = pow(max(dot(n, half_v), 0.0), 32.0) * 0.25;
    vec3 color = base * (0.25 + 0.75 * diff) + vec3(spec);
    frag_color = vec4(color, 1.0);
}
"""

COLOR_VS = """
#version 330
in vec3 in_position;
uniform mat4 mvp;
void main() {
    gl_Position = mvp * vec4(in_position, 1.0);
}
"""

COLOR_SKIN_VS = """
#version 330
in vec3 in_position;
in uvec4 in_bone_ids;
in vec4 in_bone_weights;
uniform mat4 mvp;
uniform mat4 model;
uniform mat4 bones[64];
void main() {
    mat4 skin =
        in_bone_weights[0] * bones[in_bone_ids[0]] +
        in_bone_weights[1] * bones[in_bone_ids[1]] +
        in_bone_weights[2] * bones[in_bone_ids[2]] +
        in_bone_weights[3] * bones[in_bone_ids[3]];
    gl_Position = mvp * model * skin * vec4(in_position, 1.0);
}
"""

COLOR_FS = """
#version 330
uniform vec3 color;
out vec4 frag_color;
void main() {
    frag_color = vec4(color, 1.0);
}
"""

HUD_VS = """
#version 330
in vec2 in_pos;
in vec2 in_uv;
out vec2 v_uv;
void main() {
    v_uv = in_uv;
    gl_Position = vec4(in_pos, 0.0, 1.0);
}
"""

HUD_FS = """
#version 330
in vec2 v_uv;
uniform sampler2D hud_tex;
out vec4 frag_color;
void main() {
    frag_color = texture(hud_tex, v_uv);
}
"""


class Model:
    """A renderable mesh with placement + optional skinning."""

    def __init__(self, ctx, mesh, texture=None, tint=(0.85, 0.72, 0.52)):
        self.ctx = ctx
        self.mesh = mesh
        self.tint = tint
        self.texture = texture
        self.model_matrix = mat4_identity()

        vertices = np.frombuffer(mesh.vertices, dtype=np.float32).reshape(-1, 3)
        normals = (np.frombuffer(mesh.normals, dtype=np.float32).reshape(-1, 3)
                   if mesh.normals is not None else np.zeros_like(vertices))
        uvs = (np.frombuffer(mesh.texcoords[0], dtype=np.float32).reshape(-1, 2)
               if (isinstance(mesh.texcoords, list) and len(mesh.texcoords) > 0
                   and mesh.num_uv_components[0] >= 2) else np.zeros((len(vertices), 2), np.float32))
        indices = np.frombuffer(mesh.indices, dtype=np.uint32)

        self.vertex_count = len(indices)

        self.vertex_count = len(indices)

        # interleaved record; bone ids must stay uint32 (an f32-cast would be
        # reinterpreted as garbage uvec4 indices by the shader)
        if mesh.num_bones > 0:
            self.skinned = True
            bone_ids, bone_weights = build_skin_attributes(mesh)
            record = np.zeros(len(vertices), dtype=np.dtype([
                ("pos", "f4", 3), ("nrm", "f4", 3), ("uv", "f4", 2),
                ("ids", "u4", 4), ("wts", "f4", 4),
            ]))
            record["pos"] = vertices
            record["nrm"] = normals
            record["uv"] = uvs
            record["ids"] = bone_ids
            record["wts"] = bone_weights
            self.vbo_format = "3f 3f 2f 4u4 4f"
            self.attr_names = ["in_position", "in_normal", "in_uv", "in_bone_ids", "in_bone_weights"]
        else:
            self.skinned = False
            record = np.zeros(len(vertices), dtype=np.dtype([
                ("pos", "f4", 3), ("nrm", "f4", 3), ("uv", "f4", 2),
            ]))
            record["pos"] = vertices
            record["nrm"] = normals
            record["uv"] = uvs
            self.vbo_format = "3f 3f 2f"
            self.attr_names = ["in_position", "in_normal", "in_uv"]

        self.vbo = ctx.buffer(record.tobytes())
        self.pos_vbo = ctx.buffer(vertices.tobytes())  # positions only, for line rendering
        self.ibo = ctx.buffer(indices.astype(np.uint32).tobytes())

        # skinned wireframe buffer: positions + skin attributes only
        if self.skinned:
            wire_record = np.zeros(len(vertices), dtype=np.dtype([
                ("pos", "f4", 3), ("ids", "u4", 4), ("wts", "f4", 4),
            ]))
            wire_record["pos"] = vertices
            wire_record["ids"] = bone_ids
            wire_record["wts"] = bone_weights
            self.wire_vbo = ctx.buffer(wire_record.tobytes())

        # wireframe edge index buffer
        tris = indices.reshape(-1, 3)
        edges = np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]])
        edges = np.unique(edges, axis=0)
        self.wire_ibo = ctx.buffer(edges.astype(np.uint32).tobytes())
        self.wire_count = len(edges)

        if texture is None:
            self.texture = ctx.texture((1, 1), 3, b"\x80\x80\x80")
            self.use_texture = 0.0
        else:
            self.texture = texture
            self.use_texture = 1.0

    def vao(self, program):
        return self.ctx.vertex_array(
            program,
            [(self.vbo, self.vbo_format, *self.attr_names)],
            index_buffer=self.ibo,
            index_element_size=4,
        )

    def wire_vao(self, program):
        """Line rendering; skinned meshes use the skinned line shader."""
        if self.skinned:
            vbo, fmt = self.wire_vbo, "3f 4u4 4f"
            attrs = ["in_position", "in_bone_ids", "in_bone_weights"]
        else:
            vbo, fmt = self.pos_vbo, "3f"
            attrs = ["in_position"]
        return self.ctx.vertex_array(
            program,
            [(vbo, fmt, *attrs)],
            index_buffer=self.wire_ibo,
            index_element_size=4,
        )

    def bounds(self):
        vertices = np.frombuffer(self.mesh.vertices, dtype=np.float32).reshape(-1, 3)
        return vertices.min(axis=0), vertices.max(axis=0)


def load_texture(ctx, path, flip=True):
    from PIL import Image
    image = Image.open(path).convert("RGB")
    if flip:
        image = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    return ctx.texture(image.size, 3, image.tobytes()), image.size


def fit_placement(model, origin_x, target_height, yaw=0.0):
    """Uniform scale + ground placement at origin_x."""
    lo, hi = model.bounds()
    size = hi - lo
    scale = target_height / max(size[1], 1e-6)
    center = (lo + hi) / 2.0
    m = mat4_translate([origin_x, -lo[1] * scale, -center[2] * scale])
    m = m @ mat4_scale_rows([scale, scale, scale])
    m = m @ mat4_rotate_y(yaw)
    model.model_matrix = m


# ---------------------------------------------------------------------------
# Demo application
# ---------------------------------------------------------------------------

class Demo:
    def __init__(self, args):
        self.args = args
        pygame.init()
        pygame.display.set_caption("assimp-py demo - 1/2/3 animation, Space pause, F wireframe, B skeleton")
        if args.screenshot:
            pygame.display.gl_set_attribute(pygame.GL_CONTEXT_PROFILE_MASK, 1)
        self.size = (args.width, args.height)
        flags = pygame.DOUBLEBUF | pygame.OPENGL
        self.screen = pygame.display.set_mode(self.size, flags)
        self.ctx = moderngl.create_context()
        self.ctx.enable(moderngl.DEPTH_TEST)
        self.clock = pygame.time.Clock()
        self.font = pygame.font.Font(None, 26)

        # camera
        self.cam_target = np.array([0.0, 1.0, 0.0], dtype=np.float32)
        self.cam_yaw = 25.0
        self.cam_pitch = 18.0
        self.cam_dist = 5.0
        self.dragging = False

        # toggles
        self.paused = False
        self.wireframe = False
        self.show_skeleton = False

        # --- load models ---
        cyborg_scene = assimp_py.import_file(
            str(MODELS_DIR / "cyborg/cyborg.obj"),
            assimp_py.Process_Triangulate | assimp_py.Process_GenSmoothNormals
            | assimp_py.Process_CalcTangentSpace | assimp_py.Process_FlipUVs,
        )
        fox_scene = assimp_py.import_file(
            str(MODELS_DIR / "fox/Fox.glb"),
            assimp_py.Process_Triangulate | assimp_py.Process_GenSmoothNormals
            | assimp_py.Process_LimitBoneWeights | assimp_py.Process_PopulateArmatureData,
        )
        self.animator = Animator(fox_scene)

        tex, _ = load_texture(self.ctx, MODELS_DIR / "cyborg/cyborg_diffuse.png")
        self.cyborg = Model(self.ctx, cyborg_scene.meshes[0], texture=tex)
        self.fox = Model(self.ctx, fox_scene.meshes[0], tint=(0.88, 0.55, 0.25))

        fit_placement(self.cyborg, origin_x=-0.75, target_height=1.7, yaw=-20.0)
        fit_placement(self.fox, origin_x=0.85, target_height=1.0, yaw=140.0)

        # --- programs ---
        self.prog_static = self.ctx.program(vertex_shader=STATIC_VS, fragment_shader=LIT_FS)
        self.prog_skin = self.ctx.program(vertex_shader=SKIN_VS, fragment_shader=LIT_FS)
        self.prog_color = self.ctx.program(vertex_shader=COLOR_VS, fragment_shader=COLOR_FS)
        self.prog_color_skin = self.ctx.program(vertex_shader=COLOR_SKIN_VS, fragment_shader=COLOR_FS)
        self.prog_hud = self.ctx.program(vertex_shader=HUD_VS, fragment_shader=HUD_FS)

        self.vao_cyborg = self.cyborg.vao(self.prog_static)
        self.wire_cyborg = self.cyborg.wire_vao(self.prog_color)
        self.vao_fox = self.fox.vao(self.prog_skin)
        self.wire_fox = self.fox.wire_vao(self.prog_color_skin)

        self.skeleton_vbo = self.ctx.buffer(reserve=MAX_BONES * 2 * 3 * 4, dynamic=True)
        self.vao_skeleton = self.ctx.vertex_array(
            self.prog_color, [(self.skeleton_vbo, "3f", "in_position")])

        self.hud_vbo = self.ctx.buffer(reserve=1024 * 4, dynamic=True)
        self.hud_vao = self.ctx.vertex_array(self.prog_hud, [(self.hud_vbo, "2f 2f", "in_pos", "in_uv")])

        # --- animation state ---
        self.animations = {a.name: a for a in fox_scene.animations}
        self.anim_order = [a.name for a in fox_scene.animations]
        self.current = self.anim_order[0]
        self.sim_time = 0.0
        self.bones_uniform_loc = self.prog_skin["bones"]

    # --- camera ---

    def camera_matrices(self):
        eye = np.array([
            self.cam_target[0] + self.cam_dist * math.cos(math.radians(self.cam_pitch)) * math.sin(math.radians(self.cam_yaw)),
            self.cam_target[1] + self.cam_dist * math.sin(math.radians(self.cam_pitch)),
            self.cam_target[2] + self.cam_dist * math.cos(math.radians(self.cam_pitch)) * math.cos(math.radians(self.cam_yaw)),
        ], dtype=np.float32)
        view = mat4_look_at(eye, self.cam_target)
        proj = mat4_perspective(45.0, self.size[0] / self.size[1], 0.05, 100.0)
        return proj @ view, eye

    # --- frame ---

    def draw_model(self, vao, wire_vao, model, vp, palette=None):
        mvp = vp @ model.model_matrix
        if self.wireframe:
            wire_prog = wire_vao.program
            wire_prog["mvp"].write(uploadable(vp if model.skinned else mvp))
            if model.skinned:
                wire_prog["model"].write(uploadable(model.model_matrix))
                self.prog_color_skin["bones"].write(
                    np.ascontiguousarray(palette.transpose(0, 2, 1), dtype=np.float32).tobytes())
            wire_prog["color"].value = (1.0, 1.0, 1.0)
            wire_vao.render(moderngl.LINES)
            return

        prog = vao.program
        if palette is not None:
            # skinned: world = model * skin already contains the placement,
            # so the view-projection must NOT include model a second time
            prog["mvp"].write(uploadable(vp))
        else:
            prog["mvp"].write(uploadable(mvp))
        prog["model"].write(uploadable(model.model_matrix))
        prog["tint"].value = tuple(model.tint)
        prog["use_texture"].value = model.use_texture
        prog["light_dir"].value = (-0.4, -0.8, -0.45)
        prog["eye_pos"].value = self.eye.tolist()
        model.texture.use(0)
        prog["diffuse_tex"].value = 0
        if palette is not None:
            # GL expects column-major mat4s; transpose each bone matrix
            self.bones_uniform_loc.write(
                np.ascontiguousarray(palette.transpose(0, 2, 1), dtype=np.float32).tobytes())
        vao.render(moderngl.TRIANGLES)

    def update_skeleton(self, fox_globals):
        segments = []
        for bone in self.fox.mesh.bones:
            name = bone.node_name if bone.node_name else bone.name
            g = fox_globals.get(name)
            if g is None:
                continue
            parent_name = self.animator.nodes[name].parent_name
            gp = fox_globals.get(parent_name) if parent_name else None
            p0 = (g @ np.array([0, 0, 0, 1], np.float32))[:3]
            if gp is not None:
                segments.append(p0)
                segments.append((gp @ np.array([0, 0, 0, 1], np.float32))[:3])
            else:
                segments.append(p0)
                segments.append(p0 + np.array([0, 0.02, 0], np.float32))
        data = np.asarray(segments, dtype=np.float32).tobytes()
        self.skeleton_vbo.orphan()
        self.skeleton_vbo.write(data)
        return len(segments)  # vertex count (2 per segment)

    def draw_skeleton(self, fox_globals, vp):
        count = self.update_skeleton(fox_globals)
        if count == 0:
            return
        mvp = vp @ self.fox.model_matrix
        self.prog_color["mvp"].write(uploadable(mvp))
        self.prog_color["color"].value = (0.1, 1.0, 0.4)
        # overlay: draw through the mesh so interior bones stay visible
        self.ctx.disable(moderngl.DEPTH_TEST)
        self.vao_skeleton.render(moderngl.LINES, vertices=count)
        self.ctx.enable(moderngl.DEPTH_TEST)

    def draw_hud(self, lines):
        surface = self.font.render("   |   ".join(lines), True, (255, 255, 255), (20, 20, 25))
        # row 0 = top of the text; the quad maps v=0 to its top edge
        data = pygame.image.tobytes(surface, "RGBA", False)
        tex = self.ctx.texture(surface.get_size(), 4, data)

        # quad sized to the text surface, top-left corner of the framebuffer
        fw, fh = float(self.size[0]), float(self.size[1])
        w, h = surface.get_size()
        x1 = -1.0
        x2 = -1.0 + 2.0 * w / fw
        y1 = 1.0 - 2.0 * h / fh
        y2 = 1.0
        quad = np.array([
            [x1, y1, 0.0, 1.0],
            [x2, y1, 1.0, 1.0],
            [x1, y2, 0.0, 0.0],
            [x1, y2, 0.0, 0.0],
            [x2, y1, 1.0, 1.0],
            [x2, y2, 1.0, 0.0],
        ], dtype=np.float32).tobytes()

        self.ctx.disable(moderngl.DEPTH_TEST)
        self.hud_vbo.orphan()
        self.hud_vbo.write(quad)
        tex.use(0)
        self.prog_hud["hud_tex"].value = 0
        self.hud_vao.render(moderngl.TRIANGLES)
        self.ctx.enable(moderngl.DEPTH_TEST)
        tex.release()

    def frame(self, dt):
        self.ctx.clear(0.10, 0.11, 0.13)
        vp, self.eye = self.camera_matrices()

        # evaluate fox animation
        anim = self.animations[self.current]
        if not self.paused:
            self.sim_time += dt
        duration = max(anim.duration, 1e-6)
        tps = anim.ticks_per_second or duration
        tick = (self.sim_time * tps) % duration
        fox_globals = self.animator.globals_at(anim, tick)
        palette = self.animator.palette(self.fox.mesh, fox_globals, None)

        self.draw_model(self.vao_cyborg, self.wire_cyborg, self.cyborg, vp)
        self.draw_model(self.vao_fox, self.wire_fox, self.fox, vp, palette=palette)
        if self.show_skeleton:
            self.draw_skeleton(fox_globals, vp)

        fps = self.clock.get_fps()
        self.draw_hud([
            f"anim: {self.current} [{self.anim_order.index(self.current) + 1}/{len(self.anim_order)}]",
            "paused" if self.paused else f"t={tick:.0f}/{duration:.0f}",
            f"{fps:.0f} fps",
            "F wire  B bones",
        ])

    # --- events ---

    def handle_events(self):
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                return False
            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    return False
                if event.key == pygame.K_SPACE:
                    self.paused = not self.paused
                if event.key == pygame.K_f:
                    self.wireframe = not self.wireframe
                if event.key == pygame.K_b:
                    self.show_skeleton = not self.show_skeleton
                if event.key in (pygame.K_1, pygame.K_2, pygame.K_3):
                    idx = event.key - pygame.K_1
                    if idx < len(self.anim_order):
                        self.current = self.anim_order[idx]
                        self.sim_time = 0.0
            elif event.type == pygame.MOUSEBUTTONDOWN:
                if event.button == 1:
                    self.dragging = True
                elif event.button == 4:
                    self.cam_dist = max(1.0, self.cam_dist * 0.9)
                elif event.button == 5:
                    self.cam_dist = min(30.0, self.cam_dist * 1.1)
            elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
                self.dragging = False
            elif event.type == pygame.MOUSEMOTION and self.dragging:
                self.cam_yaw -= event.rel[0] * 0.4
                self.cam_pitch = min(85.0, max(-85.0, self.cam_pitch + event.rel[1] * 0.4))
        return True

    # --- run loops ---

    def run(self):
        while self.handle_events():
            dt = self.clock.tick(60) / 1000.0
            self.frame(dt)
            pygame.display.flip()
        pygame.quit()

    def run_screenshot(self, frames):
        out = Path(self.args.screenshot)
        data = None
        for i in range(frames):
            self.frame(1.0 / 30.0)
            # read the framebuffer before pygame.display.flip() swaps it away
            if i == frames - 1:
                data = self.ctx.fbo.read(components=3)
            pygame.display.flip()
        from PIL import Image
        image = Image.frombytes("RGB", self.size, data)
        image = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        image.save(out)
        print(f"saved screenshot: {out} ({frames} frames simulated)")
        pygame.quit()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--screenshot", metavar="PNG", default=None,
                        help="headless: render --frames frames and save a screenshot, then exit")
    parser.add_argument("--frames", type=int, default=90, help="frames to simulate before screenshot")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    args = parser.parse_args()

    if args.screenshot and not any(os.environ.get(v) for v in ("DISPLAY", "WAYLAND_DISPLAY")):
        # no display available: try SDL's offscreen driver (GL via EGL)
        os.environ.setdefault("SDL_VIDEODRIVER", "offscreen")

    demo = Demo(args)
    if args.screenshot:
        demo.run_screenshot(args.frames)
    else:
        demo.run()


if __name__ == "__main__":
    main()
