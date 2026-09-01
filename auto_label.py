#!/usr/bin/env python3
import argparse
import json
import os
from datetime import datetime, timezone
from pathlib import Path

from PIL import Image


IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".webp"}


def collect_cut_assets(folder: Path) -> list[dict[str, str]]:
    cut_dir = folder / "cut_pic"
    if not cut_dir.is_dir():
        return []

    assets = []
    for cut_path in sorted(cut_dir.iterdir()):
        if not cut_path.is_file() or cut_path.suffix.lower() not in IMAGE_EXTS:
            continue
        assets.append(
            {
                "id": cut_path.stem,
                "file": str(cut_path.relative_to(folder)).replace("\\", "/"),
            }
        )
    return assets


def process_dir(folder: Path, args: argparse.Namespace) -> Path | None:
    ui_path = folder / "ui.jpg"
    if not ui_path.is_file():
        return None

    with Image.open(ui_path) as ui_image:
        ui_size = ui_image.size

    output_dir = Path(args.output_dir) / folder.name
    output_dir.mkdir(parents=True, exist_ok=True)
    ui_rel = Path(os.path.relpath(ui_path.resolve(), output_dir.resolve())).as_posix()

    result = {
        "version": 1,
        "source_dir": folder.name,
        "ui_image": ui_rel,
        "ui_size": {"width": ui_size[0], "height": ui_size[1]},
        "coord_space": "ui_original",
        "annotation_strategy": "glm_first",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "cut_assets": collect_cut_assets(folder),
        "annotations": [],
    }

    json_path = output_dir / args.output_json
    json_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    return json_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate UI annotation JSON from ui.jpg with GLM vision.")
    parser.add_argument("--res", default="res", help="resource root containing subfolders")
    parser.add_argument("--dir", default=None, help="process only one resource subfolder")
    parser.add_argument("--output-dir", default="output", help="directory for generated annotation folders")
    parser.add_argument("--output-json", default="annotations.json")
    parser.add_argument("--no-glm", action="store_true", help="only write the draft JSON; do not call GLM")
    parser.add_argument("--glm-api-key", default=None, help="Zhipu API key (default: env ZHIPU_API_KEY or glm_api_key.local)")
    parser.add_argument("--glm-model", default="glm-5v-turbo")
    parser.add_argument("--glm-thinking", action="store_true", help="enable GLM thinking mode")
    parser.add_argument("--glm-no-cut-images", action="store_true", help="do not upload cut_pic images to GLM")
    parser.add_argument("--save-raw", action="store_true", help="save raw GLM response for debugging")
    return parser.parse_args()


def maybe_glm_annotate(json_path: Path, args: argparse.Namespace) -> None:
    if args.no_glm:
        return
    from glm_annotate import annotate_annotations_file

    annotate_annotations_file(
        json_path,
        api_key=args.glm_api_key,
        model=args.glm_model,
        thinking=args.glm_thinking,
        include_cut_images=not args.glm_no_cut_images,
        save_raw=args.save_raw,
    )


def main() -> None:
    args = parse_args()
    res_root = Path(args.res)
    folders = [res_root / args.dir] if args.dir else sorted(p for p in res_root.iterdir() if p.is_dir())
    outputs = []
    for folder in folders:
        json_path = process_dir(folder, args)
        if json_path:
            maybe_glm_annotate(json_path, args)
            outputs.append(json_path)
    if not outputs:
        raise SystemExit("No folders with ui.jpg were found.")
    print("written:")
    for output in outputs:
        print(f"  {output}")


if __name__ == "__main__":
    main()