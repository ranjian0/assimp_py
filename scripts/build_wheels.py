#!/usr/bin/env python3
"""Cross-build assimp_py wheels with zig, no emulation for compilation.

One invocation builds all configured python versions for one target lane,
sharing a single assimp build, then repairs + tests the wheels inside the
matching manylinux/musllinux container (tests run emulated; compilation
does not).

Example:
    python scripts/build_wheels.py --arch s390x --libc gnu --glibc 2.28 \
        --pythons 312,313,314 --repo . --out dist

Requires: pip install ziglang==0.16.0 wheel auditwheel; docker.
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import sysconfig
import tarfile
import urllib.request
import zipfile

PY_VERSIONS_DEFAULT = "312,313,314"

# arch -> (gnu triple, musl triple, docker platform)
ARCHES = {
    "s390x": ("s390x-linux-gnu", "s390x-linux-musl", "linux/s390x"),
    "ppc64le": ("powerpc64le-linux-gnu", "powerpc64le-linux-musl", "linux/ppc64le"),
    "armv7l": ("arm-linux-gnueabihf", "arm-linux-musleabihf", "linux/arm/v7"),
    "aarch64": ("aarch64-linux-gnu", "aarch64-linux-musl", "linux/arm64"),
    "x86_64": ("x86_64-linux-gnu", "x86_64-linux-musl", "linux/amd64"),
    "i686": ("x86-linux-gnu", "x86-linux-musl", "linux/386"),
}

ASSIMP_URL = "https://github.com/assimp/assimp/archive/refs/tags/v6.0.5.tar.gz"


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd), file=sys.stderr)
    return subprocess.run([str(c) for c in cmd], check=True, **kw)


def sh(cmd: str, **kw):
    print("+", cmd, file=sys.stderr)
    return subprocess.run(cmd, shell=True, check=True, **kw)


def extract_headers(work: pathlib.Path, image: str, pythons: list[str]):
    """Copy target python include dirs + sysconfig EXT_SUFFIX data out of the
    image (docker create/cp runs without emulation)."""
    name = "hdr-extract"
    sh(f"docker rm -f {name} >/dev/null 2>&1 || true")
    sh(f"docker create --name {name} --platform {image_platform} {image} sleep 0")
    try:
        headers = work / "headers"
        headers.mkdir(parents=True, exist_ok=True)
        ext_suffixes = {}
        for cp in pythons:
            tag = f"cp{cp}-cp{cp}"
            dst = headers / tag
            if not dst.exists():
                sh(f"docker cp {name}:/opt/python/{tag}/include {dst}")
            # find EXT_SUFFIX from the interpreter's own extension modules
            out = subprocess.run(
                f"docker cp {name}:/opt/python/{tag}/lib - ", shell=True,
                check=True, capture_output=True).stdout
            # stream is a tar; list member names only
            names = tarfile_list(out)
            so_names = [n for n in names if n.endswith(".so") and "/lib-dynload/" in n]
            if not so_names:
                raise RuntimeError(f"no lib-dynload .so found for {tag}")
            m = re.search(r"\.(cpython-[^/]+\.so)$", so_names[0].split("/")[-1])
            if not m:
                raise RuntimeError(f"cannot derive EXT_SUFFIX from {so_names[0]}")
            ext_suffixes[cp] = "." + m.group(1)
        return ext_suffixes
    finally:
        sh(f"docker rm -f {name} >/dev/null")


def tarfile_list(data: bytes) -> list[str]:
    import io
    import tarfile
    with tarfile.open(fileobj=io.BytesIO(data)) as tf:
        return tf.getnames()


def fetch_assimp(work: pathlib.Path) -> pathlib.Path:
    """Fetch, verify and extract the pinned assimp source for offline cmake use."""
    src = work / "assimp-src"
    if (src / "CMakeLists.txt").exists():
        return src
    tar = work / "assimp-6.0.5.tar.gz"
    if not tar.exists():
        print("downloading assimp tarball...", file=sys.stderr)
        urllib.request.urlretrieve(ASSIMP_URL, tar)
    digest = hashlib.sha256(tar.read_bytes()).hexdigest()
    expected = "edf3749559c2b7d1f758ffb66fc5bec62186221e623b7f2e8969f17ee46ecb6f"
    if digest != expected:
        raise RuntimeError("assimp tarball hash mismatch")
    import tarfile
    with tarfile.open(tar) as tf:
        tf.extractall(work, filter="data")
    extracted = work / "assimp-6.0.5"
    extracted.rename(src)
    return src


def make_wrappers(work: pathlib.Path, zig: str, triple: str):
    d = work / "wrappers"
    d.mkdir(parents=True, exist_ok=True)
    scripts = {
        "cc": f'#!/bin/sh\nexec {zig} cc -target {triple} "$@"\n',
        "cxx": f'#!/bin/sh\nexec {zig} c++ -target {triple} "$@"\n',
        "ar": f'#!/bin/sh\nexec {zig} ar "$@"\n',
        "ranlib": f'#!/bin/sh\nexec {zig} ar s "$@"\n',
    }
    for name, body in scripts.items():
        p = d / f"zig-{name}"
        p.write_text(body)
        p.chmod(0o755)
    return d


def build(repo: pathlib.Path, work: pathlib.Path, wrappers: pathlib.Path,
          ext_suffixes: dict, pythons: list[str], assimp_tar: pathlib.Path):
    build_dir = work / "build"
    # docker cp copies the include dir AS the destination, so headers live at
    # headers/cpXYZ-cpXYZ/pythonX.Y (docker cp of .../include -> dst)
    targets = ";".join(
        f"assimp_py{ext_suffixes[cp]}={work}/headers/cp{cp}-cp{cp}/python{cp[0]}.{cp[1:]}"
        for cp in pythons
    )
    run([
        "cmake", "-S", repo, "-B", build_dir, "-G", "Unix Makefiles",
        "-DCMAKE_SYSTEM_NAME=Linux",
        f"-DCMAKE_SYSTEM_PROCESSOR={arch}",
        f"-DCMAKE_C_COMPILER={wrappers}/zig-cc",
        f"-DCMAKE_CXX_COMPILER={wrappers}/zig-cxx",
        f"-DCMAKE_AR={wrappers}/zig-ar",
        f"-DCMAKE_RANLIB={wrappers}/zig-ranlib",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DASSIMP_PY_TARGETS={targets}",
        f"-DFETCHCONTENT_SOURCE_DIR_ASSIMP={assimp_tar}",
    ])
    nproc = os.cpu_count() or 4
    run(["cmake", "--build", build_dir, "-j", str(nproc)])
    return build_dir


def metadata_text(repo: pathlib.Path) -> str:
    import tomllib
    py = tomllib.loads((repo / "pyproject.toml").read_text())["project"]
    lines = ["Metadata-Version: 2.4", "Name: assimp_py", f"Version: {py['version']}",
             f"Summary: {py['description']}",
             f"Author-email: {py['authors'][0]['name']} <{py['authors'][0]['email']}>"]
    lines += [f"Project-URL: {n}, {u}" for n, u in py["urls"].items()]
    lines.append(f"Keywords: {','.join(py['keywords'])}")
    lines += [f"Classifier: {c}" for c in py["classifiers"]]
    lines.append(f"Requires-Python: {py['requires-python']}")
    lines += ["Description-Content-Type: text/markdown",
              "License-File: LICENSE", "License-File: licenses/assimp-LICENSE"]
    for extra, deps in py.get("optional-dependencies", {}).items():
        lines.append(f"Provides-Extra: {extra}")
        lines += [f'Requires-Dist: {d}; extra == "{extra}"' for d in deps]
    lines.append("Dynamic: license-file")
    return "\n".join(lines) + "\n\n" + (repo / "README.md").read_text()


def pack_wheels(repo: pathlib.Path, build_dir: pathlib.Path, out: pathlib.Path,
                ext_suffixes: dict, pythons: list[str]):
    import tomllib
    version = tomllib.loads((repo / "pyproject.toml").read_text())["project"]["version"]
    metadata = metadata_text(repo)
    linux_tag = {"gnu": "linux", "musl": "linux"}[libc]  # placeholder, auditwheel retags
    arch_tag = {"s390x": "s390x", "ppc64le": "ppc64le", "armv7l": "armv7l",
                "aarch64": "aarch64", "x86_64": "x86_64", "i686": "i686"}[arch]
    out.mkdir(parents=True, exist_ok=True)
    for cp in pythons:
        ext = f"assimp_py{ext_suffixes[cp]}"
        stage = out / f"stage-{cp}"
        shutil.rmtree(stage, ignore_errors=True)
        pkg = stage / "assimp_py"
        pkg.mkdir(parents=True)
        di = stage / f"assimp_py-{version}.dist-info"
        (di / "licenses" / "licenses").mkdir(parents=True)
        shutil.copy(repo / "src/assimp_py/__init__.py", pkg / "__init__.py")
        shutil.copy(repo / "src/assimp_py/assimp_py.pyi", pkg / "assimp_py.pyi")
        shutil.copy(build_dir / ext, pkg / ext)
        (di / "METADATA").write_text(metadata)
        (di / "WHEEL").write_text(
            f"Wheel-Version: 1.0\nGenerator: assimp_py zig build\n"
            f"Root-Is-Purelib: false\nTag: cp{cp}-cp{cp}-{linux_tag}_{arch_tag}\n")
        (di / "top_level.txt").write_text("assimp_py\n")
        shutil.copy(repo / "LICENSE", di / "licenses" / "LICENSE")
        shutil.copy(repo / "licenses/assimp-LICENSE", di / "licenses/licenses/assimp-LICENSE")
        run([sys.executable, "-m", "wheel", "pack", stage, "-d", out])
        shutil.rmtree(stage)


TEST_DEPS = ["pytest", "pluggy", "iniconfig", "packaging", "pygments"]


def download_test_deps(work: pathlib.Path, pythons: list[str]):
    """Natively pre-download pure-python test deps (they are arch-independent;
    downloaded on the host so the emulated container never touches the net)."""
    deps = work / "testdeps"
    deps.mkdir(parents=True, exist_ok=True)
    for cp in pythons:
        run([sys.executable, "-m", "pip", "download", "-q", "--no-deps",
             "--only-binary", ":all:", "--python-version", f"{cp[0]}.{cp[1:]}",
             "--implementation", "cp", "-d", deps, *TEST_DEPS])


def container_verify(work: pathlib.Path, repo: pathlib.Path, out: pathlib.Path,
                     image: str, plat: str, ext_suffixes: dict, pythons: list[str]):
    """auditwheel repair + install + pytest inside the target container.
    Compilation stays native; only ELF inspection and tests run emulated."""
    repaired = out / "repaired"
    repaired.mkdir(exist_ok=True)
    cps = " ".join(pythons)
    inner = f"""
set -e
cd /io
auditwheel repair --strip --plat {plat} -w /io/repaired $(ls /io/*.whl)
for cp in {cps}; do
  /opt/python/cp$cp-cp$cp/bin/pip install -q --no-index --find-links=/deps pytest /io/repaired/assimp_py-*cp$cp-cp$cp-*.whl
done
cd /src
for cp in {cps}; do
  echo "== pytest cp$cp =="
  /opt/python/cp$cp-cp$cp/bin/python -m pytest tests -q
done
"""
    sh(f"docker run --rm --platform {image_platform} "
       f"-v {repo}:/src:ro -v {out}:/io -v {work}/testdeps:/deps:ro {image} bash -c {shlex_quote(inner)}")
    # clean the intermediate untagged wheels
    for p in out.glob("*.whl"):
        p.unlink()
    return repaired


def shlex_quote(s: str) -> str:
    import shlex
    return shlex.quote(s)


def main():
    global arch, libc, image_platform
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--arch", required=True, choices=sorted(ARCHES))
    ap.add_argument("--libc", required=True, choices=["gnu", "musl"])
    ap.add_argument("--glibc", default=None,
                    help="glibc pin for gnu targets (default: 2.17, or 2.19 "
                         "for ppc64le which did not exist before glibc 2.19)")
    ap.add_argument("--pythons", default=PY_VERSIONS_DEFAULT)
    ap.add_argument("--repo", default=".")
    ap.add_argument("--out", default="dist")
    ap.add_argument("--image", help="override the test/repair image")
    ap.add_argument("--skip-tests", action="store_true")
    args = ap.parse_args()

    arch = args.arch
    libc = args.libc
    gnu_triple, musl_triple, image_platform = ARCHES[arch]
    glibc = args.glibc or ("2.19" if arch == "ppc64le" else "2.17")
    triple = f"{gnu_triple}.{glibc}" if libc == "gnu" else musl_triple

    pythons = [p for p in args.pythons.split(",") if p]
    repo = pathlib.Path(args.repo).resolve()
    out = pathlib.Path(args.out).resolve()
    work = out / f".work-{arch}-{libc}"
    work.mkdir(parents=True, exist_ok=True)

    if libc == "gnu":
        image = args.image or f"quay.io/pypa/manylinux_2_28_{arch}:latest"
        plat = f"manylinux_2_28_{arch}"
    else:
        image = args.image or f"quay.io/pypa/musllinux_1_2_{arch}:latest"
        plat = f"musllinux_1_2_{arch}"

    zig = f"{sys.executable} -m ziglang"
    ext_suffixes = extract_headers(work, image, pythons)
    print("EXT_SUFFIXES:", ext_suffixes, file=sys.stderr)
    wrappers = make_wrappers(work, zig, triple)
    assimp_tar = fetch_assimp(work)
    build_dir = build(repo, work, wrappers, ext_suffixes, pythons, assimp_tar)
    pack_wheels(repo, build_dir, out, ext_suffixes, pythons)
    if args.skip_tests:
        print("wheels (unrepaired):", *[p.name for p in sorted(out.glob('*.whl'))], sep="\n  ")
        return
    download_test_deps(work, pythons)
    repaired = container_verify(work, repo, out, image, plat, ext_suffixes, pythons)
    print("wheels:", *[p.name for p in sorted(repaired.glob('*.whl'))], sep="\n  ")


if __name__ == "__main__":
    main()
