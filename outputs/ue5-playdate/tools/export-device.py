"""Convert the site's rigid mesh hierarchy into UE procedural-mesh data.

No SDK game assets are used. The original model remains in playdate-local.
Three's right-handed Y-up basis becomes UE's left-handed Z-up basis;
source units are scaled to centimetres. The basis reflection makes Three's
CCW front faces match UE's CW front faces, so the index order is retained.
"""
import argparse
import json
import pathlib
import shutil
import struct

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT.parent / "playdate-local/assets"
BASIS = np.array([[0, 0, -1], [1, 0, 0], [0, 1, 0]], dtype=float)
SCALE = 8.0


def quaternion(m):
    # Standard column-vector rotation matrix -> x,y,z,w quaternion.
    tr = np.trace(m)
    if tr > 0:
        s = np.sqrt(tr + 1) * 2
        return [(m[2, 1]-m[1, 2])/s, (m[0, 2]-m[2, 0])/s,
                (m[1, 0]-m[0, 1])/s, s/4]
    i = int(np.argmax(np.diag(m)))
    j, k = (i+1) % 3, (i+2) % 3
    s = np.sqrt(1 + m[i, i] - m[j, j] - m[k, k]) * 2
    q = [0.0]*4
    q[i] = s/4
    q[j] = (m[i, j]+m[j, i])/s
    q[k] = (m[i, k]+m[k, i])/s
    q[3] = (m[k, j]-m[j, k])/s
    return q


def export():
    scene = json.loads((SOURCE / "scenes/asheville.json").read_text())
    source_data = (SOURCE / "scenes/data/asheville.bin").read_bytes()
    geometries = {g["uuid"]: g for g in scene["geometries"]}
    materials = {m["uuid"]: m for m in scene["materials"]}
    images = {i["uuid"]: i["url"] for i in scene["images"]}
    textures = {t["uuid"]: images[t["image"]] for t in scene["textures"]}
    target = ROOT / "Unreal/Content/Device/Data"
    target.mkdir(parents=True, exist_ok=True)
    texture_source = ROOT / "Unreal/ContentSource/Device"
    texture_source.mkdir(parents=True, exist_ok=True)
    binary = bytearray()
    nodes, used_materials = [], set()

    def array(g, key, columns):
        start, end = g["offsets"][key]
        dtype = "<u4" if key == "index" else "<f4"
        return np.frombuffer(source_data[start:end+1], dtype=dtype).reshape(-1, columns)

    def section(g, indices, material, center):
        p, n, uv = array(g, "position", 3), array(g, "normal", 3), array(g, "uv", 2)
        unique, mapped = np.unique(indices, return_inverse=True)
        p = ((p[unique]-center) @ BASIS.T)*SCALE
        n = n[unique] @ BASIS.T
        uv = uv[unique].copy()
        uv[:, 1] = 1-uv[:, 1]
        vertices = np.hstack((p, n, uv)).astype("<f4")
        indices = mapped.reshape(-1, 3).astype("<u4")
        offset = len(binary)
        binary.extend(vertices.tobytes())
        binary.extend(indices.tobytes())
        used_materials.add(material)
        return dict(offset=offset, vertices=len(vertices), indices=indices.size,
                    material=materials[material]["name"])

    def walk(n, parent):
        name = n.get("name", "group")
        if n.get("visible") is False or name in {"copy", "crank 1"}:
            return
        matrix = np.array(n.get("matrix", np.eye(4).flatten(order="F")), dtype=float).reshape(4, 4, order="F")
        rotation = BASIS @ matrix[:3, :3] @ BASIS.T
        scale = np.linalg.norm(rotation, axis=0)
        position = BASIS @ matrix[:3, 3]*SCALE
        node = dict(name=name, parent=parent, position=position.tolist(),
                    rotation=quaternion(rotation/scale), scale=scale.tolist(), sections=[])
        index = len(nodes)
        nodes.append(node)
        if "geometry" in n:
            g = geometries[n["geometry"]]
            idx = array(g, "index", 1).flatten()
            groups = g["groups"] or [dict(start=0, count=len(idx), materialIndex=0)]
            split = {}
            for group in groups:
                triangles = idx[group["start"]:group["start"]+group["count"]].reshape(-1, 3)
                mat = n["materials"][group["materialIndex"]]
                if name == "main" and group["materialIndex"] in (1, 6):
                    p = array(g, "position", 3)
                    for tri in triangles:
                        x, _, z = p[tri].mean(axis=0)
                        part = "dpad" if group["materialIndex"] == 6 else (
                            ("A" if x < -.15 else "B") if z < 0 else ("sleep" if z > .35 else "menu"))
                        split.setdefault((part, mat), []).extend(tri.tolist())
                else:
                    node["sections"].append(section(g, triangles.flatten(), mat, np.zeros(3)))
            for (part, mat), ids in split.items():
                ids = np.array(ids, dtype=np.uint32)
                used = array(g, "position", 3)[np.unique(ids)]
                center = (used.min(axis=0)+used.max(axis=0))/2
                nodes.append(dict(name=part, parent=index, position=(BASIS @ center*SCALE).tolist(),
                                  rotation=[0, 0, 0, 1], scale=[1, 1, 1],
                                  sections=[section(g, ids, mat, center)]))
        for child in n.get("children", []):
            walk(child, index)

    device = next(n for n in scene["object"]["children"] if n.get("name") == "device")
    walk(device, -1)
    defs = []
    for material_id in sorted(used_materials):
        m = materials[material_id]
        definition = dict(name=m["name"], color=m.get("color", 0xffffff),
                          roughness=1-m.get("glossFactor", 1), metallic=m.get("metalFactor", 1))
        # The scene is unfolded. Its original viewer swaps the baked AO maps
        # when opening the crank; retaining the folded maps leaves a dark stain.
        open_material = next((other for other in materials.values()
                              if other['name'] == m['name']+'_crank_open'), None)
        for role in ("map", "normalMap", "packedPBRMap", "aoMap", "metalGlossMap"):
            if role in m:
                texture_id = open_material[role] if open_material and role == 'packedPBRMap' else m[role]
                filename = pathlib.Path(textures[texture_id]).name
                shutil.copy2(SOURCE / "asheville" / textures[texture_id], texture_source / filename)
                definition[role] = filename
        defs.append(definition)
    # Preserve the original prefiltered LogLuv atlas byte for byte. The UE
    # material samples this just as the webpage's mobile panorama path does.
    panorama = np.frombuffer((SOURCE/'environments/env/panorama.bin').read_bytes(),
                             dtype=np.uint8).reshape(1024, 1024, 4)
    Image.fromarray(panorama).save(texture_source/'EnvironmentLogLuv.png')
    shutil.copy2(SOURCE/'asheville/Textures/ground_shadow.png',texture_source/'ground_shadow.png')
    harmonics = json.loads((SOURCE/'environments/env/irradiance.json').read_text())[:27]
    (target / "device.bin").write_bytes(binary)
    manifest = dict(version=1, source="play.date intro-canvas asheville", unitScale=SCALE,
                    nodes=nodes, materials=defs, bytes=len(binary), environmentSH=harmonics)
    (target / "device.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(dict(nodes=len(nodes), sections=sum(len(n['sections']) for n in nodes),
                          triangles=sum(s['indices']//3 for n in nodes for s in n['sections']),
                          bytes=len(binary))))


if __name__ == "__main__":
    export()
