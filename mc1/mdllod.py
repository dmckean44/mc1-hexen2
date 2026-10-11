#!/usr/bin/env python3
"""Build reduced-polygon versions of the MDL models in a Hexen II pak file.

The simplification is md2lod's (q2mr/tools/md2lod): half-edge collapses ranked
by quadric error over every frame. Models are read as version 6 or 50 and
written as version 50, which keeps a skin coordinate per triangle corner.
Vertices that sit at the same place in every frame are welded first; Hexen II
models split them wherever the skin layout does. Skin coordinates keep their
onseam flag (as a third column of st) and triangles their facesfront flag, so
the renderer's half-skin shift for back faces still applies after collapses.
"""

import argparse
import fnmatch
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "../../q2mr/tools/md2lod"))
from md2lod import Simplifier, read_pak, write_pak  # noqa: E402

HEADER = struct.Struct("<4si3f3ff3f8if")      # mdl_t
NEW_HEADER = struct.Struct("<4si3f3ff3f8ifi")  # newmdl_t
VERSION, NEW_VERSION = 6, 50


class Mdl:
    """Holds a model in the shape md2lod's Simplifier works on: st, tri_xyz,
    tri_st, frame_verts and positions()."""

    def __init__(self, data, weld_normals=False):
        ident, version = struct.unpack_from("<4si", data)
        if ident not in (b"IDPO", b"RAPO") or version not in (VERSION, NEW_VERSION):
            raise ValueError("not a Hexen II model")
        hdr = (NEW_HEADER if version == NEW_VERSION else HEADER).unpack_from(data)
        self.scale = np.array(hdr[2:5])
        self.origin = np.array(hdr[5:8])
        self.radius = hdr[8]
        self.eye = hdr[9:12]
        (num_skins, self.skinwidth, self.skinheight, num_verts, num_tris,
         num_frames, self.synctype, self.flags) = hdr[12:20]
        self.size = hdr[20]
        num_st = hdr[21] if version == NEW_VERSION else num_verts
        pos = (NEW_HEADER if version == NEW_VERSION else HEADER).size

        skin_bytes = self.skinwidth * self.skinheight
        start = pos
        for _ in range(num_skins):
            (kind,) = struct.unpack_from("<i", data, pos)
            pos += 4
            if kind == 0:
                pos += skin_bytes
            else:
                (n,) = struct.unpack_from("<i", data, pos)
                pos += 4 + 4 * n + n * skin_bytes
        self.skins = data[start:pos]
        self.num_skins = num_skins

        stverts = np.frombuffer(data, "<i4", num_st * 3, pos).reshape(-1, 3)
        pos += num_st * 12
        if version == NEW_VERSION:
            raw = np.frombuffer(data, np.uint8, num_tris * 16, pos).reshape(-1, 16)
            front = raw[:, 0:4].copy().view("<i4")[:, 0]
            corners = raw[:, 4:16].copy().view("<u2")
            tri_xyz, tri_st = corners[:, :3].astype(np.int64), corners[:, 3:].astype(np.int64)
            pos += num_tris * 16
        else:
            raw = np.frombuffer(data, "<i4", num_tris * 4, pos).reshape(-1, 4)
            front, tri_xyz = raw[:, 0], raw[:, 1:].astype(np.int64)
            tri_st = tri_xyz.copy()
            pos += num_tris * 16

        # st rows are (s, t, onseam), one per distinct value
        st_index = {}
        self.tri_st = np.empty_like(tri_st)
        for t in range(num_tris):
            for c in range(3):
                onseam, s, tt = stverts[tri_st[t, c]]
                key = (int(s), int(tt), 1 if onseam else 0)
                self.tri_st[t, c] = st_index.setdefault(key, len(st_index))
        self.st = np.array(list(st_index), np.int64).reshape(-1, 3)
        self.front = (front != 0).astype(np.int64)
        self.tri_xyz = tri_xyz

        # Frames: simple frames and groups, flattened; the layout is kept to write them back.
        self.layout = []
        verts = []
        names = []
        frame_size = 24 + num_verts * 4
        for _ in range(num_frames):
            (kind,) = struct.unpack_from("<i", data, pos)
            pos += 4
            if kind == 0:
                self.layout.append(None)
                names.append(data[pos + 8:pos + 24])
                verts.append(np.frombuffer(data, np.uint8, num_verts * 4, pos + 24).reshape(-1, 4))
                pos += frame_size
            else:
                (n,) = struct.unpack_from("<i", data, pos)
                intervals = data[pos + 12:pos + 12 + 4 * n]
                self.layout.append(intervals)
                pos += 12 + 4 * n
                for _ in range(n):
                    names.append(data[pos + 8:pos + 24])
                    verts.append(np.frombuffer(data, np.uint8, num_verts * 4, pos + 24).reshape(-1, 4))
                    pos += frame_size
        self.frame_names = names
        self.frame_verts = np.stack(verts)
        self.weld(weld_normals)

    def weld(self, weld_normals):
        """Merges vertices at the same position in every frame. Hexen II models
        split vertices wherever the skin layout does; skin coordinates are kept
        per triangle corner, so the mesh can be joined without losing them. Unless
        weld_normals is set, merged vertices must also share their normals, which
        leaves the shading exactly as it was."""
        first = {}
        remap = np.empty(self.frame_verts.shape[1], np.int64)
        for v in range(self.frame_verts.shape[1]):
            key = self.frame_verts[:, v, :3 if weld_normals else 4].tobytes()
            remap[v] = first.setdefault(key, v)
        keep = np.unique(remap)
        index = np.full(len(remap), -1)
        index[keep] = np.arange(len(keep))
        self.tri_xyz = index[remap[self.tri_xyz]]
        self.frame_verts = self.frame_verts[:, keep]
        degenerate = (self.tri_xyz[:, 0] == self.tri_xyz[:, 1]) | \
                     (self.tri_xyz[:, 1] == self.tri_xyz[:, 2]) | \
                     (self.tri_xyz[:, 2] == self.tri_xyz[:, 0])
        self.tri_xyz = self.tri_xyz[~degenerate]
        self.tri_st = self.tri_st[~degenerate]
        self.front = self.front[~degenerate]

    def positions(self):
        return self.frame_verts[:, :, :3].astype(np.float64) * self.scale + self.origin

    def serialize(self):
        num_verts = self.frame_verts.shape[1]
        out = bytearray(NEW_HEADER.pack(
            b"RAPO", NEW_VERSION, *self.scale, *self.origin, self.radius, *self.eye,
            self.num_skins, self.skinwidth, self.skinheight, num_verts, len(self.tri_xyz),
            len(self.layout), self.synctype, self.flags, self.size, len(self.st)))
        out += self.skins
        st = np.zeros((len(self.st), 3), "<i4")
        st[:, 0] = self.st[:, 2] * 0x20
        st[:, 1:] = self.st[:, :2]
        out += st.tobytes()
        for front, xyz, sts in zip(self.front, self.tri_xyz, self.tri_st):
            out += struct.pack("<i3H3H", int(front), *map(int, xyz), *map(int, sts))

        def frame(i):
            v = self.frame_verts[i]
            lo = np.append(v[:, :3].min(axis=0), 0).astype(np.uint8)
            hi = np.append(v[:, :3].max(axis=0), 0).astype(np.uint8)
            return lo.tobytes() + hi.tobytes() + self.frame_names[i] + v.tobytes()

        i = 0
        for group in self.layout:
            if group is None:
                out += struct.pack("<i", 0) + frame(i)
                i += 1
            else:
                n = len(group) // 4
                v = self.frame_verts[i:i + n, :, :3]
                lo = np.append(v.min(axis=(0, 1)), 0).astype(np.uint8)
                hi = np.append(v.max(axis=(0, 1)), 0).astype(np.uint8)
                out += struct.pack("<ii", 1, n) + lo.tobytes() + hi.tobytes() + group
                for _ in range(n):
                    out += frame(i)
                    i += 1
        return bytes(out)


class MdlSimplifier(Simplifier):
    """md2lod's Simplifier, but a UV island of u with no triangle shared with v
    keeps u's skin coordinate when u moves to v, instead of blocking the
    collapse. Hexen II skins split almost every vertex into islands, so without
    this few collapses are possible; the texture stretches a little instead."""

    def can_collapse(self, u, v):
        if self.locked[u]:
            return None
        shared = self.vert_tris[u] & self.vert_tris[v]
        if not shared:
            return None
        if self.neighbours(u) & self.neighbours(v) != {
                int(w) for t in shared for w in self.tri_xyz[t] if w != u and w != v}:
            return None
        new_st = {}
        wedges = self.wedges(u)
        for wedge, tris in wedges.items():
            sides = tris & shared
            if not sides:
                new_st[wedge] = self.st_at(next(iter(tris)), u)
                continue
            if len({self.st_value(t, v) for t in sides}) != 1:
                return None
            new_st[wedge] = self.st_at(next(iter(sides)), v)
        moved = [t for t in self.vert_tris[u] if t not in shared]
        if not moved:
            return None
        tri = self.tri_xyz[moved]
        new_tri = np.where(tri == u, v, tri)
        if not self._orientation_kept(tri, new_tri):
            return None
        remap = {t: new_st[self.st_value(t, u)] for t in moved}
        for wedge, tris in wedges.items():
            if not self._skin_area_kept(tris, remap, u):
                return None
        return remap


def target_for(name, num_tris, args):
    for rule in args.rule:
        pattern, ratio = rule.split("=")
        if fnmatch.fnmatch(name, pattern):
            return max(args.min_tris, int(num_tris * float(ratio)))
    return max(args.min_tris, int(num_tris * args.ratio))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paks", nargs="+", help="source pak files, later ones override earlier ones")
    parser.add_argument("-o", "--output", required=True, help="output pak file")
    parser.add_argument("-r", "--ratio", type=float, default=0.5, help="fraction of triangles to keep")
    parser.add_argument("--rule", action="append", default=[],
                        help="per-model ratio, e.g. 'models/gauntlet.mdl=0.75' (first match wins)")
    parser.add_argument("--min-tris", type=int, default=48, help="leave models this small alone")
    parser.add_argument("--min-normal-dot", type=float, default=0.2)
    parser.add_argument("--seam-weight", type=float, default=2.0)
    parser.add_argument("--uv-tolerance", type=float, default=2.0)
    parser.add_argument("--only", default="*.mdl", help="glob of models to process")
    parser.add_argument("--weld-normals", action="store_true",
                        help="also merge vertices whose normals differ (changes shading)")
    args = parser.parse_args()

    files = {}
    for pak in args.paks:
        files.update(read_pak(pak))

    out = {}
    total_before = total_after = 0
    for name in sorted(files):
        if not name.endswith(".mdl") or not fnmatch.fnmatch(name, args.only):
            continue
        mdl = Mdl(files[name], args.weld_normals)
        before = len(mdl.tri_xyz)
        target = target_for(name, before, args)
        simplifier = MdlSimplifier(mdl, args.min_normal_dot, args.seam_weight, args.uv_tolerance)
        if before > args.min_tris and target < before:
            simplifier.run(target)
        front = mdl.front[simplifier.alive]
        small = simplifier.result()
        small.front = front
        total_before += before
        total_after += len(small.tri_xyz)
        out[name] = small.serialize()
        print(f"{name}: {before} -> {len(small.tri_xyz)} triangles", file=sys.stderr)

    write_pak(args.output, out)
    print(f"{len(out)} models, {total_before} -> {total_after} triangles", file=sys.stderr)


if __name__ == "__main__":
    main()
