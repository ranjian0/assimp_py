"""Memory leak check for assimp_py.

Repeatedly imports each model and watches process RSS. A case is flagged
as leaking when memory keeps growing with the iteration count after the
first few warmup iterations.

Usage:
    python scripts/memprof.py                # leak harness (no dependencies)
    python scripts/memprof.py --iterations 100
    python scripts/memprof.py --lines        # line-by-line profile via
                                             # memory_profiler (pip install
                                             # memory-profiler)
"""
import argparse
import gc
import sys
from pathlib import Path

import assimp_py

MODELS_DIR = Path(__file__).parent.parent.joinpath("tests/models")

SKELETAL_FLAGS = (
    assimp_py.Process_Triangulate
    | assimp_py.Process_LimitBoneWeights
    | assimp_py.Process_PopulateArmatureData
)


def rss_mb():
    """Resident set size in MB (psutil if available, /proc fallback)."""
    try:
        import psutil
        return psutil.Process().memory_info().rss / (1024 * 1024)
    except ImportError:
        pass
    try:
        with open("/proc/self/statm") as f:
            pages = int(f.read().split()[1])
        return pages * 4096 / (1024 * 1024)
    except OSError:
        print("error: no RSS source available (install psutil or use Linux)")
        sys.exit(1)


# --- Cases: each exercises different code paths in the extension ---

def load_cyborg():
    model = MODELS_DIR.joinpath("cyborg/cyborg.obj")
    scn = assimp_py.import_file(str(model), assimp_py.Process_GenNormals | assimp_py.Process_CalcTangentSpace)
    del scn


def load_planet():
    model = MODELS_DIR.joinpath("planet/planet.obj")
    scn = assimp_py.import_file(str(model), assimp_py.Process_GenNormals | assimp_py.Process_CalcTangentSpace)
    del scn


def load_fox_plain():
    model = MODELS_DIR.joinpath("fox/Fox.glb")
    scn = assimp_py.import_file(str(model), assimp_py.Process_Triangulate)
    del scn


def load_fox_skeletal():
    model = MODELS_DIR.joinpath("fox/Fox.glb")
    scn = assimp_py.import_file(str(model), SKELETAL_FLAGS)
    del scn


# Retain memoryviews (and plain copies) past the lifetime of the Scene to
# exercise the copy-ownership design: views must stay valid after dealloc.
_retained = []


def load_fox_retain_views():
    global _retained
    model = MODELS_DIR.joinpath("fox/Fox.glb")
    scn = assimp_py.import_file(str(model), SKELETAL_FLAGS)

    keep = []
    for mesh in scn.meshes:
        for bone in mesh.bones:
            keep.append(bone.weights)
            keep.append(bone.weight_vertex_ids)
            keep.append(bytes(bone.weights))
    for anim in scn.animations:
        for channel in anim.channels:
            keep.append(channel.position_key_times)
            keep.append(channel.rotation_key_values)

    # touch every retained view so the memory is actually read
    for view in keep:
        if isinstance(view, memoryview):
            view.cast("B")[0]

    _retained = keep[:32]  # keep some alive for the next iteration
    del scn
    gc.collect()


CASES = [
    ("cyborg (meshes/materials/normals)", load_cyborg),
    ("planet (large static mesh)", load_planet),
    ("fox plain (bones, no armature flag)", load_fox_plain),
    ("fox skeletal (bones + animations)", load_fox_skeletal),
    ("fox skeletal (retained memoryviews)", load_fox_retain_views),
]


def _linear_slope(samples):
    """Least-squares slope (MB per iteration)."""
    n = len(samples)
    mean_x = (n - 1) / 2
    mean_y = sum(samples) / n
    denom = sum((x - mean_x) ** 2 for x in range(n))
    return sum((x - mean_x) * (y - mean_y) for x, y in enumerate(samples)) / max(denom, 1)


def run_leak_check(iterations, warmup=5, threshold_mb=0.01):
    failures = []
    print(f"RSS leak check ({iterations} iterations per case)\n")

    # Global warmup: fill python allocator arenas / freelists so the first
    # measured case does not look like a leak due to one-time growth
    for _, func in CASES:

        func()

    for name, func in CASES:
        samples = []
        for i in range(iterations):
            func()
            samples.append(rss_mb())

        slope_mb_per_iter = _linear_slope(samples[warmup:])

        # Allocator arena growth can produce one-time steps that look like a
        # slope. Confirm by measuring a fresh window: a real leak keeps
        # growing, a step does not.
        if slope_mb_per_iter > threshold_mb:
            confirm = []
            for i in range(15):
                func()
                confirm.append(rss_mb())
            confirm_slope = _linear_slope(confirm)
            if confirm_slope > threshold_mb:
                slope_mb_per_iter = confirm_slope
            else:
                slope_mb_per_iter = 0.0  # step noise, not a leak

        warm = samples[:warmup]
        tail = samples[-5:]
        growth = (sum(tail) / len(tail)) - (sum(warm) / len(warm))

        status = "OK" if slope_mb_per_iter <= threshold_mb else "LEAKING"
        if status == "LEAKING":
            failures.append(name)

        print(f"  {name}")
        print(
            f"    first={samples[0]:8.3f} MB  last={samples[-1]:8.3f} MB  "
            f"growth(after warmup)={growth:+8.3f} MB  slope={slope_mb_per_iter:+.5f} MB/iter  [{status}]"
        )

    print()
    if failures:
        print(f"FAIL: leaking cases: {', '.join(failures)}")
        sys.exit(1)
    print("OK: no leaks detected")


def run_line_profile(iterations):
    from memory_profiler import profile

    for name, func in CASES:
        print(f"\n=== {name} ===")
        profiled = profile(func)
        for _ in range(iterations):
            profiled()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iterations", type=int, default=25, help="iterations per case")
    parser.add_argument("--lines", action="store_true", help="line-by-line profile via memory_profiler")
    args = parser.parse_args()

    if args.lines:
        run_line_profile(args.iterations)
    else:
        run_leak_check(args.iterations)
