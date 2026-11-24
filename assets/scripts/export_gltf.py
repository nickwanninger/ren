#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Export a .blend file as separate glTF with original texture references.

Usage:
    uv run scripts/export_gltf.py cube.blend
    uv run scripts/export_gltf.py cube.blend -o /tmp/export/cube.gltf

Requires Blender. UVs and materials come from the .blend file unchanged.
The script does not save changes to the .blend file.
"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
from urllib.parse import unquote, urlsplit


WORKER_FLAG = "--blender-worker"


def export_in_blender(output: Path) -> None:
    import bpy

    # Fail before export if a material uses an image without an external file.
    for material in bpy.data.materials:
        if material.node_tree is None:
            continue
        for node in material.node_tree.nodes:
            if node.type != "TEX_IMAGE" or node.image is None:
                continue
            image = node.image
            source = Path(bpy.path.abspath(image.filepath, library=image.library))
            if not image.filepath or not source.is_file():
                raise RuntimeError(
                    f"Image {image.name!r} needs an external file: {image.filepath!r}"
                )

    output.parent.mkdir(parents=True, exist_ok=True)
    result = bpy.ops.export_scene.gltf(
        filepath=str(output),
        export_format="GLTF_SEPARATE",
        export_keep_originals=True,
        export_materials="EXPORT",
        export_image_format="AUTO",
        export_texcoords=True,
        export_normals=True,
        use_selection=False,
    )
    if "FINISHED" not in result:
        raise RuntimeError(f"Export failed: {result}")

    document = json.loads(output.read_text())
    for image in document.get("images", []):
        uri = image.get("uri", "")
        parsed = urlsplit(uri)
        if not uri or parsed.scheme or parsed.netloc or "bufferView" in image:
            raise RuntimeError(f"Expected a relative external image URI: {image}")
        if not (output.parent / unquote(parsed.path)).is_file():
            raise RuntimeError(f"Exported texture does not exist: {uri}")
        print(f"Texture: {uri}")
    if not document.get("images"):
        print("Warning: export contains no textures. Inspect the material node links.")
    print(f"Exported: {output}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("blend", type=Path, help="Source .blend file")
    parser.add_argument("-o", "--output", type=Path, help="Output .gltf path (default: beside source)")
    parser.add_argument("--blender", help="Blender executable path or command")
    args = parser.parse_args()
    source = args.blend.resolve()
    output = args.output.resolve() if args.output else source.with_suffix(".gltf")
    if not source.is_file() or source.suffix.lower() != ".blend":
        parser.error(f"Expected an existing .blend file: {source}")
    if output.suffix.lower() != ".gltf":
        parser.error("Output must use the .gltf extension")

    blender = args.blender or shutil.which("blender")
    if not blender:
        mac_blender = Path("/Applications/Blender.app/Contents/MacOS/Blender")
        if mac_blender.is_file():
            blender = str(mac_blender)
    if not blender:
        parser.error("Blender not found. Set --blender /path/to/blender")

    return subprocess.run([
        blender, "--background", str(source),
        "--python-exit-code", "1", "--python", str(Path(__file__).resolve()),
        "--", WORKER_FLAG, str(output),
    ], check=False).returncode


if __name__ == "__main__":
    if "--" in sys.argv and sys.argv[sys.argv.index("--") + 1:][:1] == [WORKER_FLAG]:
        export_in_blender(Path(sys.argv[sys.argv.index("--") + 2]))
    else:
        sys.exit(main())
