#!/usr/bin/env python3
"""Use GLM-5V-Turbo to create UI annotations and infer hierarchy."""

from __future__ import annotations

import argparse
import base64
import io
import json
import os
import re
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from PIL import Image

TYPE_COLORS = {
    "button": "#e11d48",
    "panel": "#2563eb",
    "img": "#16a34a",
    "list": "#d97706",
    "label": "#7c3aed",
}
VALID_TYPES = set(TYPE_COLORS)
DEFAULT_API_URL = "https://open.bigmodel.cn/api/paas/v4/chat/completions"
DEFAULT_MODEL = "glm-5v-turbo"
DEFAULT_API_KEY_FILE = Path(__file__).with_name("glm_api_key.local")
IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".webp"}


def load_api_key(explicit: str | None = None) -> str | None:
    if explicit:
        return explicit.strip()
    for env_name in ("ZHIPU_API_KEY", "BIGMODEL_API_KEY"):
        value = os.environ.get(env_name)
        if value:
            return value.strip()
    if DEFAULT_API_KEY_FILE.exists():
        return DEFAULT_API_KEY_FILE.read_text(encoding="utf-8").strip()
    return None


def resolve_ui_path(json_path: Path, document: dict[str, Any]) -> Path:
    rel = document.get("ui_image_relative_path") or document.get("ui_image") or "ui.jpg"
    candidate = (json_path.parent / rel).resolve()
    if candidate.exists():
        return candidate
    source_dir = document.get("source_dir")
    if source_dir:
        alt = (json_path.parent.parent / "res" / source_dir / "ui.jpg").resolve()
        if alt.exists():
            return alt
    raise FileNotFoundError(f"UI image not found for {json_path}")


def prepare_image_payload(image_path: Path, max_long_edge: int) -> tuple[str, float, tuple[int, int]]:
    image = Image.open(image_path).convert("RGB")
    orig_w, orig_h = image.size
    scale = 1.0
    if max(orig_w, orig_h) > max_long_edge:
        scale = max_long_edge / max(orig_w, orig_h)
        new_w = max(1, round(orig_w * scale))
        new_h = max(1, round(orig_h * scale))
        image = image.resize((new_w, new_h), Image.Resampling.LANCZOS)

    buffer = io.BytesIO()
    image.save(buffer, format="JPEG", quality=92)
    encoded = base64.b64encode(buffer.getvalue()).decode("ascii")
    return encoded, scale, (orig_w, orig_h)


def resolve_source_dir(json_path: Path, document: dict[str, Any]) -> Path:
    return resolve_ui_path(json_path, document).parent


def resolve_cut_image_path(source_dir: Path, cut_rel: str) -> Path | None:
    if not cut_rel:
        return None
    candidate = (source_dir / cut_rel).resolve()
    if candidate.is_file():
        return candidate
    return None


def encode_image_base64(image_path: Path, max_long_edge: int) -> str:
    encoded, _, _ = prepare_image_payload(image_path, max_long_edge)
    return encoded


def iter_cut_assets(document: dict[str, Any], source_dir: Path) -> list[dict[str, str]]:
    assets: list[dict[str, str]] = []
    seen: set[str] = set()

    for asset in document.get("cut_assets") or []:
        asset_id = str(asset.get("id") or "").strip()
        asset_file = str(asset.get("file") or "").strip()
        if not asset_id or not asset_file or asset_id in seen:
            continue
        assets.append({"id": asset_id, "file": asset_file.replace("\\", "/")})
        seen.add(asset_id)

    cut_dir = source_dir / "cut_pic"
    if cut_dir.is_dir():
        for cut_path in sorted(cut_dir.iterdir()):
            if not cut_path.is_file() or cut_path.suffix.lower() not in IMAGE_EXTS:
                continue
            asset_id = cut_path.stem
            if asset_id in seen:
                continue
            assets.append({"id": asset_id, "file": str(cut_path.relative_to(source_dir)).replace("\\", "/")})
            seen.add(asset_id)

    return assets


def collect_cut_image_payloads(
    document: dict[str, Any],
    source_dir: Path,
    *,
    max_long_edge: int,
) -> list[dict[str, str]]:
    payloads: list[dict[str, str]] = []
    for asset in iter_cut_assets(document, source_dir):
        cut_path = resolve_cut_image_path(source_dir, asset["file"])
        if cut_path is None:
            print(f"  warning: cut image not found for {asset['id']}: {asset['file']}")
            continue
        payloads.append(
            {
                "id": asset["id"],
                "file": asset["file"],
                "b64": encode_image_base64(cut_path, max_long_edge),
            }
        )
    return payloads


def annotation_summary(annotations: list[dict[str, Any]]) -> list[dict[str, Any]]:
    summary = []
    for ann in annotations:
        item = {
            "id": ann.get("id"),
            "name": ann.get("name"),
            "type": ann.get("type", "img"),
            "matched": bool(ann.get("matched")),
        }
        if ann.get("matched") and ann.get("x") is not None:
            item["bbox"] = {
                "x": ann["x"],
                "y": ann["y"],
                "width": ann["width"],
                "height": ann["height"],
            }
        elif ann.get("cut_image"):
            item["cut_image"] = ann["cut_image"]
            item["note"] = "template match failed; locate visually if possible"
        summary.append(item)
    return summary


def build_prompt(
    document: dict[str, Any],
    annotations: list[dict[str, Any]],
    orig_size: tuple[int, int],
    *,
    cut_image_ids: list[str] | None = None,
) -> str:
    ui_w, ui_h = orig_size
    existing = annotation_summary(annotations)
    existing_ids = {ann["id"] for ann in annotations if ann.get("id")}
    cut_ids = cut_image_ids or []
    cut_section = ""
    if cut_ids:
        cut_section = f"""
Artist-provided cut assets are attached BEFORE this text (one image per id).
Use these cut assets as visual references only. They are not pre-located.
When a visible UI component clearly corresponds to a cut asset, you may create a new annotation using the cut asset id and include its cut_image file path if known.
Cut asset ids: {json.dumps(cut_ids, ensure_ascii=False)}
"""

    return f"""You are a mobile game UI annotation assistant.

The first attached image is the full UI screenshot.
{f"Additional images are artist cut assets from cut_pic/.{cut_section}" if cut_ids else ""}
Create a practical annotation set for the screenshot, then infer parent-child hierarchy.

Image coordinate system:
- Original UI size: width={ui_w}, height={ui_h}
- Bounding boxes must use top-left origin: x, y, width, height in ORIGINAL pixel coordinates.
- Do not invent components that are not visible.

Supported types: button, panel, img, list, label
- panel: container / background region that groups children
- button: clickable control
- img: decorative or informational image/icon (non-text)
- label: text label
- list: scrollable/repeated list area

Existing annotations, if any, may be updated but not duplicated (existing ids: {sorted(existing_ids)}):
{json.dumps(existing, ensure_ascii=False, indent=2)}

Return ONLY valid JSON (no markdown) with this schema:
{{
  "new_annotations": [
    {{
      "id": "unique_snake_case_id",
      "name": "human readable name",
      "type": "button|panel|img|list|label",
      "x": 0,
      "y": 0,
      "width": 1,
      "height": 1,
      "parent_id": null,
      "cut_image": null,
      "confidence": 0.0
    }}
  ],
  "updates": [
    {{
      "id": "existing_id",
      "type": "button",
      "parent_id": "panel_main",
      "x": 0,
      "y": 0,
      "width": 1,
      "height": 1,
      "confidence": 0.0
    }}
  ]
}}

Rules:
- new_annotations: include major visible panels, buttons, lists, labels, and key images.
- If a component corresponds to a cut asset, prefer the cut asset id for id and set cut_image to its cut_pic path when known.
- updates: only for existing ids; use this for manually edited or previously generated annotations.
- parent_id must reference another annotation id or null for root-level nodes.
- Prefer meaningful English snake_case ids.
- Keep the annotation set practical, not pixel-perfect decorative fragments."""


def repair_json_text(text: str) -> str:
    """Fix common JSON typos from vision model output."""
    text = re.sub(r",(\s*[}\]])", r"\1", text)
    text = re.sub(
        r'([\{,\[]\s*|\n\s+)([A-Za-z_][A-Za-z0-9_]*)"\s*:',
        r'\1"\2":',
        text,
    )
    text = re.sub(
        r'([\{,\[]\s*|\n\s+)([A-Za-z_][A-Za-z0-9_]*)\s*:',
        r'\1"\2":',
        text,
    )
    return text


def extract_json_payload(text: str) -> dict[str, Any]:
    text = text.strip()
    if not text:
        raise ValueError("Empty model response")

    fence = re.search(r"```(?:json)?\s*([\s\S]*?)```", text)
    if fence:
        text = fence.group(1).strip()

    candidates = [text]
    start = text.find("{")
    end = text.rfind("}")
    if start >= 0 and end > start:
        candidates.append(text[start : end + 1])

    last_error: json.JSONDecodeError | None = None
    for candidate in candidates:
        for attempt in (candidate, repair_json_text(candidate)):
            try:
                data = json.loads(attempt)
            except json.JSONDecodeError as exc:
                last_error = exc
                continue
            if not isinstance(data, dict):
                raise ValueError(f"Expected JSON object, got {type(data).__name__}")
            return data

    if last_error is not None:
        raise ValueError(
            f"Invalid JSON from model at line {last_error.lineno}, col {last_error.colno}: "
            f"{last_error.msg}"
        ) from last_error
    raise ValueError("No JSON object found in model response")


def clamp_bbox(x: int, y: int, width: int, height: int, ui_w: int, ui_h: int) -> tuple[int, int, int, int]:
    width = max(1, width)
    height = max(1, height)
    x = max(0, min(x, max(0, ui_w - 1)))
    y = max(0, min(y, max(0, ui_h - 1)))
    width = min(width, ui_w - x)
    height = min(height, ui_h - y)
    return x, y, max(1, width), max(1, height)


def bbox_iou(a: dict[str, Any], b: dict[str, Any]) -> float:
    ax2, ay2 = a["x"] + a["width"], a["y"] + a["height"]
    bx2, by2 = b["x"] + b["width"], b["y"] + b["height"]
    ix1, iy1 = max(a["x"], b["x"]), max(a["y"], b["y"])
    ix2, iy2 = min(ax2, bx2), min(ay2, by2)
    if ix2 <= ix1 or iy2 <= iy1:
        return 0.0
    inter = (ix2 - ix1) * (iy2 - iy1)
    area_a = a["width"] * a["height"]
    area_b = b["width"] * b["height"]
    union = area_a + area_b - inter
    return inter / union if union else 0.0


def scale_bbox(item: dict[str, Any], scale: float) -> dict[str, Any]:
    if scale == 1.0:
        return item
    scaled = dict(item)
    for key in ("x", "y", "width", "height"):
        if key in scaled and scaled[key] is not None:
            scaled[key] = int(round(scaled[key] / scale))
    return scaled


def normalize_type(value: str | None) -> str:
    if not value:
        return "img"
    value = value.strip().lower()
    return value if value in VALID_TYPES else "img"


def make_annotation(
    *,
    ann_id: str,
    name: str,
    ann_type: str,
    x: int,
    y: int,
    width: int,
    height: int,
    ui_w: int,
    ui_h: int,
    parent_id: str | None = None,
    cut_image: str = "",
    confidence: float | None = None,
    source: str = "glm",
) -> dict[str, Any]:
    x, y, width, height = clamp_bbox(x, y, width, height, ui_w, ui_h)
    ann_type = normalize_type(ann_type)
    item = {
        "id": ann_id,
        "name": name or ann_id,
        "cut_image": cut_image,
        "type": ann_type,
        "color": TYPE_COLORS[ann_type],
        "x": x,
        "y": y,
        "width": width,
        "height": height,
        "matched": True,
        "parent_id": parent_id,
        "source": source,
    }
    if confidence is not None:
        item["confidence"] = round(float(confidence), 6)
    return item


def merge_glm_result(
    annotations: list[dict[str, Any]],
    glm_data: dict[str, Any],
    ui_size: tuple[int, int],
    scale: float,
) -> list[dict[str, Any]]:
    ui_w, ui_h = ui_size
    by_id = {ann["id"]: dict(ann) for ann in annotations if ann.get("id")}

    for update in glm_data.get("updates", []):
        ann_id = update.get("id")
        if not ann_id or ann_id not in by_id:
            continue
        target = by_id[ann_id]
        update = scale_bbox(update, scale)
        if "type" in update and update["type"]:
            target["type"] = normalize_type(update["type"])
            target["color"] = TYPE_COLORS[target["type"]]
        if update.get("parent_id") is not None or "parent_id" in update:
            target["parent_id"] = update.get("parent_id")
        for key in ("x", "y", "width", "height"):
            if key in update and update[key] is not None:
                target[key] = int(update[key])
        if any(k in update for k in ("x", "y", "width", "height")):
            x, y, w, h = clamp_bbox(
                int(target.get("x", 0)),
                int(target.get("y", 0)),
                int(target.get("width", 1)),
                int(target.get("height", 1)),
                ui_w,
                ui_h,
            )
            target.update({"x": x, "y": y, "width": w, "height": h, "matched": True})
            target["source"] = target.get("source", "glm")
            if update.get("confidence") is not None:
                target["confidence"] = float(update["confidence"])

    existing_boxes = [
        {
            "id": ann["id"],
            "x": ann["x"],
            "y": ann["y"],
            "width": ann["width"],
            "height": ann["height"],
        }
        for ann in by_id.values()
        if ann.get("matched") and ann.get("x") is not None
    ]

    used_ids = set(by_id)
    for raw in glm_data.get("new_annotations", []):
        raw = scale_bbox(raw, scale)
        ann_id = raw.get("id") or raw.get("name")
        if not ann_id:
            continue
        base_id = re.sub(r"[^a-zA-Z0-9_]+", "_", str(ann_id)).strip("_").lower() or "annotation"
        ann_id = base_id
        suffix = 2
        while ann_id in used_ids:
            ann_id = f"{base_id}_{suffix}"
            suffix += 1
        used_ids.add(ann_id)

        required = ("x", "y", "width", "height")
        if any(raw.get(k) is None for k in required):
            continue

        candidate = {
            "x": int(raw["x"]),
            "y": int(raw["y"]),
            "width": int(raw["width"]),
            "height": int(raw["height"]),
        }
        if any(bbox_iou(candidate, box) >= 0.5 for box in existing_boxes):
            continue

        ann = make_annotation(
            ann_id=ann_id,
            name=str(raw.get("name") or ann_id),
            ann_type=str(raw.get("type") or "img"),
            x=candidate["x"],
            y=candidate["y"],
            width=candidate["width"],
            height=candidate["height"],
            ui_w=ui_w,
            ui_h=ui_h,
            parent_id=raw.get("parent_id"),
            cut_image=str(raw.get("cut_image") or ""),
            confidence=raw.get("confidence"),
            source="glm",
        )
        by_id[ann_id] = ann
        existing_boxes.append(
            {"id": ann_id, "x": ann["x"], "y": ann["y"], "width": ann["width"], "height": ann["height"]}
        )

    # Drop invalid parent references and obvious cycles.
    for ann in by_id.values():
        parent_id = ann.get("parent_id")
        if parent_id and parent_id not in by_id:
            ann["parent_id"] = None

    result = list(by_id.values())
    result.sort(key=lambda item: (item.get("parent_id") is not None, item.get("id", "")))
    return result


def call_glm_vision(
    *,
    api_key: str,
    ui_image_b64: str,
    prompt: str,
    cut_images: list[dict[str, str]] | None = None,
    model: str = DEFAULT_MODEL,
    api_url: str = DEFAULT_API_URL,
    thinking: bool = False,
    timeout: int = 180,
) -> str:
    content: list[dict[str, Any]] = [
        {"type": "text", "text": "Full UI screenshot:"},
        {
            "type": "image_url",
            "image_url": {"url": f"data:image/jpeg;base64,{ui_image_b64}"},
        },
    ]
    for cut in cut_images or []:
        content.append(
            {
                "type": "text",
                "text": (
                    f"Artist cut asset id=\"{cut['id']}\" file=\"{cut['file']}\". "
                    "Use this as a visual reference; create an annotation if it is visible in the full screenshot."
                ),
            }
        )
        content.append(
            {
                "type": "image_url",
                "image_url": {"url": f"data:image/jpeg;base64,{cut['b64']}"},
            }
        )
    content.append({"type": "text", "text": prompt})

    payload = {
        "model": model,
        "messages": [
            {
                "role": "user",
                "content": content,
            }
        ],
        "temperature": 0.2,
    }
    if thinking:
        payload["thinking"] = {"type": "enabled"}

    request = urllib.request.Request(
        api_url,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"GLM API HTTP {exc.code}: {detail}") from exc

    choices = body.get("choices") or []
    if not choices:
        raise RuntimeError(f"GLM API returned no choices: {body}")
    message = choices[0].get("message") or {}
    content = message.get("content")
    if isinstance(content, list):
        content = "".join(part.get("text", "") for part in content if isinstance(part, dict))
    if not content:
        raise RuntimeError(f"GLM API empty content: {body}")
    return str(content)


def annotate_annotations_file(
    json_path: Path,
    *,
    api_key: str | None = None,
    model: str = DEFAULT_MODEL,
    api_url: str = DEFAULT_API_URL,
    max_long_edge: int = 2048,
    max_cut_long_edge: int = 768,
    include_cut_images: bool = True,
    thinking: bool = False,
    save_raw: bool = False,
) -> Path:
    json_path = Path(json_path)
    api_key = load_api_key(api_key)
    if not api_key:
        raise SystemExit("Missing API key. Set ZHIPU_API_KEY, pass --api-key, or create glm_api_key.local.")

    document = json.loads(json_path.read_text(encoding="utf-8"))
    annotations = list(document.get("annotations") or [])
    ui_path = resolve_ui_path(json_path, document)
    ui_size = (
        int(document.get("ui_size", {}).get("width") or 0),
        int(document.get("ui_size", {}).get("height") or 0),
    )
    if ui_size[0] <= 0 or ui_size[1] <= 0:
        with Image.open(ui_path) as img:
            ui_size = img.size

    image_b64, scale, orig_size = prepare_image_payload(ui_path, max_long_edge)
    source_dir = resolve_source_dir(json_path, document)
    cut_images: list[dict[str, str]] = []
    if include_cut_images:
        cut_images = collect_cut_image_payloads(
            document,
            source_dir,
            max_long_edge=max_cut_long_edge,
        )
    cut_ids = [cut["id"] for cut in cut_images]
    prompt = build_prompt(document, annotations, orig_size, cut_image_ids=cut_ids)
    print(f"calling {model} for {json_path} ...")
    if cut_images:
        print(f"  attaching {len(cut_images)} cut image(s): {', '.join(cut_ids)}")
    raw_text = call_glm_vision(
        api_key=api_key,
        ui_image_b64=image_b64,
        cut_images=cut_images,
        prompt=prompt,
        model=model,
        api_url=api_url,
        thinking=thinking,
    )

    if save_raw:
        raw_path = json_path.with_name(json_path.stem + ".glm_raw.txt")
        raw_path.write_text(raw_text, encoding="utf-8")
        print(f"  raw response: {raw_path}")

    glm_data = extract_json_payload(raw_text)
    merged = merge_glm_result(annotations, glm_data, orig_size, scale)

    document["annotations"] = merged
    document["coord_space"] = "ui_original"
    document["annotation_strategy"] = "glm_first"
    document["glm_annotated_at"] = datetime.now(timezone.utc).isoformat()
    document["glm_model"] = model
    json_path.write_text(json.dumps(document, ensure_ascii=False, indent=2), encoding="utf-8")

    added = len(merged) - len(annotations)
    with_parent = sum(1 for ann in merged if ann.get("parent_id"))
    print(f"  annotations: {len(annotations)} -> {len(merged)} (+{added}), with parent_id: {with_parent}")
    return json_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Annotate ui.jpg with GLM-5V-Turbo vision model.")
    parser.add_argument("json", nargs="?", help="Path to annotations.json")
    parser.add_argument("--json", dest="json_opt", help="Path to annotations.json")
    parser.add_argument("--api-key", default=None, help="Zhipu API key (or env ZHIPU_API_KEY)")
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--api-url", default=DEFAULT_API_URL)
    parser.add_argument("--max-long-edge", type=int, default=2048)
    parser.add_argument("--max-cut-long-edge", type=int, default=768, help="Max long edge for cut_pic uploads")
    parser.add_argument("--no-cut-images", action="store_true", help="Do not upload artist cut_pic images")
    parser.add_argument("--thinking", action="store_true", help="Enable GLM thinking mode")
    parser.add_argument("--save-raw", action="store_true", help="Save raw GLM response for debugging")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    json_path = args.json_opt or args.json
    if not json_path:
        raise SystemExit("Provide annotations.json path.")
    annotate_annotations_file(
        Path(json_path),
        api_key=args.api_key,
        model=args.model,
        api_url=args.api_url,
        max_long_edge=args.max_long_edge,
        max_cut_long_edge=args.max_cut_long_edge,
        include_cut_images=not args.no_cut_images,
        thinking=args.thinking,
        save_raw=args.save_raw,
    )


if __name__ == "__main__":
    main()
