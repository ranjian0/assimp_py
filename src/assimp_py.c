#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h> // For PyMemberDef, T_* flags, offsetof
#include <stdio.h>        // For FILE, fopen, etc. (though only used for existence check)
#include <string.h>       // For strcmp, memcpy
#include <stdlib.h>       // For malloc, free

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/anim.h>

// Forward declarations for type objects
static PyTypeObject MeshType;
static PyTypeObject SceneType;
static PyTypeObject NodeType;
static PyTypeObject BoneType;
static PyTypeObject AnimationType;
static PyTypeObject NodeAnimType;

// --- Node Type Definition ---
typedef struct Node {
    PyObject_HEAD
    PyObject *name;          // PyUnicodeObject
    PyObject *transformation;// Tuple[Tuple[float,...],...] (4x4 matrix)
    PyObject *parent_name;   // PyUnicodeObject (name of parent) or Py_None
    PyObject *children;      // PyList of Node
    PyObject *mesh_indices;  // PyTuple of PyLongObject

    // Store counts directly for convenience
    unsigned int num_children;
    unsigned int num_meshes;
} Node;

static int Node_init(Node *self, PyObject *args, PyObject *kwds) {
    self->name = NULL;
    self->transformation = NULL;
    self->parent_name = NULL;
    self->children = NULL;
    self->mesh_indices = NULL;
    self->num_children = 0;
    self->num_meshes = 0;
    return 0;
}

static void Node_dealloc(Node *self) {
    Py_CLEAR(self->name);
    Py_CLEAR(self->transformation);
    Py_CLEAR(self->parent_name);
    Py_CLEAR(self->children);
    Py_CLEAR(self->mesh_indices);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyMemberDef Node_members[] = {
    {"name", T_OBJECT_EX, offsetof(Node, name), READONLY, "Node name"},
    {"transformation", T_OBJECT_EX, offsetof(Node, transformation), READONLY, "4x4 transformation matrix (tuple of tuples)"},
    {"parent_name", T_OBJECT_EX, offsetof(Node, parent_name), READONLY, "Name of the parent node (str or None)"},
    {"children", T_OBJECT_EX, offsetof(Node, children), READONLY, "List of child Node objects"},
    {"mesh_indices", T_OBJECT_EX, offsetof(Node, mesh_indices), READONLY, "Tuple of mesh indices associated with this node"},
    {"num_children", T_UINT, offsetof(Node, num_children), READONLY, "Number of children"},
    {"num_meshes", T_UINT, offsetof(Node, num_meshes), READONLY, "Number of meshes referenced"},
    {NULL} /* Sentinel */
};

static PyTypeObject NodeType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp_py.Node",
    .tp_doc = "Node in the scene hierarchy",
    .tp_basicsize = sizeof(Node),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)Node_init,
    .tp_dealloc = (destructor)Node_dealloc,
    .tp_members = Node_members,
};


// --- Bone Type Definition ---
typedef struct {
    PyObject_HEAD
    PyObject *name;              // PyUnicodeObject
    PyObject *offset_matrix;     // Tuple[Tuple[float,...],...] (4x4 matrix)
    PyObject *weights;           // PyMemoryView (float32, N) or None
    PyObject *weight_vertex_ids; // PyMemoryView (uint32, N) or None
    PyObject *armature_name;     // PyUnicodeObject or None (requires Process_PopulateArmatureData)
    PyObject *node_name;         // PyUnicodeObject or None (requires Process_PopulateArmatureData)

    unsigned int num_weights;

    // --- C Data Pointers (managed internally) ---
    float *c_weights;
    unsigned int *c_weight_ids;
} Bone;

static int Bone_init(Bone *self, PyObject *args, PyObject *kwds) {
    self->name = NULL;
    self->offset_matrix = NULL;
    self->weights = NULL;
    self->weight_vertex_ids = NULL;
    self->armature_name = NULL;
    self->node_name = NULL;
    self->num_weights = 0;
    self->c_weights = NULL;
    self->c_weight_ids = NULL;
    return 0;
}

static void Bone_dealloc(Bone *self) {
    Py_CLEAR(self->name);
    Py_CLEAR(self->offset_matrix);
    Py_CLEAR(self->weights);
    Py_CLEAR(self->weight_vertex_ids);
    Py_CLEAR(self->armature_name);
    Py_CLEAR(self->node_name);
    free(self->c_weights);
    free(self->c_weight_ids);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyMemberDef Bone_members[] = {
    {"name", T_OBJECT_EX, offsetof(Bone, name), READONLY, "Bone name"},
    {"offset_matrix", T_OBJECT_EX, offsetof(Bone, offset_matrix), READONLY, "Mesh-to-bone bind pose matrix (tuple of tuples, 4x4)"},
    {"num_weights", T_UINT, offsetof(Bone, num_weights), READONLY, "Number of vertex weights"},
    {"weights", T_OBJECT_EX, offsetof(Bone, weights), READONLY, "Influence strengths (memoryview, float32, N)"},
    {"weight_vertex_ids", T_OBJECT_EX, offsetof(Bone, weight_vertex_ids), READONLY, "Vertex indices parallel to weights (memoryview, uint32, N)"},
    {"armature_name", T_OBJECT_EX, offsetof(Bone, armature_name), READONLY, "Name of the armature root node (str or None; requires Process_PopulateArmatureData)"},
    {"node_name", T_OBJECT_EX, offsetof(Bone, node_name), READONLY, "Name of the scene node matching this bone (str or None; requires Process_PopulateArmatureData)"},
    {NULL} /* Sentinel */
};

static PyTypeObject BoneType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp_py.Bone",
    .tp_doc = "Bone of a mesh: bind pose offset matrix and vertex weights",
    .tp_basicsize = sizeof(Bone),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)Bone_init,
    .tp_dealloc = (destructor)Bone_dealloc,
    .tp_members = Bone_members,
};


// --- NodeAnim Type Definition ---
typedef struct {
    PyObject_HEAD
    PyObject *node_name;          // PyUnicodeObject
    PyObject *position_key_times; // PyMemoryView (float64, N) or None
    PyObject *position_key_values;// PyMemoryView (float32, N*3) or None
    PyObject *rotation_key_times; // PyMemoryView (float64, N) or None
    PyObject *rotation_key_values;// PyMemoryView (float32, N*4) or None
    PyObject *scaling_key_times;  // PyMemoryView (float64, N) or None
    PyObject *scaling_key_values; // PyMemoryView (float32, N*3) or None

    unsigned int num_position_keys;
    unsigned int num_rotation_keys;
    unsigned int num_scaling_keys;
    unsigned int pre_state;       // aiAnimBehaviour (AnimBehaviour_* constants)
    unsigned int post_state;      // aiAnimBehaviour

    // --- C Data Pointers (managed internally) ---
    double *c_pos_times;
    float  *c_pos_values;
    double *c_rot_times;
    float  *c_rot_values;
    double *c_scale_times;
    float  *c_scale_values;
} NodeAnim;

static int NodeAnim_init(NodeAnim *self, PyObject *args, PyObject *kwds) {
    self->node_name = NULL;
    self->position_key_times = NULL;
    self->position_key_values = NULL;
    self->rotation_key_times = NULL;
    self->rotation_key_values = NULL;
    self->scaling_key_times = NULL;
    self->scaling_key_values = NULL;
    self->num_position_keys = 0;
    self->num_rotation_keys = 0;
    self->num_scaling_keys = 0;
    self->pre_state = 0;
    self->post_state = 0;
    self->c_pos_times = NULL;
    self->c_pos_values = NULL;
    self->c_rot_times = NULL;
    self->c_rot_values = NULL;
    self->c_scale_times = NULL;
    self->c_scale_values = NULL;
    return 0;
}

static void NodeAnim_dealloc(NodeAnim *self) {
    Py_CLEAR(self->node_name);
    Py_CLEAR(self->position_key_times);
    Py_CLEAR(self->position_key_values);
    Py_CLEAR(self->rotation_key_times);
    Py_CLEAR(self->rotation_key_values);
    Py_CLEAR(self->scaling_key_times);
    Py_CLEAR(self->scaling_key_values);
    free(self->c_pos_times);
    free(self->c_pos_values);
    free(self->c_rot_times);
    free(self->c_rot_values);
    free(self->c_scale_times);
    free(self->c_scale_values);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyMemberDef NodeAnim_members[] = {
    {"node_name", T_OBJECT_EX, offsetof(NodeAnim, node_name), READONLY, "Name of the node/bone this channel animates"},
    {"num_position_keys", T_UINT, offsetof(NodeAnim, num_position_keys), READONLY, "Number of position keys"},
    {"position_key_times", T_OBJECT_EX, offsetof(NodeAnim, position_key_times), READONLY, "Position key times (memoryview, float64, N)"},
    {"position_key_values", T_OBJECT_EX, offsetof(NodeAnim, position_key_values), READONLY, "Position key values (memoryview, float32, Nx3)"},
    {"num_rotation_keys", T_UINT, offsetof(NodeAnim, num_rotation_keys), READONLY, "Number of rotation keys"},
    {"rotation_key_times", T_OBJECT_EX, offsetof(NodeAnim, rotation_key_times), READONLY, "Rotation key times (memoryview, float64, N)"},
    {"rotation_key_values", T_OBJECT_EX, offsetof(NodeAnim, rotation_key_values), READONLY, "Rotation key quaternions (memoryview, float32, Nx4 as x,y,z,w)"},
    {"num_scaling_keys", T_UINT, offsetof(NodeAnim, num_scaling_keys), READONLY, "Number of scaling keys"},
    {"scaling_key_times", T_OBJECT_EX, offsetof(NodeAnim, scaling_key_times), READONLY, "Scaling key times (memoryview, float64, N)"},
    {"scaling_key_values", T_OBJECT_EX, offsetof(NodeAnim, scaling_key_values), READONLY, "Scaling key values (memoryview, float32, Nx3)"},
    {"pre_state", T_UINT, offsetof(NodeAnim, pre_state), READONLY, "Behaviour before the first key (AnimBehaviour_*)"},
    {"post_state", T_UINT, offsetof(NodeAnim, post_state), READONLY, "Behaviour after the last key (AnimBehaviour_*)"},
    {NULL} /* Sentinel */
};

static PyTypeObject NodeAnimType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp_py.NodeAnim",
    .tp_doc = "Animation channel for a single node: position/rotation/scaling keyframes",
    .tp_basicsize = sizeof(NodeAnim),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)NodeAnim_init,
    .tp_dealloc = (destructor)NodeAnim_dealloc,
    .tp_members = NodeAnim_members,
};


// --- Animation Type Definition ---
typedef struct {
    PyObject_HEAD
    PyObject *name;      // PyUnicodeObject
    PyObject *channels;  // PyList of NodeAnim
    unsigned int num_channels;
    double duration;           // aiAnimation::mDuration
    double ticks_per_second;   // aiAnimation::mTicksPerSecond (0.0 = unspecified)
} Animation;

static int Animation_init(Animation *self, PyObject *args, PyObject *kwds) {
    self->name = NULL;
    self->channels = NULL;
    self->num_channels = 0;
    self->duration = 0.0;
    self->ticks_per_second = 0.0;
    return 0;
}

static void Animation_dealloc(Animation *self) {
    Py_CLEAR(self->name);
    Py_CLEAR(self->channels);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyMemberDef Animation_members[] = {
    {"name", T_OBJECT_EX, offsetof(Animation, name), READONLY, "Animation name"},
    {"duration", T_DOUBLE, offsetof(Animation, duration), READONLY, "Duration in ticks"},
    {"ticks_per_second", T_DOUBLE, offsetof(Animation, ticks_per_second), READONLY, "Ticks per second (0.0 = same as duration, i.e. seconds)"},
    {"num_channels", T_UINT, offsetof(Animation, num_channels), READONLY, "Number of animation channels"},
    {"channels", T_OBJECT_EX, offsetof(Animation, channels), READONLY, "List of NodeAnim channels"},
    {NULL} /* Sentinel */
};

static PyTypeObject AnimationType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp_py.Animation",
    .tp_doc = "Animation: key-frame channels for a number of nodes",
    .tp_basicsize = sizeof(Animation),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)Animation_init,
    .tp_dealloc = (destructor)Animation_dealloc,
    .tp_members = Animation_members,
};


// --- Mesh Type Definition ---
typedef struct {
    PyObject_HEAD
    // --- Python Objects (exposed as attributes) ---
    PyObject *name;             // PyUnicodeObject
    PyObject *num_uv_components;// List of ints (or maybe just one int if needed?)
    PyObject *indices;          // PyMemoryView (uint32)
    PyObject *vertices;         // PyMemoryView (float32 x 3)
    PyObject *normals;          // PyMemoryView (float32 x 3) or None
    PyObject *tangents;         // PyMemoryView (float32 x 3) or None
    PyObject *bitangents;       // PyMemoryView (float32 x 3) or None
    PyObject *colors;           // List of PyMemoryView (float32 x 4) or None
    PyObject *texcoords;        // List of PyMemoryView (float32 x N) or None
    PyObject *bones;            // PyList of Bone (empty list if no bones)

    // --- C Data Pointers (managed internally) ---
    // We store these to manage the lifetime of the memory backing the memoryviews
    unsigned int *c_indices;
    float *c_vertices;
    float *c_normals;
    float *c_tangents;
    float *c_bitangents;
    float **c_colors;           // Array of pointers to color sets
    float **c_texcoords;        // Array of pointers to texcoord sets

    // --- Other Attributes ---
    unsigned int num_vertices;
    unsigned int num_indices;    // Total number of indices (num_faces * 3 if triangulated)
    unsigned int num_faces;      // Original number of faces
    unsigned int material_index;
    unsigned int num_color_sets;
    unsigned int num_texcoord_sets;
    unsigned int num_bones;
    unsigned int *c_num_uv_components; // Array for UV component counts

} Mesh;

static int Mesh_init(Mesh *self, PyObject *args, PyObject *kwds) {
    // Initialize all PyObject pointers to NULL is crucial for safe deallocation
    self->name = NULL;
    self->num_uv_components = NULL;
    self->indices = NULL;
    self->vertices = NULL;
    self->normals = NULL;
    self->tangents = NULL;
    self->bitangents = NULL;
    self->colors = NULL;
    self->texcoords = NULL;
    self->bones = NULL;
    self->c_indices = NULL;
    self->c_vertices = NULL;
    self->c_normals = NULL;
    self->c_tangents = NULL;
    self->c_bitangents = NULL;
    self->c_colors = NULL;
    self->c_texcoords = NULL;
    self->c_num_uv_components = NULL;

    self->num_vertices = 0;
    self->num_indices = 0;
    self->num_faces = 0;
    self->material_index = 0;
    self->num_color_sets = 0;
    self->num_texcoord_sets = 0;
    self->num_bones = 0;
    return 0;
}

static void Mesh_dealloc(Mesh *self) {
    // Clear Python objects (decrements reference count)
    Py_CLEAR(self->name);
    Py_CLEAR(self->num_uv_components);
    Py_CLEAR(self->indices);
    Py_CLEAR(self->vertices);
    Py_CLEAR(self->normals);
    Py_CLEAR(self->tangents);
    Py_CLEAR(self->bitangents);
    Py_CLEAR(self->colors);
    Py_CLEAR(self->texcoords);
    Py_CLEAR(self->bones);

    // Free C arrays
    free(self->c_indices);
    free(self->c_vertices);
    free(self->c_normals);
    free(self->c_tangents);
    free(self->c_bitangents);
    if (self->c_colors) {
        for (unsigned int i = 0; i < self->num_color_sets; ++i) {
            free(self->c_colors[i]);
        }
        free(self->c_colors);
    }
    if (self->c_texcoords) {
        for (unsigned int i = 0; i < self->num_texcoord_sets; ++i) {
            free(self->c_texcoords[i]);
        }
        free(self->c_texcoords);
    }
    free(self->c_num_uv_components);


    // Free the object itself
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyMemberDef Mesh_members[] = {
    {"name", T_OBJECT_EX, offsetof(Mesh, name), READONLY, "Mesh name"},
    {"material_index", T_UINT, offsetof(Mesh, material_index), READONLY, "Material index for this mesh"},
    {"num_vertices", T_UINT, offsetof(Mesh, num_vertices), READONLY, "Number of vertices"},
    {"num_faces", T_UINT, offsetof(Mesh, num_faces), READONLY, "Number of faces"},
    {"num_indices", T_UINT, offsetof(Mesh, num_indices), READONLY, "Total number of indices"},

    // Data attributes (MemoryViews or None)
    {"indices", T_OBJECT_EX, offsetof(Mesh, indices), READONLY, "Vertex indices (memoryview, uint32)"},
    {"vertices", T_OBJECT_EX, offsetof(Mesh, vertices), READONLY, "Vertex positions (memoryview, float32, Nx3)"},
    {"normals", T_OBJECT_EX, offsetof(Mesh, normals), READONLY, "Vertex normals (memoryview, float32, Nx3 or None)"},
    {"tangents", T_OBJECT_EX, offsetof(Mesh, tangents), READONLY, "Vertex tangents (memoryview, float32, Nx3 or None)"},
    {"bitangents", T_OBJECT_EX, offsetof(Mesh, bitangents), READONLY, "Vertex bitangents (memoryview, float32, Nx3 or None)"},
    {"colors", T_OBJECT_EX, offsetof(Mesh, colors), READONLY, "List of vertex color sets (list of memoryview, float32, Nx4 or None)"},
    {"texcoords", T_OBJECT_EX, offsetof(Mesh, texcoords), READONLY, "List of vertex texture coordinate sets (list of memoryview, float32, NxNcomp or None)"},
    {"num_uv_components", T_OBJECT_EX, offsetof(Mesh, num_uv_components), READONLY, "List of component counts for each texcoord set"},
    {"num_bones", T_UINT, offsetof(Mesh, num_bones), READONLY, "Number of bones"},
    {"bones", T_OBJECT_EX, offsetof(Mesh, bones), READONLY, "List of Bone objects (empty list if the mesh has no bones)"},
    {NULL} /* Sentinel */
};

static PyTypeObject MeshType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp_py.Mesh",
    .tp_doc = "Mesh object containing vertex data and indices",
    .tp_basicsize = sizeof(Mesh),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)Mesh_init,
    .tp_dealloc = (destructor)Mesh_dealloc,
    .tp_members = Mesh_members,
};


// --- Scene Type Definition ---
typedef struct {
    PyObject_HEAD
    PyObject *meshes;     // List of Mesh objects
    PyObject *materials;  // List of Material dictionaries
    PyObject *root_node;
    PyObject *animations; // List of Animation objects (empty list if none)
    unsigned int num_meshes;
    unsigned int num_materials;
    unsigned int num_animations;
} Scene;

static int Scene_init(Scene *self, PyObject *args, PyObject *kwds) {
    self->meshes = NULL;
    self->materials = NULL;
    self->root_node = NULL;
    self->animations = NULL;
    self->num_meshes = 0;
    self->num_materials = 0;
    self->num_animations = 0;
    return 0;
}

static void Scene_dealloc(Scene *self) {
    Py_CLEAR(self->meshes);
    Py_CLEAR(self->materials);
    Py_CLEAR(self->root_node);
    Py_CLEAR(self->animations);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyMemberDef Scene_members[] = {
    {"meshes", T_OBJECT_EX, offsetof(Scene, meshes), READONLY, "List of meshes in the scene"},
    {"materials", T_OBJECT_EX, offsetof(Scene, materials), READONLY, "List of materials (dictionaries) in the scene"},
    {"root_node", T_OBJECT_EX, offsetof(Scene, root_node), READONLY, "Root node of the scene hierarchy"},
    {"num_meshes", T_UINT, offsetof(Scene, num_meshes), READONLY, "Number of meshes"},
    {"num_materials", T_UINT, offsetof(Scene, num_materials), READONLY, "Number of materials"},
    {"animations", T_OBJECT_EX, offsetof(Scene, animations), READONLY, "List of animations in the scene (empty list if none)"},
    {"num_animations", T_UINT, offsetof(Scene, num_animations), READONLY, "Number of animations"},
    {NULL} /* Sentinel */
};

static PyTypeObject SceneType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp_py.Scene",
    .tp_doc = "Scene object containing meshes and materials loaded from a file",
    .tp_basicsize = sizeof(Scene),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)Scene_init,
    .tp_dealloc = (destructor)Scene_dealloc,
    .tp_members = Scene_members,
};


// --- Helper Functions ---

// Safely create a memory view from a C array. Returns new reference or NULL on error.
static PyObject* create_memoryview(void* data, Py_ssize_t len_bytes, const char* format, Py_ssize_t itemsize) {
    if (!data) {
        Py_RETURN_NONE; // Return None if the C data pointer is NULL
    }
    if (itemsize <= 0) {
         PyErr_SetString(PyExc_ValueError, "itemsize must be positive");
         return NULL;
    }
    if (len_bytes < 0) {
        PyErr_SetString(PyExc_ValueError, "len_bytes cannot be negative");
        return NULL;
    }
    // Check if len_bytes is divisible by itemsize
    if (len_bytes % itemsize != 0) {
        PyErr_Format(PyExc_ValueError, "Buffer length (%zd) is not a multiple of itemsize (%zd)", len_bytes, itemsize);
        return NULL;
    }

    Py_ssize_t num_elements = len_bytes / itemsize;

    Py_buffer buf_info;
    // Allocate shape and strides arrays (needed by PyMemoryView_FromBuffer)
    // For 1D array, we only need arrays of size 1
    Py_ssize_t shape[1] = { num_elements };
    Py_ssize_t strides[1] = { itemsize }; // Simple stride for contiguous data

    // Manually initialize the Py_buffer struct
    // Start by zeroing it out
    memset(&buf_info, 0, sizeof(Py_buffer));

    buf_info.buf = data;              // Pointer to the data buffer
    buf_info.obj = NULL;              // We own the memory (malloc'd), no base Python object
    buf_info.len = len_bytes;         // Total length in bytes
    buf_info.itemsize = itemsize;     // Size of one item
    buf_info.readonly = 1;            // Make it read-only
    buf_info.ndim = 1;                // Number of dimensions
    buf_info.format = (char*)format;  // Format string (cast needed)
    buf_info.shape = shape;           // Pointer to shape array {num_elements}
    buf_info.strides = strides;       // Pointer to strides array {itemsize}
    buf_info.suboffsets = NULL;       // Not needed for simple buffers
    buf_info.internal = NULL;         // Reserved

    // Create the memoryview from the manually populated buffer structure
    PyObject *memview = PyMemoryView_FromBuffer(&buf_info);
    if (!memview) {
        // Error should be set by PyMemoryView_FromBuffer
        return NULL;
    }

    // Note: PyMemoryView_FromBuffer creates the memoryview object.
    // The 'buf_info' struct itself is used during creation but doesn't
    // need to persist after the memoryview object exists, as the relevant
    // info is copied or managed internally by the memoryview object.
    // The shape and strides arrays declared on the stack are sufficient here.

    return memview; // Return new reference
}

// Helper to create a Python list of floats from a C array of aiColor4D
// Returns a new reference or NULL on error.
static PyObject* list_from_color4d_array(const struct aiColor4D* colors, unsigned int count) {
    PyObject* list = PyList_New(count);
    if (!list) return NULL;

    for (unsigned int i = 0; i < count; ++i) {
        PyObject* tuple = Py_BuildValue("ffff", colors[i].r, colors[i].g, colors[i].b, colors[i].a);
        if (!tuple) {
            Py_DECREF(list);
            return NULL;
        }
        // PyList_SetItem steals the reference to tuple, no DECREF needed on success
        if (PyList_SetItem(list, i, tuple) < 0) {
             Py_DECREF(tuple);
             Py_DECREF(list);
             return NULL;
        }
    }
    return list;
}

// Helper to get a material property value by iterating properties and using generic getters.
// Returns new reference or NULL on error.
static PyObject* get_material_property(struct aiMaterial *mat, const char *key, unsigned int type, unsigned int index) {
    aiReturn ret = aiReturn_FAILURE;
    PyObject *py_value = NULL;
    const struct aiMaterialProperty* prop = NULL;

    // 1. Find the specific property in the material's list
    for (unsigned int i = 0; i < mat->mNumProperties; ++i) {
        // Use .data for aiString comparison
        if (strcmp(mat->mProperties[i]->mKey.data, key) == 0 &&
            mat->mProperties[i]->mSemantic == type &&
            mat->mProperties[i]->mIndex == index)
        {
            prop = mat->mProperties[i];
            break;
        }
    }

    // If the property wasn't found in the list, return None.
    if (!prop) {
        // fprintf(stderr, "DEBUG get_material_property: Property not found for key='%s', type=%u, index=%u\n", key, type, index);
        Py_RETURN_NONE;
    }

    // --- Use Fixed-Size Buffers for Array Getters ---
    float fval[16];       // Fixed buffer for floats
    int ival[16];         // Fixed buffer for ints
    struct aiString sval; // For strings
    unsigned int arr_size; // Input: buffer capacity, Output: elements written


    // 2. Process based on the found property's type (prop->mType)
    switch (prop->mType) {
        case aiPTI_String:
            // Use aiGetMaterialString (it directly uses the underlying prop->mData)
            ret = aiGetMaterialString(mat, key, type, index, &sval);
            if (ret == aiReturn_SUCCESS) {
                py_value = PyUnicode_FromString(sval.data); // Use .data
                if (!py_value) { ret = aiReturn_FAILURE; } // Check PyUnicode creation
            }
            break; // End String case

        case aiPTI_Float:
        case aiPTI_Buffer: // Treat buffer as float
        case aiPTI_Double: // aiGetMaterialFloatArray handles double conversion internally
             arr_size = 16; // Input: Our buffer capacity
             // Call the generic Float Array getter
             ret = aiGetMaterialFloatArray(mat, key, type, index, fval, &arr_size);
             // fprintf(stderr, "DEBUG get_material_property (Float Fixed): key='%s', ret=%d, arr_size_written=%u\n", key, ret, arr_size);

             if (ret == aiReturn_SUCCESS && arr_size > 0) { // Check success AND elements retrieved
                 if (arr_size == 1) {
                     py_value = PyFloat_FromDouble((double)fval[0]);
                 } else {
                     py_value = PyList_New(arr_size);
                     if (py_value) {
                         for (unsigned int i = 0; i < arr_size; ++i) {
                             PyObject* f = PyFloat_FromDouble((double)fval[i]);
                             if (!f) { Py_DECREF(py_value); py_value = NULL; break; }
                             PyList_SET_ITEM(py_value, i, f); // Steals reference
                         }
                         if (!py_value && !PyErr_Occurred()) {
                             PyErr_SetString(PyExc_RuntimeError, "Failed to build float list items");
                         }
                     } // else PyList_New failed
                 }
                 // Check object creation success
                 if (!py_value) { ret = aiReturn_FAILURE; }
             } else if (ret == aiReturn_SUCCESS && arr_size == 0) {
                 // Property type was float/double/buffer, but getter returned 0 elements.
                 // This shouldn't typically happen if prop->mType matched, but handle it.
                 ret = aiReturn_FAILURE; // Treat as failure to get a *value*
             } // else ret != SUCCESS
             break; // End Float/Buffer/Double case

        case aiPTI_Integer:
             arr_size = 16; // Input: Our buffer capacity
             // Call the generic Integer Array getter
             ret = aiGetMaterialIntegerArray(mat, key, type, index, ival, &arr_size);
             // fprintf(stderr, "DEBUG get_material_property (Int Fixed): key='%s', ret=%d, arr_size_written=%u\n", key, ret, arr_size);

             if (ret == aiReturn_SUCCESS && arr_size > 0) { // Check success AND elements retrieved
                 if (arr_size == 1) {
                     // NOTE: No special handling for shininess here, return as Long
                     // Conversion to float if needed should happen in Python layer
                     py_value = PyLong_FromLong((long)ival[0]);
                 } else { // List of integers
                     py_value = PyList_New(arr_size);
                     if (py_value) {
                         for (unsigned int i = 0; i < arr_size; ++i) {
                             PyObject* integer = PyLong_FromLong((long)ival[i]);
                             if (!integer) { Py_DECREF(py_value); py_value = NULL; break; }
                             PyList_SET_ITEM(py_value, i, integer); // Steals reference
                         }
                         if (!py_value && !PyErr_Occurred()) {
                            PyErr_SetString(PyExc_RuntimeError, "Failed to build integer list items");
                         }
                     } // else PyList_New failed
                 }
                  // Check object creation success
                 if (!py_value) { ret = aiReturn_FAILURE; }
            } else if (ret == aiReturn_SUCCESS && arr_size == 0) {
                 // Property type was int, but getter returned 0 elements.
                 ret = aiReturn_FAILURE; // Treat as failure to get a *value*
            } // else ret != SUCCESS
            break; // End Integer case

        default:
             // Unsupported property type found in the material's property list
             // fprintf(stderr, "DEBUG get_material_property: Unsupported property type %d for key='%s'\n", prop->mType, key);
             ret = aiReturn_FAILURE; // Mark as failure
             break;
    } // end switch

    // --- Final Return Decision ---
    if (ret == aiReturn_SUCCESS && py_value != NULL) {
        // Data successfully retrieved and converted
        return py_value;
    } else {
        // Something failed or no value was produced (e.g., ret!=SUCCESS, arr_size=0, Py object creation fail)
        Py_XDECREF(py_value); // Ensure cleanup if partially created
        if (!PyErr_Occurred()) {
            // No specific Python error set - return None
            Py_RETURN_NONE;
        } else {
            // Propagate existing Python error
            return NULL;
        }
    }
}

// Helper to create a Python tuple of floats from a row of aiMatrix4x4
// Returns a NEW reference or NULL on error
static PyObject* tuple_from_matrix4x4_row(const float* row_data) {
    return Py_BuildValue("ffff", row_data[0], row_data[1], row_data[2], row_data[3]);
    // Py_BuildValue returns NULL on error and sets exception
}

// Helper to create Python tuple (4x4) from aiMatrix4x4
// Returns a NEW reference or NULL on error
static PyObject* tuple_from_matrix4x4(const struct aiMatrix4x4* mat) {
    PyObject* matrix_tuple = PyTuple_New(4);
    if (!matrix_tuple) return NULL;

    PyObject* row0 = tuple_from_matrix4x4_row(&(mat->a1)); // Assimp stores row-major: a1-a4 is row 0
    PyObject* row1 = tuple_from_matrix4x4_row(&(mat->b1)); // b1-b4 is row 1
    PyObject* row2 = tuple_from_matrix4x4_row(&(mat->c1)); // c1-c4 is row 2
    PyObject* row3 = tuple_from_matrix4x4_row(&(mat->d1)); // d1-d4 is row 3

    if (!row0 || !row1 || !row2 || !row3) {
        Py_XDECREF(row0); Py_XDECREF(row1); Py_XDECREF(row2); Py_XDECREF(row3);
        Py_DECREF(matrix_tuple);
        return NULL;
    }

    // PyTuple_SET_ITEM steals references
    PyTuple_SET_ITEM(matrix_tuple, 0, row0);
    PyTuple_SET_ITEM(matrix_tuple, 1, row1);
    PyTuple_SET_ITEM(matrix_tuple, 2, row2);
    PyTuple_SET_ITEM(matrix_tuple, 3, row3);

    return matrix_tuple;
}

// Helper to create Python tuple from unsigned int array
// Returns a NEW reference or NULL on error
static PyObject* tuple_from_uint_array(const unsigned int* arr, unsigned int size) {
    if (size == 0 || !arr) { // Handle case of no meshes
        return PyTuple_New(0); // Return empty tuple
    }
    PyObject* tuple = PyTuple_New(size);
    if (!tuple) return NULL;

    for (unsigned int i = 0; i < size; ++i) {
        PyObject* val = PyLong_FromUnsignedLong(arr[i]);
        if (!val) {
            Py_DECREF(tuple);
            return NULL;
        }
        PyTuple_SET_ITEM(tuple, i, val); // Steals reference
    }
    return tuple;
}

// Helper to convert ASSIMP property names to better format
static char* nice_prop_name(char* prop) {
    if (strcmp(prop, "?mat.name") == 0)
        return "NAME";
    else if (strcmp(prop, "$mat.twosided") == 0)
        return "TWOSIDED";
    else if (strcmp(prop, "$mat.shadingm") == 0)
        return "SHADING_MODEL";
    else if (strcmp(prop, "$mat.wireframe") == 0)
        return "ENABLE_WIREFRAME";
    else if (strcmp(prop, "$mat.blend") == 0)
        return "BLEND_FUNC";
    else if (strcmp(prop, "$mat.opacity") == 0)
        return "OPACITY";
    else if (strcmp(prop, "$mat.bumpscaling") == 0)
        return "BUMPSCALING";
    else if (strcmp(prop, "$mat.shininess") == 0)
        return "SHININESS";
    else if (strcmp(prop, "$mat.reflectivity") == 0)
        return "REFLECTIVITY";
    else if (strcmp(prop, "$mat.shinpercent") == 0)
        return "SHININESS_STRENGTH";
    else if (strcmp(prop, "$mat.refracti") == 0)
        return "REFRACTI";
    else if (strcmp(prop, "$clr.diffuse") == 0)
        return "COLOR_DIFFUSE";
    else if (strcmp(prop, "$clr.ambient") == 0)
        return "COLOR_AMBIENT";
    else if (strcmp(prop, "$clr.specular") == 0)
        return "COLOR_SPECULAR";
    else if (strcmp(prop, "$clr.emissive") == 0)
        return "COLOR_EMISSIVE";
    else if (strcmp(prop, "$clr.transparent") == 0)
        return "COLOR_TRANSPARENT";
    else if (strcmp(prop, "$clr.reflective") == 0)
        return "COLOR_REFLECTIVE";
    else if (strcmp(prop, "?bg.global") == 0)
        return "GLOBAL_BACKGROUND_IMAGE";
    else if (strcmp(prop, "$tex.file") == 0)
        return "TEXTURE_BASE";
    else if (strcmp(prop, "$tex.mapping") == 0)
        return "MAPPING_BASE";
    else if (strcmp(prop, "$tex.flags") == 0)
        return "TEXFLAGS_BASE";
    else if (strcmp(prop, "$tex.uvwsrc") == 0)
        return "UVWSRC_BASE";
    else if (strcmp(prop, "$tex.mapmodev") == 0)
        return "MAPPINGMODE_V_BASE";
    else if (strcmp(prop, "$tex.mapaxis") == 0)
        return "TEXMAP_AXIS_BASE";
    else if (strcmp(prop, "$tex.blend") == 0)
        return "TEXBLEND_BASE";
    else if (strcmp(prop, "$tex.uvtrafo") == 0)
        return "UVTRANSFORM_BASE";
    else if (strcmp(prop, "$tex.op") == 0)
        return "TEXOP_BASE";
    else if (strcmp(prop, "$tex.mapmodeu") == 0)
        return "MAPPINGMODE_U_BASE";
    else
        return "NONE";
}


// --- Scene Processing Logic ---

// --- Forward Declaration for Recursion ---
static PyObject* process_node_recursive(struct aiNode* c_node);


// Process materials from aiScene into a Python list of dictionaries.
// Returns a new reference to the list, or NULL on error.
static PyObject* process_materials(const struct aiScene *c_scene) {
    unsigned int num_materials = c_scene->mNumMaterials;
    PyObject *py_materials_list = PyList_New(num_materials);
    if (!py_materials_list) return NULL;

    for (unsigned int i = 0; i < num_materials; ++i) {
        struct aiMaterial *mat = c_scene->mMaterials[i];
        PyObject *mat_dict = PyDict_New();
        if (!mat_dict) goto fail_mat_list;

        // Iterate through material properties
        for (unsigned int p = 0; p < mat->mNumProperties; ++p) {
            struct aiMaterialProperty *prop = mat->mProperties[p];
            // Use the original key directly
            PyObject *py_key = PyUnicode_FromString(nice_prop_name(prop->mKey.data));
            if (!py_key) {
                Py_DECREF(mat_dict);
                goto fail_mat_list;
            }

            PyObject *py_value = get_material_property(mat, prop->mKey.data, prop->mSemantic, prop->mIndex);
            if (!py_value) { // Error occurred in get_material_property
                Py_DECREF(py_key);
                Py_DECREF(mat_dict);
                goto fail_mat_list;
            }

            // Only add if value is not None (unless None signifies absence vs error)
            // Let's include None values for completeness.
            if (PyDict_SetItem(mat_dict, py_key, py_value) < 0) {
                Py_DECREF(py_key);
                Py_DECREF(py_value);
                Py_DECREF(mat_dict);
                goto fail_mat_list;
            }
            Py_DECREF(py_key);   // Dereference temporary key
            Py_DECREF(py_value); // Dereference value (SetItem increments)
        }

        // Add textures separately for clarity
        PyObject *textures_dict = PyDict_New();
        if (!textures_dict) {
            Py_DECREF(mat_dict);
            goto fail_mat_list;
        }

        for (int tt = aiTextureType_NONE; tt <= aiTextureType_UNKNOWN; ++tt) {
             enum aiTextureType tex_type = (enum aiTextureType)tt;
             unsigned int tex_count = aiGetMaterialTextureCount(mat, tex_type);
             if (tex_count == 0) continue;

             PyObject *texture_list = PyList_New(tex_count);
             if (!texture_list) {
                 Py_DECREF(textures_dict);
                 Py_DECREF(mat_dict);
                 goto fail_mat_list;
             }

             for(unsigned int tex_idx = 0; tex_idx < tex_count; ++tex_idx) {
                 struct aiString path;
                 // We only retrieve the path here. Other details could be added.
                 aiReturn ret = aiGetMaterialTexture(mat, tex_type, tex_idx, &path,
                                                     NULL, NULL, NULL, NULL, NULL, NULL);
                 if (ret == aiReturn_SUCCESS) {
                     PyObject *py_path = PyUnicode_FromString(path.data);
                     if (!py_path) {
                         Py_DECREF(texture_list);
                         Py_DECREF(textures_dict);
                         Py_DECREF(mat_dict);
                         goto fail_mat_list;
                     }
                     PyList_SET_ITEM(texture_list, tex_idx, py_path); // Steals ref
                 } else {
                     // Handle error or insert None? Let's skip on error.
                     PyErr_Format(PyExc_RuntimeError, "Failed to get texture path for type %d index %d", tt, tex_idx);
                     Py_DECREF(texture_list);
                     Py_DECREF(textures_dict);
                     Py_DECREF(mat_dict);
                     goto fail_mat_list;
                 }
             }
             // Use the texture type enum value as the key
             PyObject *py_tex_type_key = PyLong_FromLong(tex_type);
             if (!py_tex_type_key) {
                 Py_DECREF(texture_list); // List contains owned refs now
                 Py_DECREF(textures_dict);
                 Py_DECREF(mat_dict);
                 goto fail_mat_list;
             }
             if (PyDict_SetItem(textures_dict, py_tex_type_key, texture_list) < 0) {
                 Py_DECREF(py_tex_type_key);
                 Py_DECREF(texture_list);
                 Py_DECREF(textures_dict);
                 Py_DECREF(mat_dict);
                 goto fail_mat_list;
             }
             Py_DECREF(py_tex_type_key);
             Py_DECREF(texture_list); // SetItem increments ref
        }

        // Add the textures dictionary to the main material dictionary
        PyObject *textures_key = PyUnicode_FromString("TEXTURES");
         if (!textures_key) {
             Py_DECREF(textures_dict);
             Py_DECREF(mat_dict);
             goto fail_mat_list;
         }
        if (PyDict_SetItem(mat_dict, textures_key, textures_dict) < 0) {
            Py_DECREF(textures_key);
            Py_DECREF(textures_dict);
            Py_DECREF(mat_dict);
            goto fail_mat_list;
        }
        Py_DECREF(textures_key);
        Py_DECREF(textures_dict); // SetItem increments ref

        // PyList_SetItem steals the reference to mat_dict
        if (PyList_SetItem(py_materials_list, i, mat_dict) < 0) {
            Py_DECREF(mat_dict); // Decref if SetItem failed
            goto fail_mat_list;
        }
    }

    return py_materials_list; // Success

fail_mat_list:
    Py_DECREF(py_materials_list); // Decrement ref count on the main list
    return NULL; // Error
}


// Process a single aiNodeAnim into a Python NodeAnim object.
// Returns a NEW reference or NULL on error.
static PyObject* process_node_anim(const struct aiNodeAnim *c_na) {
    NodeAnim *py_na = (NodeAnim *)NodeAnimType.tp_alloc(&NodeAnimType, 0);
    if (!py_na) return NULL;

    py_na->node_name = PyUnicode_FromString(c_na->mNodeName.data);
    if (!py_na->node_name) goto fail_node_anim;

    py_na->num_position_keys = c_na->mNumPositionKeys;
    py_na->num_rotation_keys = c_na->mNumRotationKeys;
    py_na->num_scaling_keys = c_na->mNumScalingKeys;
    py_na->pre_state = (unsigned int)c_na->mPreState;
    py_na->post_state = (unsigned int)c_na->mPostState;

    // --- Position Keys (time float64 + value float32 x3) ---
    if (c_na->mNumPositionKeys > 0 && c_na->mPositionKeys) {
        py_na->c_pos_times = (double*)malloc(c_na->mNumPositionKeys * sizeof(double));
        py_na->c_pos_values = (float*)malloc(c_na->mNumPositionKeys * 3 * sizeof(float));
        if (!py_na->c_pos_times || !py_na->c_pos_values) { PyErr_NoMemory(); goto fail_node_anim; }

        for (unsigned int k = 0; k < c_na->mNumPositionKeys; ++k) {
            py_na->c_pos_times[k] = c_na->mPositionKeys[k].mTime;
            py_na->c_pos_values[k*3 + 0] = c_na->mPositionKeys[k].mValue.x;
            py_na->c_pos_values[k*3 + 1] = c_na->mPositionKeys[k].mValue.y;
            py_na->c_pos_values[k*3 + 2] = c_na->mPositionKeys[k].mValue.z;
        }

        py_na->position_key_times = create_memoryview(
            py_na->c_pos_times,
            (Py_ssize_t)c_na->mNumPositionKeys * sizeof(double), "d", sizeof(double));
        if (!py_na->position_key_times) goto fail_node_anim;

        py_na->position_key_values = create_memoryview(
            py_na->c_pos_values,
            (Py_ssize_t)c_na->mNumPositionKeys * 3 * sizeof(float), "f", sizeof(float));
        if (!py_na->position_key_values) goto fail_node_anim;
    } else {
        Py_INCREF(Py_None); py_na->position_key_times = Py_None;
        Py_INCREF(Py_None); py_na->position_key_values = Py_None;
    }

    // --- Rotation Keys (time float64 + quaternion float32 x4) ---
    if (c_na->mNumRotationKeys > 0 && c_na->mRotationKeys) {
        py_na->c_rot_times = (double*)malloc(c_na->mNumRotationKeys * sizeof(double));
        py_na->c_rot_values = (float*)malloc(c_na->mNumRotationKeys * 4 * sizeof(float));
        if (!py_na->c_rot_times || !py_na->c_rot_values) { PyErr_NoMemory(); goto fail_node_anim; }

        for (unsigned int k = 0; k < c_na->mNumRotationKeys; ++k) {
            py_na->c_rot_times[k] = c_na->mRotationKeys[k].mTime;
            py_na->c_rot_values[k*4 + 0] = c_na->mRotationKeys[k].mValue.x;
            py_na->c_rot_values[k*4 + 1] = c_na->mRotationKeys[k].mValue.y;
            py_na->c_rot_values[k*4 + 2] = c_na->mRotationKeys[k].mValue.z;
            py_na->c_rot_values[k*4 + 3] = c_na->mRotationKeys[k].mValue.w;
        }

        py_na->rotation_key_times = create_memoryview(
            py_na->c_rot_times,
            (Py_ssize_t)c_na->mNumRotationKeys * sizeof(double), "d", sizeof(double));
        if (!py_na->rotation_key_times) goto fail_node_anim;

        py_na->rotation_key_values = create_memoryview(
            py_na->c_rot_values,
            (Py_ssize_t)c_na->mNumRotationKeys * 4 * sizeof(float), "f", sizeof(float));
        if (!py_na->rotation_key_values) goto fail_node_anim;
    } else {
        Py_INCREF(Py_None); py_na->rotation_key_times = Py_None;
        Py_INCREF(Py_None); py_na->rotation_key_values = Py_None;
    }

    // --- Scaling Keys (time float64 + value float32 x3) ---
    if (c_na->mNumScalingKeys > 0 && c_na->mScalingKeys) {
        py_na->c_scale_times = (double*)malloc(c_na->mNumScalingKeys * sizeof(double));
        py_na->c_scale_values = (float*)malloc(c_na->mNumScalingKeys * 3 * sizeof(float));
        if (!py_na->c_scale_times || !py_na->c_scale_values) { PyErr_NoMemory(); goto fail_node_anim; }

        for (unsigned int k = 0; k < c_na->mNumScalingKeys; ++k) {
            py_na->c_scale_times[k] = c_na->mScalingKeys[k].mTime;
            py_na->c_scale_values[k*3 + 0] = c_na->mScalingKeys[k].mValue.x;
            py_na->c_scale_values[k*3 + 1] = c_na->mScalingKeys[k].mValue.y;
            py_na->c_scale_values[k*3 + 2] = c_na->mScalingKeys[k].mValue.z;
        }

        py_na->scaling_key_times = create_memoryview(
            py_na->c_scale_times,
            (Py_ssize_t)c_na->mNumScalingKeys * sizeof(double), "d", sizeof(double));
        if (!py_na->scaling_key_times) goto fail_node_anim;

        py_na->scaling_key_values = create_memoryview(
            py_na->c_scale_values,
            (Py_ssize_t)c_na->mNumScalingKeys * 3 * sizeof(float), "f", sizeof(float));
        if (!py_na->scaling_key_values) goto fail_node_anim;
    } else {
        Py_INCREF(Py_None); py_na->scaling_key_times = Py_None;
        Py_INCREF(Py_None); py_na->scaling_key_values = Py_None;
    }

    return (PyObject*)py_na; // Success

fail_node_anim:
    Py_DECREF(py_na); // NodeAnim_dealloc frees members and C arrays
    return NULL;
}

// Process animations from aiScene into a Python list of Animation objects.
// Returns a new reference to the list, or NULL on error.
static PyObject* process_animations(const struct aiScene *c_scene) {
    PyObject *py_anim_list = PyList_New(c_scene->mNumAnimations);
    if (!py_anim_list) return NULL;

    for (unsigned int i = 0; i < c_scene->mNumAnimations; ++i) {
        const struct aiAnimation *c_anim = c_scene->mAnimations[i];
        Animation *py_anim = (Animation *)AnimationType.tp_alloc(&AnimationType, 0);
        if (!py_anim) goto fail_anim_list;

        py_anim->name = PyUnicode_FromString(c_anim->mName.data);
        if (!py_anim->name) { Py_DECREF(py_anim); goto fail_anim_list; }

        py_anim->duration = c_anim->mDuration;
        py_anim->ticks_per_second = c_anim->mTicksPerSecond;
        py_anim->num_channels = c_anim->mNumChannels;

        py_anim->channels = PyList_New(c_anim->mNumChannels);
        if (!py_anim->channels) { Py_DECREF(py_anim); goto fail_anim_list; }

        for (unsigned int c = 0; c < c_anim->mNumChannels; ++c) {
            PyObject *py_channel = process_node_anim(c_anim->mChannels[c]);
            if (!py_channel) { Py_DECREF(py_anim); goto fail_anim_list; }

            if (PyList_SetItem(py_anim->channels, c, py_channel) < 0) {
                Py_DECREF(py_channel);
                Py_DECREF(py_anim);
                goto fail_anim_list;
            }
        }

        if (PyList_SetItem(py_anim_list, i, (PyObject*)py_anim) < 0) {
            Py_DECREF(py_anim);
            goto fail_anim_list;
        }
    }

    return py_anim_list; // Success

fail_anim_list:
    Py_DECREF(py_anim_list);
    return NULL;
}


// Process bones from aiMesh into a Python list of Bone objects.
// Returns a new reference to the list, or NULL on error.
static PyObject* process_bones(const struct aiMesh *c_mesh) {
    PyObject *py_bones_list = PyList_New(c_mesh->mNumBones);
    if (!py_bones_list) return NULL;

    for (unsigned int i = 0; i < c_mesh->mNumBones; ++i) {
        const struct aiBone *c_bone = c_mesh->mBones[i];
        Bone *py_bone = (Bone *)BoneType.tp_alloc(&BoneType, 0);
        if (!py_bone) goto fail_bone_list;

        // --- Name & Offset Matrix ---
        py_bone->name = PyUnicode_FromString(c_bone->mName.data);
        if (!py_bone->name) { Py_DECREF(py_bone); goto fail_bone_list; }

        py_bone->offset_matrix = tuple_from_matrix4x4(&c_bone->mOffsetMatrix);
        if (!py_bone->offset_matrix) { Py_DECREF(py_bone); goto fail_bone_list; }

        // --- Vertex Weights (parallel arrays: vertex ids + strengths) ---
        py_bone->num_weights = c_bone->mNumWeights;

        if (c_bone->mNumWeights > 0 && c_bone->mWeights) {
            py_bone->c_weight_ids = (unsigned int*)malloc(c_bone->mNumWeights * sizeof(unsigned int));
            py_bone->c_weights = (float*)malloc(c_bone->mNumWeights * sizeof(float));
            if (!py_bone->c_weight_ids || !py_bone->c_weights) {
                PyErr_NoMemory();
                Py_DECREF(py_bone);
                goto fail_bone_list;
            }

            for (unsigned int w = 0; w < c_bone->mNumWeights; ++w) {
                py_bone->c_weight_ids[w] = c_bone->mWeights[w].mVertexId;
                py_bone->c_weights[w] = (float)c_bone->mWeights[w].mWeight;
            }

            py_bone->weight_vertex_ids = create_memoryview(
                py_bone->c_weight_ids,
                (Py_ssize_t)c_bone->mNumWeights * sizeof(unsigned int), "I", sizeof(unsigned int));
            if (!py_bone->weight_vertex_ids) { Py_DECREF(py_bone); goto fail_bone_list; }

            py_bone->weights = create_memoryview(
                py_bone->c_weights,
                (Py_ssize_t)c_bone->mNumWeights * sizeof(float), "f", sizeof(float));
            if (!py_bone->weights) { Py_DECREF(py_bone); goto fail_bone_list; }
        } else {
            Py_INCREF(Py_None); py_bone->weight_vertex_ids = Py_None;
            Py_INCREF(Py_None); py_bone->weights = Py_None;
        }

        // --- Armature / Node names (only populated with Process_PopulateArmatureData) ---
        if (c_bone->mArmature) {
            py_bone->armature_name = PyUnicode_FromString(c_bone->mArmature->mName.data);
        } else {
            Py_INCREF(Py_None);
            py_bone->armature_name = Py_None;
        }
        if (!py_bone->armature_name) { Py_DECREF(py_bone); goto fail_bone_list; }

        if (c_bone->mNode) {
            py_bone->node_name = PyUnicode_FromString(c_bone->mNode->mName.data);
        } else {
            Py_INCREF(Py_None);
            py_bone->node_name = Py_None;
        }
        if (!py_bone->node_name) { Py_DECREF(py_bone); goto fail_bone_list; }

        if (PyList_SetItem(py_bones_list, i, (PyObject*)py_bone) < 0) {
            Py_DECREF(py_bone);
            goto fail_bone_list;
        }
    }

    return py_bones_list; // Success

fail_bone_list:
    Py_DECREF(py_bones_list);
    return NULL;
}


// Process meshes from aiScene into a Python list of Mesh objects.
// Returns a new reference to the list, or NULL on error.
static PyObject* process_meshes(const struct aiScene *c_scene) {
    unsigned int num_meshes = c_scene->mNumMeshes;
    PyObject *py_meshes_list = PyList_New(num_meshes);
    if (!py_meshes_list) return NULL;

    for (unsigned int i = 0; i < num_meshes; ++i) {
        struct aiMesh *c_mesh = c_scene->mMeshes[i];
        Mesh *py_mesh = (Mesh *)MeshType.tp_alloc(&MeshType, 0); // Create new Mesh object
        if (!py_mesh) goto fail_mesh_list;

        // --- Basic Info ---
        py_mesh->name = PyUnicode_FromString(c_mesh->mName.data);
        if (!py_mesh->name) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        py_mesh->num_vertices = c_mesh->mNumVertices;
        py_mesh->num_faces = c_mesh->mNumFaces;
        py_mesh->material_index = c_mesh->mMaterialIndex;

        size_t buffer_size; // Used for calculating allocation sizes
        Py_ssize_t itemsize; // For memoryview

        // --- Indices ---
        // Calculate total number of indices assuming triangulation (most common case)
        // If aiProcess_Triangulate is not used, this needs adjustment or checking mNumIndices per face.
        py_mesh->num_indices = 0;
        for (unsigned int f = 0; f < c_mesh->mNumFaces; ++f) {
            // Ensure faces are triangles if assuming flat index buffer
             if (c_mesh->mFaces[f].mNumIndices != 3) {
                 // Consider raising an error if triangulation wasn't enforced by flags
                 PyErr_SetString(PyExc_ValueError, "Mesh processing assumes triangulated faces (use aiProcess_Triangulate flag).");
                 Py_DECREF(py_mesh);
                 goto fail_mesh_list;
             }
            py_mesh->num_indices += c_mesh->mFaces[f].mNumIndices;
        }

        if (py_mesh->num_indices > 0) {
            buffer_size = py_mesh->num_indices * sizeof(unsigned int);
            itemsize = sizeof(unsigned int);
            py_mesh->c_indices = (unsigned int*)malloc(buffer_size);
            if (!py_mesh->c_indices) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }

            unsigned int idx_count = 0;
            for (unsigned int f = 0; f < c_mesh->mNumFaces; ++f) {
                memcpy(py_mesh->c_indices + idx_count, c_mesh->mFaces[f].mIndices, c_mesh->mFaces[f].mNumIndices * sizeof(unsigned int));
                idx_count += c_mesh->mFaces[f].mNumIndices;
            }
            // Format 'I' is standard unsigned int
            py_mesh->indices = create_memoryview(py_mesh->c_indices, buffer_size, "I", itemsize);
            if (!py_mesh->indices) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        } else {
            Py_INCREF(Py_None); py_mesh->indices = Py_None; // No indices
        }


        // --- Vertices ---
        if (c_mesh->mVertices) {
            buffer_size = py_mesh->num_vertices * 3 * sizeof(float);
            itemsize = sizeof(float);
            py_mesh->c_vertices = (float*)malloc(buffer_size);
            if (!py_mesh->c_vertices) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }
            memcpy(py_mesh->c_vertices, c_mesh->mVertices, buffer_size);
            // Format 'f' is standard float
            py_mesh->vertices = create_memoryview(py_mesh->c_vertices, buffer_size, "f", itemsize);
             if (!py_mesh->vertices) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        } else {
             Py_INCREF(Py_None); py_mesh->vertices = Py_None; // Should not happen for valid mesh
        }

        // --- Normals ---
        if (c_mesh->mNormals) {
            buffer_size = py_mesh->num_vertices * 3 * sizeof(float);
            itemsize = sizeof(float);
            py_mesh->c_normals = (float*)malloc(buffer_size);
            if (!py_mesh->c_normals) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }
            memcpy(py_mesh->c_normals, c_mesh->mNormals, buffer_size);
            py_mesh->normals = create_memoryview(py_mesh->c_normals, buffer_size, "f", itemsize);
             if (!py_mesh->normals) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        } else {
             Py_INCREF(Py_None); py_mesh->normals = Py_None;
        }

        // --- Tangents ---
        if (c_mesh->mTangents) {
            buffer_size = py_mesh->num_vertices * 3 * sizeof(float);
            itemsize = sizeof(float);
            py_mesh->c_tangents = (float*)malloc(buffer_size);
            if (!py_mesh->c_tangents) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }
            memcpy(py_mesh->c_tangents, c_mesh->mTangents, buffer_size);
            py_mesh->tangents = create_memoryview(py_mesh->c_tangents, buffer_size, "f", itemsize);
             if (!py_mesh->tangents) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        } else {
             Py_INCREF(Py_None); py_mesh->tangents = Py_None;
        }

        // --- Bitangents ---
        if (c_mesh->mBitangents) {
            buffer_size = py_mesh->num_vertices * 3 * sizeof(float);
            itemsize = sizeof(float);
            py_mesh->c_bitangents = (float*)malloc(buffer_size);
            if (!py_mesh->c_bitangents) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }
            memcpy(py_mesh->c_bitangents, c_mesh->mBitangents, buffer_size);
            py_mesh->bitangents = create_memoryview(py_mesh->c_bitangents, buffer_size, "f", itemsize);
             if (!py_mesh->bitangents) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        } else {
             Py_INCREF(Py_None); py_mesh->bitangents = Py_None;
        }

        // --- Colors ---
        py_mesh->num_color_sets = 0;
        for(unsigned int k=0; k < AI_MAX_NUMBER_OF_COLOR_SETS; ++k) {
            if(c_mesh->mColors[k] != NULL) py_mesh->num_color_sets++; else break;
        }

        if (py_mesh->num_color_sets > 0) {
            py_mesh->colors = PyList_New(py_mesh->num_color_sets);
            if (!py_mesh->colors) { Py_DECREF(py_mesh); goto fail_mesh_list; }
            py_mesh->c_colors = (float**)calloc(py_mesh->num_color_sets, sizeof(float*)); // Use calloc for NULL init
             if (!py_mesh->c_colors) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }

            for(unsigned int k=0; k < py_mesh->num_color_sets; ++k) {
                 // Colors are aiColor4D (r,g,b,a) -> 4 floats
                 buffer_size = py_mesh->num_vertices * 4 * sizeof(float);
                 itemsize = sizeof(float);
                 py_mesh->c_colors[k] = (float*)malloc(buffer_size);
                 if (!py_mesh->c_colors[k]) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }
                 // Need to copy component-wise from aiColor4D
                 for(unsigned int v=0; v < py_mesh->num_vertices; ++v) {
                     py_mesh->c_colors[k][v*4 + 0] = c_mesh->mColors[k][v].r;
                     py_mesh->c_colors[k][v*4 + 1] = c_mesh->mColors[k][v].g;
                     py_mesh->c_colors[k][v*4 + 2] = c_mesh->mColors[k][v].b;
                     py_mesh->c_colors[k][v*4 + 3] = c_mesh->mColors[k][v].a;
                 }
                 PyObject *memview = create_memoryview(py_mesh->c_colors[k], buffer_size, "f", itemsize);
                 if (!memview) { Py_DECREF(py_mesh); goto fail_mesh_list; }
                 PyList_SET_ITEM(py_mesh->colors, k, memview); // Steals ref
            }
        } else {
             Py_INCREF(Py_None); py_mesh->colors = Py_None;
        }

        // --- Texture Coordinates ---
        py_mesh->num_texcoord_sets = 0;
        for(unsigned int k=0; k < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++k) {
            if(c_mesh->mTextureCoords[k] != NULL) py_mesh->num_texcoord_sets++; else break;
        }

        if (py_mesh->num_texcoord_sets > 0) {
            py_mesh->texcoords = PyList_New(py_mesh->num_texcoord_sets);
             if (!py_mesh->texcoords) { Py_DECREF(py_mesh); goto fail_mesh_list; }
            py_mesh->num_uv_components = PyList_New(py_mesh->num_texcoord_sets);
             if (!py_mesh->num_uv_components) { Py_DECREF(py_mesh); goto fail_mesh_list; }

            py_mesh->c_texcoords = (float**)calloc(py_mesh->num_texcoord_sets, sizeof(float*));
             if (!py_mesh->c_texcoords) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }
            py_mesh->c_num_uv_components = (unsigned int*)malloc(py_mesh->num_texcoord_sets * sizeof(unsigned int));
             if (!py_mesh->c_num_uv_components) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }


            for(unsigned int k=0; k < py_mesh->num_texcoord_sets; ++k) {
                 unsigned int ncomp = c_mesh->mNumUVComponents[k]; // 1, 2 or 3
                 py_mesh->c_num_uv_components[k] = ncomp;
                 PyObject *comp_obj = PyLong_FromUnsignedLong(ncomp);
                 if (!comp_obj) { Py_DECREF(py_mesh); goto fail_mesh_list; }
                 PyList_SET_ITEM(py_mesh->num_uv_components, k, comp_obj); // Steals ref

                 buffer_size = py_mesh->num_vertices * ncomp * sizeof(float);
                 itemsize = sizeof(float);
                 py_mesh->c_texcoords[k] = (float*)malloc(buffer_size);
                 if (!py_mesh->c_texcoords[k]) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }

                 // Copy from aiVector3D, taking only ncomp components
                 for(unsigned int v=0; v < py_mesh->num_vertices; ++v) {
                    memcpy(py_mesh->c_texcoords[k] + v * ncomp, &(c_mesh->mTextureCoords[k][v].x), ncomp * sizeof(float));
                 }

                 PyObject *memview = create_memoryview(py_mesh->c_texcoords[k], buffer_size, "f", itemsize);
                 if (!memview) { Py_DECREF(py_mesh); goto fail_mesh_list; }
                 PyList_SET_ITEM(py_mesh->texcoords, k, memview); // Steals ref
            }
        } else {
             Py_INCREF(Py_None); py_mesh->texcoords = Py_None;
             py_mesh->num_uv_components = PyList_New(0); // Empty list if no texcoords
             if (!py_mesh->num_uv_components) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        }


        // --- Bones ---
        py_mesh->num_bones = c_mesh->mNumBones;
        py_mesh->bones = process_bones(c_mesh);
        if (!py_mesh->bones) { Py_DECREF(py_mesh); goto fail_mesh_list; }


        // --- Add Mesh to List ---
        // PyList_SetItem steals the reference, no DECREF needed on success
        if (PyList_SetItem(py_meshes_list, i, (PyObject*)py_mesh) < 0) {
             Py_DECREF(py_mesh); // Decref if SetItem failed
             goto fail_mesh_list;
        }
    }

    return py_meshes_list; // Success

fail_mesh_list:
    Py_DECREF(py_meshes_list); // Clean up the main list on error
    return NULL;
}


// Recursively process Assimp nodes and build the Python Node hierarchy
// Returns a NEW reference to the created Node or NULL on error
static PyObject* process_node_recursive(struct aiNode* c_node) {
    if (!c_node) { // Should not happen for root, but check anyway
        PyErr_SetString(PyExc_ValueError, "Encountered NULL aiNode during processing");
        return NULL;
    }

    // 1. Create Python Node object
    Node* py_node = (Node*)NodeType.tp_alloc(&NodeType, 0);
    if (!py_node) return NULL; // Allocation failed (MemoryError likely set)

    // 2. Populate Simple Members
    py_node->name = PyUnicode_FromString(c_node->mName.data);
    if (!py_node->name) goto node_proc_error;

    py_node->transformation = tuple_from_matrix4x4(&c_node->mTransformation);
    if (!py_node->transformation) goto node_proc_error;

    // Parent Name (or None for root)
    if (c_node->mParent) {
        py_node->parent_name = PyUnicode_FromString(c_node->mParent->mName.data);
        if (!py_node->parent_name) goto node_proc_error;
    } else {
        Py_INCREF(Py_None);
        py_node->parent_name = Py_None;
    }

    // Mesh Indices
    py_node->num_meshes = c_node->mNumMeshes;
    py_node->mesh_indices = tuple_from_uint_array(c_node->mMeshes, c_node->mNumMeshes);
    if (!py_node->mesh_indices) goto node_proc_error;

    // 3. Populate Children (Recursive Step)
    py_node->num_children = c_node->mNumChildren;
    py_node->children = PyList_New(py_node->num_children);
    if (!py_node->children) goto node_proc_error;

    for (unsigned int i = 0; i < py_node->num_children; ++i) {
        // Recursively process child
        PyObject* py_child = process_node_recursive(c_node->mChildren[i]);
        if (!py_child) {
            // Error occurred deeper in recursion
            goto node_proc_error; // Children list and self will be cleaned up
        }
        // PyList_SET_ITEM steals the reference to py_child
        if (PyList_SetItem(py_node->children, i, py_child) < 0) {
            Py_DECREF(py_child); // Decref if SET_ITEM failed
            goto node_proc_error;
        }
    }

    // Success
    return (PyObject*)py_node;

// Error handling: clean up partially created node
node_proc_error:
    Py_DECREF(py_node); // This calls Node_dealloc which Py_CLEARs members
    return NULL;
}

// --- Module Methods ---

PyDoc_STRVAR(import_file_doc,
"import_file(filename: str, flags: int) -> Scene\n"
"--\n\n"
"Imports the 3D model from the given filename.\n\n"
"Args:\n"
"    filename: Path to the model file.\n"
"    flags: Post-processing flags (e.g., Process_Triangulate | Process_GenNormals).\n"
"           Process_Triangulate is highly recommended for predictable index buffers.\n"
"           Process_JoinIdenticalVertices is useful for reducing vertex count.\n"
"           Process_CalcTangentSpace is needed if you require tangents/bitangents.\n"
"           Process_GenSmoothNormals or Process_GenNormals if normals are missing.\n"
"           Process_FlipUVs can be important depending on texture conventions.\n\n"
"Returns:\n"
"    A Scene object containing the loaded data.\n\n"
"Raises:\n"
"    FileNotFoundError: If the file does not exist.\n"
"    RuntimeError: If Assimp fails to load the file.\n"
"    MemoryError: If memory allocation fails.\n"
"    ValueError: If arguments are invalid or mesh data is inconsistent (e.g., non-triangulated when expected).");

static PyObject* py_import_file(PyObject *self, PyObject *args) {
    const char* filename = NULL;
    unsigned int flags = 0;
    const struct aiScene *c_scene = NULL;
    Scene *py_scene = NULL; // The Python Scene object we will return

    if (!PyArg_ParseTuple(args, "sI:import_file", &filename, &flags)) {
        // Error already set by PyArg_ParseTuple
        return NULL;
    }

    // Basic check if file exists before calling Assimp
    // Use Python's built-in os.path.exists for better cross-platform compatibility?
    // For C extension, fopen is reasonable.
    FILE *f = fopen(filename, "rb"); // Use "rb" for binary check
    if (!f) {
        // Map C's file not found to Python's FileNotFoundError
        // Note: Python 3.3+ needed for FileNotFoundError. Use IOError for older.
        PyErr_SetString(PyExc_FileNotFoundError, filename);
        return NULL;
    }
    fclose(f);

    // Import the file using Assimp
    c_scene = aiImportFile(filename, flags);

    // Check for Assimp loading errors
    if (!c_scene || !c_scene->mRootNode || (c_scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        PyErr_Format(PyExc_RuntimeError, "Assimp error loading '%s': %s", filename, aiGetErrorString());
        aiReleaseImport(c_scene); // Release even if partially loaded
        return NULL;
    }

    // Create the Python Scene object
    py_scene = (Scene *)SceneType.tp_alloc(&SceneType, 0);
    if (!py_scene) {
        aiReleaseImport(c_scene);
        return NULL; // Error already set (likely MemoryError)
    }

    py_scene->num_meshes = c_scene->mNumMeshes;
    py_scene->num_materials = c_scene->mNumMaterials;
    py_scene->num_animations = c_scene->mNumAnimations;

    // Process Meshes
    py_scene->meshes = process_meshes(c_scene);
    if (!py_scene->meshes) {
        goto fail; // Error occurred during mesh processing
    }

    // Process Materials
    py_scene->materials = process_materials(c_scene);
    if (!py_scene->materials) {
        goto fail; // Error occurred during material processing
    }

    // Process Animations
    py_scene->animations = process_animations(c_scene);
    if (!py_scene->animations) {
        goto fail; // Error occurred during animation processing
    }

    // **** Process Node Hierarchy ****
    if (c_scene->mRootNode) {
        py_scene->root_node = process_node_recursive(c_scene->mRootNode);
        if (!py_scene->root_node) {
            // Error occurred during node processing
            goto fail;
        }
    } else {
        // Should not happen if aiImportFile succeeded, but handle defensively
        Py_INCREF(Py_None);
        py_scene->root_node = Py_None;
    }    

    // Success! Release the C scene and return the Python scene
    aiReleaseImport(c_scene);
    return (PyObject *)py_scene;

fail:
    // Cleanup on error
    aiReleaseImport(c_scene);
    Py_XDECREF(py_scene); // Use XDECREF as py_scene might be NULL if allocation failed
    return NULL;
}


// --- Module Definition ---

static PyMethodDef assimp_py_methods[] = {
    {"import_file", py_import_file, METH_VARARGS, import_file_doc},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

// Function to add integer constants to the module
static int add_int_constant(PyObject *module, const char *name, long value) {
    if (PyModule_AddIntConstant(module, name, value) < 0) {
        fprintf(stderr, "Failed to add constant: %s\n", name);
        return -1;
    }
    return 0;
}


static PyModuleDef assimp_py_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "assimp_py",
    .m_doc = "Python C bindings for the Assimp library",
    .m_size = -1, // No global state for this module
    .m_methods = assimp_py_methods,
};

PyMODINIT_FUNC PyInit_assimp_py(void) {
    PyObject *module = NULL;

    // Initialize Types
    if (PyType_Ready(&MeshType) < 0) return NULL;
    if (PyType_Ready(&SceneType) < 0) return NULL;
    if (PyType_Ready(&NodeType) < 0) return NULL;
    if (PyType_Ready(&BoneType) < 0) return NULL;
    if (PyType_Ready(&AnimationType) < 0) return NULL;
    if (PyType_Ready(&NodeAnimType) < 0) return NULL;

    // Create Module
    module = PyModule_Create(&assimp_py_module);
    if (!module) {
        Py_DECREF(&MeshType); // Need cleanup if module creation fails
        Py_DECREF(&SceneType);
        Py_DECREF(&NodeType);
        return NULL;
    }

    // Add Types to Module
    Py_INCREF(&MeshType); // Module takes ownership
    if (PyModule_AddObject(module, "Mesh", (PyObject *)&MeshType) < 0) {
        Py_DECREF(&MeshType);
        Py_DECREF(module);
        return NULL;
    }

    Py_INCREF(&SceneType); // Module takes ownership
    if (PyModule_AddObject(module, "Scene", (PyObject *)&SceneType) < 0) {
        Py_DECREF(&MeshType);
        Py_DECREF(&SceneType);
        Py_DECREF(module);
        return NULL;
    }

    Py_INCREF(&NodeType); // **** ADDED ****
    if (PyModule_AddObject(module, "Node", (PyObject *)&NodeType) < 0) {
        Py_DECREF(&MeshType);
        Py_DECREF(&SceneType);
        Py_DECREF(&NodeType);
        Py_DECREF(module);
        return NULL;
    }

    Py_INCREF(&BoneType);
    if (PyModule_AddObject(module, "Bone", (PyObject *)&BoneType) < 0) {
        Py_DECREF(&MeshType);
        Py_DECREF(&SceneType);
        Py_DECREF(&NodeType);
        Py_DECREF(&BoneType);
        Py_DECREF(module);
        return NULL;
    }

    Py_INCREF(&AnimationType);
    if (PyModule_AddObject(module, "Animation", (PyObject *)&AnimationType) < 0) {
        Py_DECREF(&MeshType);
        Py_DECREF(&SceneType);
        Py_DECREF(&NodeType);
        Py_DECREF(&BoneType);
        Py_DECREF(&AnimationType);
        Py_DECREF(module);
        return NULL;
    }

    Py_INCREF(&NodeAnimType);
    if (PyModule_AddObject(module, "NodeAnim", (PyObject *)&NodeAnimType) < 0) {
        Py_DECREF(&MeshType);
        Py_DECREF(&SceneType);
        Py_DECREF(&NodeType);
        Py_DECREF(&BoneType);
        Py_DECREF(&AnimationType);
        Py_DECREF(&NodeAnimType);
        Py_DECREF(module);
        return NULL;
    }

    // Add Constants (Post-processing flags) - Abbreviated list for example
    int error = 0;
    error |= add_int_constant(module, "Process_CalcTangentSpace", aiProcess_CalcTangentSpace);
    error |= add_int_constant(module, "Process_JoinIdenticalVertices", aiProcess_JoinIdenticalVertices);
    error |= add_int_constant(module, "Process_MakeLeftHanded", aiProcess_MakeLeftHanded);
    error |= add_int_constant(module, "Process_Triangulate", aiProcess_Triangulate);
    error |= add_int_constant(module, "Process_RemoveComponent", aiProcess_RemoveComponent);
    error |= add_int_constant(module, "Process_GenNormals", aiProcess_GenNormals);
    error |= add_int_constant(module, "Process_GenSmoothNormals", aiProcess_GenSmoothNormals);
    error |= add_int_constant(module, "Process_SplitLargeMeshes", aiProcess_SplitLargeMeshes);
    error |= add_int_constant(module, "Process_PreTransformVertices", aiProcess_PreTransformVertices);
    error |= add_int_constant(module, "Process_LimitBoneWeights", aiProcess_LimitBoneWeights);
    error |= add_int_constant(module, "Process_ValidateDataStructure", aiProcess_ValidateDataStructure);
    error |= add_int_constant(module, "Process_ImproveCacheLocality", aiProcess_ImproveCacheLocality);
    error |= add_int_constant(module, "Process_RemoveRedundantMaterials", aiProcess_RemoveRedundantMaterials);
    error |= add_int_constant(module, "Process_FixInfacingNormals", aiProcess_FixInfacingNormals);
    error |= add_int_constant(module, "Process_SortByPType", aiProcess_SortByPType);
    error |= add_int_constant(module, "Process_FindDegenerates", aiProcess_FindDegenerates);
    error |= add_int_constant(module, "Process_FindInvalidData", aiProcess_FindInvalidData);
    error |= add_int_constant(module, "Process_GenUVCoords", aiProcess_GenUVCoords);
    error |= add_int_constant(module, "Process_TransformUVCoords", aiProcess_TransformUVCoords);
    error |= add_int_constant(module, "Process_FindInstances", aiProcess_FindInstances);
    error |= add_int_constant(module, "Process_OptimizeMeshes", aiProcess_OptimizeMeshes);
    error |= add_int_constant(module, "Process_OptimizeGraph", aiProcess_OptimizeGraph);
    error |= add_int_constant(module, "Process_FlipUVs", aiProcess_FlipUVs);
    error |= add_int_constant(module, "Process_FlipWindingOrder", aiProcess_FlipWindingOrder);
    error |= add_int_constant(module, "Process_SplitByBoneCount", aiProcess_SplitByBoneCount);
    error |= add_int_constant(module, "Process_Debone", aiProcess_Debone);
    error |= add_int_constant(module, "Process_GlobalScale", aiProcess_GlobalScale);
    error |= add_int_constant(module, "Process_PopulateArmatureData", aiProcess_PopulateArmatureData);
    // Add Animation Behaviour constants
    error |= add_int_constant(module, "AnimBehaviour_DEFAULT", aiAnimBehaviour_DEFAULT);
    error |= add_int_constant(module, "AnimBehaviour_CONSTANT", aiAnimBehaviour_CONSTANT);
    error |= add_int_constant(module, "AnimBehaviour_LINEAR", aiAnimBehaviour_LINEAR);
    error |= add_int_constant(module, "AnimBehaviour_REPEAT", aiAnimBehaviour_REPEAT);
    // Add Texture Type constants
    error |= add_int_constant(module, "TextureType_NONE", aiTextureType_NONE);
    error |= add_int_constant(module, "TextureType_DIFFUSE", aiTextureType_DIFFUSE);
    error |= add_int_constant(module, "TextureType_SPECULAR", aiTextureType_SPECULAR);
    error |= add_int_constant(module, "TextureType_AMBIENT", aiTextureType_AMBIENT);
    error |= add_int_constant(module, "TextureType_EMISSIVE", aiTextureType_EMISSIVE);
    error |= add_int_constant(module, "TextureType_HEIGHT", aiTextureType_HEIGHT);
    error |= add_int_constant(module, "TextureType_NORMALS", aiTextureType_NORMALS);
    error |= add_int_constant(module, "TextureType_SHININESS", aiTextureType_SHININESS);
    error |= add_int_constant(module, "TextureType_OPACITY", aiTextureType_OPACITY);
    error |= add_int_constant(module, "TextureType_DISPLACEMENT", aiTextureType_DISPLACEMENT);
    error |= add_int_constant(module, "TextureType_LIGHTMAP", aiTextureType_LIGHTMAP);
    error |= add_int_constant(module, "TextureType_REFLECTION", aiTextureType_REFLECTION);
    error |= add_int_constant(module, "TextureType_UNKNOWN", aiTextureType_UNKNOWN);


    if (error < 0) {
        // Cleanup if adding constants failed
        Py_DECREF(&MeshType);
        Py_DECREF(&SceneType);
        Py_DECREF(module);
        PyErr_SetString(PyExc_RuntimeError, "Failed to initialize assimp_py constants");
        return NULL;
    }


    return module;
}