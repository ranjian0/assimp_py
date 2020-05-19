import math
import assimp


def float_eq(a, b):
    return math.isclose(a, b, rel_tol=1e-6)


def test_Funcs():
    scene = assimp.ImportFile("models/planet.obj", 1)
    assert(scene)

    release = assimp.ReleaseImporter()
    assert(release is None)

    error = assimp.GetErrorString()
    assert(error == "")


def test_String():
    s = assimp.String("Hello")
    assert s.data == "Hello"


def test_Vec2D():
    v = assimp.Vec2D(1, 0)
    x, y = v.xy
    assert(float_eq(x, 1.0))
    assert(float_eq(y, 0.0))


def test_Vec3D():
    v = assimp.Vec3D(1, 2, 3)
    x, y, z = v.xyz
    assert(float_eq(x, 1.0))
    assert(float_eq(y, 2.0))
    assert(float_eq(z, 3.0))


def test_Quaternion():
    q = assimp.Quaternion(4, 1, 2, 3)
    w, x, y, z = q.wxyz
    assert(float_eq(w, 4.0))
    assert(float_eq(x, 1.0))
    assert(float_eq(y, 2.0))
    assert(float_eq(z, 3.0))


def test_Color3d():
    c = assimp.Color3D(.2, .3, .3)
    r, g, b = c.rgb
    assert(float_eq(r, .2))
    assert(float_eq(g, .3))
    assert(float_eq(b, .3))


def test_Color4d():
    c = assimp.Color4D(.2, .3, .3, 1.0)
    r, g, b, a = c.rgba
    assert(float_eq(r, .2))
    assert(float_eq(g, .3))
    assert(float_eq(b, .3))
    assert(float_eq(a, 1.))
