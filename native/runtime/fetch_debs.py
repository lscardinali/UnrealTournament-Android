#!/usr/bin/env python3
"""
Assemble the glibc (aarch64) userland the game process runs on, from Debian arm64 packages.

    python -I fetch_debs.py <download-dir> <out-dir> [--dev]

- Resolves each package in PACKAGES (and DEV_PACKAGES with --dev) against Debian's Packages index
  for SUITE, downloads the .deb into <download-dir> (cached), checks its SHA256 against the index,
  and extracts it into <out-dir>/root (a sysroot usable for cross-compiling with clang).
- Then copies the runtime shared libraries (real files, named by their SONAME) into <out-dir>/lib,
  which is what gets installed on the device next to the glibc loader.

No dependency resolution: the list is explicit so the runtime stays small and predictable.
"""
import hashlib
import io
import lzma
import os
import shutil
import sys
import tarfile
import urllib.request

MIRROR = "https://deb.debian.org/debian"
SUITE = "trixie"
ARCH = "arm64"

PACKAGES = [
    # C/C++ runtime (UT needs GLIBC_2.34; pocket-foundry's Turnip needs 2.38)
    "libc6", "libstdc++6", "libgcc-s1", "libatomic1",
    # X11 client side (SDL3 X11 backend dlopens these; Mesa X11 WSI links the xcb ones)
    "libx11-6", "libx11-xcb1", "libxcb1", "libxau6", "libxdmcp6", "libbsd0", "libmd0",
    "libxext6", "libxi6", "libxrandr2", "libxrender1", "libxfixes3", "libxcursor1", "libxss1",
    "libxkbcommon0", "libxcb-dri3-0", "libxcb-present0", "libxcb-xfixes0", "libxcb-sync1",
    "libxcb-randr0", "libxcb-shm0", "libxshmfence1", "libxcb-xkb1",
    # Turnip deps
    "zlib1g", "libzstd1", "libdrm2", "libexpat1",
    # Vulkan loader
    "libvulkan1",
    # Data: keyboard layouts for the X server (lorie), Xlib locale data for clients
    "xkb-data", "libx11-data",
]

# Headers + link stubs for building our own glibc-side code (SDL3, shims) with the NDK's clang.
DEV_PACKAGES = [
    "libc6-dev", "linux-libc-dev", "libgcc-14-dev", "libstdc++-14-dev",
    "libx11-dev", "x11proto-dev", "libxcb1-dev", "libxau-dev", "libxdmcp-dev",
    "libxext-dev", "libxi-dev", "libxrandr-dev", "libxrender-dev", "libxfixes-dev",
    "libxcursor-dev", "libxss-dev", "libxkbcommon-dev", "libvulkan-dev", "libxcb-randr0-dev", "libxcb-render0-dev",
]

# Runtime libraries to ship (SONAMEs), looked up in the extracted tree.
SKIP_LIB_PREFIXES = ("libc_malloc_debug", "libmemusage", "libpcprofile", "libSegFault", "libasan")


def fetch(url):
    with urllib.request.urlopen(url) as r:
        return r.read()


def load_index():
    raw = lzma.decompress(fetch(f"{MIRROR}/dists/{SUITE}/main/binary-{ARCH}/Packages.xz")).decode()
    index = {}
    for stanza in raw.split("\n\n"):
        fields = {}
        for line in stanza.splitlines():
            if line and not line[0].isspace() and ":" in line:
                k, v = line.split(":", 1)
                fields[k] = v.strip()
        if "Package" in fields:
            index[fields["Package"]] = fields
    return index


def ar_members(data):
    assert data[:8] == b"!<arch>\n", "not an ar archive"
    pos = 8
    while pos < len(data):
        header = data[pos:pos + 60]
        name = header[:16].decode().strip().rstrip("/")
        size = int(header[48:58].decode().strip())
        yield name, data[pos + 60:pos + 60 + size]
        pos += 60 + size + (size & 1)


def extract_deb(path, root):
    with open(path, "rb") as f:
        data = f.read()
    for name, member in ar_members(data):
        if name.startswith("data.tar"):
            mode = "r:xz" if name.endswith(".xz") else "r:gz" if name.endswith(".gz") else "r:"
            if name.endswith(".zst"):
                raise SystemExit(f"{path}: zstd data member not supported")
            with tarfile.open(fileobj=io.BytesIO(member), mode=mode) as tar:
                for m in tar.getmembers():
                    target = os.path.normpath(os.path.join(root, m.name))
                    if not target.startswith(os.path.normpath(root)):
                        continue  # no path escapes
                    if m.isdir():
                        os.makedirs(target, exist_ok=True)
                    elif m.isfile():
                        os.makedirs(os.path.dirname(target), exist_ok=True)
                        with tar.extractfile(m) as src, open(target, "wb") as dst:
                            shutil.copyfileobj(src, dst)
                    elif m.issym():
                        # Windows: no symlinks; record them so we can resolve after extraction.
                        os.makedirs(os.path.dirname(target), exist_ok=True)
                        with open(target + ".symlink", "w") as dst:
                            dst.write(m.linkname)


def resolve_symlinks(root):
    """Replace every *.symlink marker with a copy of its target (looping until stable)."""
    for _ in range(5):
        pending = []
        for dirpath, _, files in os.walk(root):
            for fn in files:
                if fn.endswith(".symlink"):
                    pending.append(os.path.join(dirpath, fn))
        if not pending:
            return
        for marker in pending:
            link = marker[:-len(".symlink")]
            with open(marker) as f:
                target = f.read()
            src = os.path.normpath(os.path.join(root, target.lstrip("/")) if target.startswith("/")
                                   else os.path.join(os.path.dirname(link), target))
            if os.path.isfile(src):
                shutil.copyfile(src, link)
                os.remove(marker)
            elif os.path.isdir(src):
                os.remove(marker)  # directory links are not needed for our purposes


def collect_runtime_libs(root, out_lib):
    os.makedirs(out_lib, exist_ok=True)
    for sub in ("lib/aarch64-linux-gnu", "usr/lib/aarch64-linux-gnu"):
        d = os.path.join(root, sub)
        if not os.path.isdir(d):
            continue
        for fn in os.listdir(d):
            p = os.path.join(d, fn)
            if ".so" in fn and os.path.isfile(p) and not fn.endswith(".so") and not fn.startswith(SKIP_LIB_PREFIXES):
                shutil.copyfile(p, os.path.join(out_lib, fn))
    loader = os.path.join(root, "usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1")
    if os.path.isfile(loader):
        shutil.copyfile(loader, os.path.join(out_lib, "ld-linux-aarch64.so.1"))
    gconv_src = os.path.join(root, "usr/lib/aarch64-linux-gnu/gconv")
    gconv_dst = os.path.join(out_lib, "gconv")
    os.makedirs(gconv_dst, exist_ok=True)
    for fn in ("gconv-modules", "CP1252.so", "UTF-16.so", "UNICODE.so", "UTF-32.so"):
        shutil.copyfile(os.path.join(gconv_src, fn), os.path.join(gconv_dst, fn))
    if os.path.isdir(os.path.join(gconv_src, "gconv-modules.d")):
        shutil.copytree(os.path.join(gconv_src, "gconv-modules.d"), os.path.join(gconv_dst, "gconv-modules.d"),
                        dirs_exist_ok=True)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    dl, out = sys.argv[1], sys.argv[2]
    wanted = PACKAGES + (DEV_PACKAGES if "--dev" in sys.argv else [])
    os.makedirs(dl, exist_ok=True)
    root = os.path.join(out, "root")
    index = load_index()
    for pkg in wanted:
        info = index.get(pkg)
        if not info:
            raise SystemExit(f"package not found in {SUITE}: {pkg}")
        fn = os.path.join(dl, os.path.basename(info["Filename"]))
        if not os.path.isfile(fn) or hashlib.sha256(open(fn, "rb").read()).hexdigest() != info["SHA256"]:
            data = fetch(f"{MIRROR}/{info['Filename']}")
            if hashlib.sha256(data).hexdigest() != info["SHA256"]:
                raise SystemExit(f"sha256 mismatch: {pkg}")
            with open(fn, "wb") as f:
                f.write(data)
        print(f"{pkg} {info['Version']}")
        extract_deb(fn, root)
    resolve_symlinks(root)
    # Debian is merged-/usr: /lib -> usr/lib. Linker scripts (libc.so) reference /lib/..., so mirror it.
    usr_multiarch = os.path.join(root, "usr/lib/aarch64-linux-gnu")
    shutil.copytree(usr_multiarch, os.path.join(root, "lib/aarch64-linux-gnu"), dirs_exist_ok=True)
    loader = os.path.join(root, "usr/lib/ld-linux-aarch64.so.1")
    if os.path.isfile(loader):
        shutil.copyfile(loader, os.path.join(root, "lib/ld-linux-aarch64.so.1"))
    collect_runtime_libs(root, os.path.join(out, "lib"))
    # X data (xkb layouts + Xlib locale) -> out/share/X11
    share = os.path.join(out, "share", "X11")
    for sub in ("xkb", "locale"):
        src_dir = os.path.join(root, "usr/share/X11", sub)
        if os.path.isdir(src_dir):
            shutil.copytree(src_dir, os.path.join(share, sub), dirs_exist_ok=True)
    print("runtime libs:", len(os.listdir(os.path.join(out, "lib"))))


if __name__ == "__main__":
    main()
