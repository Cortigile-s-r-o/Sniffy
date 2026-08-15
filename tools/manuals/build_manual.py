#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
from datetime import datetime
import html
import mimetypes
import re
import struct
import sys
from pathlib import Path
from typing import Iterable

import markdown
from bs4 import BeautifulSoup


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CSS = Path(__file__).with_name("manual.css")
DISPLAY_MATH_RE = re.compile(r"\$\$\s*(?P<expr>.*?)\s*\$\$", re.DOTALL)
INLINE_MATH_RE = re.compile(r"(?<!\\)(?<!\$)\$(?!\$)(?P<expr>[^\n$]+?)(?<!\\)\$(?!\$)")
FENCED_CODE_BLOCK_RE = re.compile(r"(^```.*?^```[ \t]*$)", re.MULTILINE | re.DOTALL)
MANUAL_LAYOUT_CLASSES = {
    "manual-image--standard",
    "manual-image--compact",
    "manual-image--panel",
    "manual-image--snippet",
    "manual-image--wide",
}
MANUAL_LOCALIZATION = {
    "cs": {
        "html_lang": "cs",
        "header_title": "Uživatelský manuál",
    },
    "en": {
        "html_lang": "en",
        "header_title": "User Manual",
    },
}


def parse_toc_depth(value: str) -> int:
    depth = int(value)
    if depth < 2 or depth > 6:
        raise argparse.ArgumentTypeError("TOC depth must be between 2 and 6.")
    return depth


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build a PDF user manual from Markdown using HTML/CSS rendering."
    )
    parser.add_argument(
        "--input",
        required=True,
        type=Path,
        help="Path to the Markdown source file.",
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="Path to the generated PDF file.",
    )
    parser.add_argument(
        "--title",
        default="",
        help="Optional document title override.",
    )
    parser.add_argument(
        "--language",
        choices=sorted(MANUAL_LOCALIZATION),
        default=None,
        help="Optional manual language override. Defaults to inferring it from the input file name.",
    )
    parser.add_argument(
        "--asset-root",
        action="append",
        type=Path,
        default=[],
        help=(
            "Allowed root for local assets. Can be passed multiple times. "
            "Defaults to the input file directory."
        ),
    )
    parser.add_argument(
        "--css",
        type=Path,
        default=DEFAULT_CSS,
        help="Path to the CSS file used for HTML/PDF rendering.",
    )
    parser.add_argument(
        "--toc-depth",
        type=parse_toc_depth,
        default=3,
        help=(
            "Maximum heading depth included in the generated table of contents. "
            "Depth 3 includes ## and ### headings."
        ),
    )
    parser.add_argument(
        "--html-output",
        type=Path,
        default=None,
        help="Optional path for the intermediate rendered HTML document.",
    )
    parser.add_argument(
        "--html-only",
        action="store_true",
        help="Render and validate HTML without generating a PDF.",
    )
    return parser.parse_args()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def find_title(markdown_text: str, fallback: Path) -> str:
    for line in markdown_text.splitlines():
        stripped = line.strip()
        if stripped.startswith("# "):
            return stripped[2:].strip()
    return fallback.stem


def build_generated_at_label() -> str:
    return datetime.now().strftime("%d.%m.%y")


def infer_manual_language(input_path: Path) -> str:
    stem = input_path.stem.lower()
    if stem.endswith("_en"):
        return "en"
    if stem.endswith("_cs"):
        return "cs"
    return "cs"


def read_png_dimensions(asset_path: Path) -> tuple[int | None, int | None]:
    raw_bytes = asset_path.read_bytes()
    if raw_bytes.startswith(b"\x89PNG\r\n\x1a\n") and len(raw_bytes) >= 24:
        width, height = struct.unpack(">II", raw_bytes[16:24])
        return width, height

    return None, None


def classify_image_dimensions(width: int, height: int) -> str:
    aspect_ratio = width / max(height, 1)
    area = width * height

    if height <= 160 or area <= 80_000 or (area <= 140_000 and aspect_ratio >= 2.2):
        return "manual-image--snippet"
    if aspect_ratio <= 0.85 and area <= 360_000:
        return "manual-image--panel"
    if aspect_ratio >= 2.35 and area >= 350_000:
        return "manual-image--wide"
    if area <= 750_000:
        return "manual-image--compact"
    return "manual-image--standard"


def dedupe_preserving_order(items: list[str]) -> list[str]:
    deduped: list[str] = []
    for item in items:
        if item and item not in deduped:
            deduped.append(item)
    return deduped


def resolve_image_classes(existing_classes: list[str], width: int | None, height: int | None) -> list[str]:
    classes = list(existing_classes)
    if "manual-image" not in classes:
        classes.append("manual-image")

    if not any(class_name in MANUAL_LAYOUT_CLASSES for class_name in classes):
        if width is not None and height is not None:
            classes.append(classify_image_dimensions(width, height))
        else:
            classes.append("manual-image--standard")

    return dedupe_preserving_order(classes)


def convert_latex_to_mathml(expression: str, *, block: bool) -> str:
    try:
        from latex2mathml.converter import convert as latex_to_mathml
    except ImportError as exc:
        raise RuntimeError(
            "latex2mathml is not installed. Run 'python -m pip install -r tools/manuals/requirements.txt'."
        ) from exc

    rendered = latex_to_mathml(expression.strip())
    if block:
        if 'display="' in rendered:
            rendered = re.sub(r'display="[^"]*"', 'display="block"', rendered, count=1)
        else:
            rendered = re.sub(r"<math\b", '<math display="block"', rendered, count=1)
        return f'<div class="manual-math manual-math--block">{rendered}</div>'

    return f'<span class="manual-math manual-math--inline">{rendered}</span>'


def replace_math_expressions(markdown_text: str) -> str:
    parts = FENCED_CODE_BLOCK_RE.split(markdown_text)
    processed_parts: list[str] = []

    for part in parts:
        if part.startswith("```"):
            processed_parts.append(part)
            continue

        part = DISPLAY_MATH_RE.sub(
            lambda match: "\n\n" + convert_latex_to_mathml(match.group("expr"), block=True) + "\n\n",
            part,
        )
        part = INLINE_MATH_RE.sub(
            lambda match: convert_latex_to_mathml(match.group("expr"), block=False),
            part,
        )
        processed_parts.append(part)

    return "".join(processed_parts)


def render_markdown(markdown_text: str, toc_depth: int) -> tuple[str, str]:
    extensions = [
        "extra",
        "sane_lists",
        "tables",
        "toc",
        "md_in_html",
    ]
    renderer = markdown.Markdown(
        extensions=extensions,
        extension_configs={"toc": {"toc_depth": f"2-{toc_depth}"}},
        output_format="html5",
    )
    return renderer.convert(markdown_text), getattr(renderer, "toc", "")


def is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def normalize_roots(input_path: Path, roots: Iterable[Path]) -> list[Path]:
    resolved = [root.resolve() for root in roots] if roots else [input_path.parent.resolve()]
    return resolved


def to_data_uri(asset_path: Path) -> str:
    mime_type, _ = mimetypes.guess_type(asset_path.name)
    if mime_type is None:
        mime_type = "application/octet-stream"
    encoded = base64.b64encode(asset_path.read_bytes()).decode("ascii")
    return f"data:{mime_type};base64,{encoded}"


def is_standalone_image_paragraph(image) -> bool:
    parent = image.parent
    if parent is None or parent.name != "p":
        return False
    return all(child is image or (isinstance(child, str) and not child.strip()) for child in parent.contents)


def inline_local_images(html_text: str, document_dir: Path, asset_roots: list[Path]) -> str:
    soup = BeautifulSoup(html_text, "html.parser")
    missing_assets: list[str] = []

    for image in soup.find_all("img"):
        src = (image.get("src") or "").strip()
        if not src or src.startswith("data:"):
            continue

        if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", src):
            raise ValueError(f"Remote asset references are not allowed in manuals: {src}")

        resolved = (document_dir / src).resolve()
        if not resolved.exists() or not resolved.is_file():
            missing_assets.append(src)
            continue

        if not any(is_relative_to(resolved, root) for root in asset_roots):
            allowed = ", ".join(str(root) for root in asset_roots)
            raise ValueError(
                f"Asset '{src}' resolves outside the allowed roots ({allowed})."
            )

        width, height = read_png_dimensions(resolved)
        image["class"] = resolve_image_classes(list(image.get("class", [])), width, height)
        if width is not None:
            image["width"] = str(width)
        if height is not None:
            image["height"] = str(height)
        image["src"] = to_data_uri(resolved)

        alt_text = image.get("alt", "").strip()
        if alt_text:
            figure = soup.new_tag("figure", **{"class": "manual-figure"})
            caption = soup.new_tag("figcaption", **{"class": "manual-caption"})
            caption.string = alt_text
            if is_standalone_image_paragraph(image):
                paragraph = image.parent
                image.extract()
                figure.append(image)
                figure.append(caption)
                paragraph.replace_with(figure)
            else:
                image.wrap(figure)
                figure.append(caption)


    if missing_assets:
        missing_list = ", ".join(sorted(missing_assets))
        raise FileNotFoundError(f"Missing manual assets: {missing_list}")

    return str(soup)


def insert_table_of_contents(body_html: str, toc_html: str) -> str:
    if not toc_html.strip():
        return body_html

    body_soup = BeautifulSoup(body_html, "html.parser")
    toc_soup = BeautifulSoup(
        (
            '<section class="manual-toc" role="doc-toc">'
            '<h2 class="manual-toc__title">Obsah</h2>'
            "</section>"
        ),
        "html.parser",
    )
    toc_section = toc_soup.section

    toc_fragment = BeautifulSoup(toc_html, "html.parser")
    toc_root = toc_fragment.find(class_="toc")
    if toc_root is None:
        return body_html

    toc_root["class"] = [*toc_root.get("class", []), "manual-toc__list"]
    toc_section.append(toc_root)

    # Najdeme ideálně první kapitolu (h2) začínající "1. " pro vložení TOC před ni (aby TOC bylo po Front Matter)
    first_numbered_h2 = body_soup.find("h2", string=re.compile(r"^\s*1\.\s+"))
    if first_numbered_h2 is not None:
        first_numbered_h2.insert_before(toc_section)
        return str(body_soup)

    title_heading = body_soup.find("h1")
    if title_heading is not None:
        title_heading.insert_after(toc_section)
        return str(body_soup)

    first_element = next((child for child in body_soup.contents if getattr(child, "name", None)), None)
    if first_element is not None:
        first_element.insert_before(toc_section)
    else:
        body_soup.append(toc_section)

    return str(body_soup)


def build_html_document(
    title: str,
    body_html: str,
    css_text: str,
    generated_at_label: str,
    header_logo_uri: str,
    html_lang: str,
    header_title: str,
) -> str:
    escaped_title = html.escape(title)
    escaped_generated_at = html.escape(generated_at_label)
    escaped_header_logo_uri = html.escape(header_logo_uri, quote=True)
    escaped_header_title = html.escape(header_title)
    return f"""<!DOCTYPE html>
<html lang=\"{html_lang}\">
<head>
    <meta charset=\"utf-8\" />
    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />
    <title>{escaped_title}</title>
    <style>
{css_text}
    </style>
</head>
<body>
    <header class=\"manual-page-header\" aria-hidden=\"true\">
        <div class=\"manual-page-header__row\">
            <img class=\"manual-page-header__logo\" src=\"{escaped_header_logo_uri}\" alt=\"\" />
            <div class=\"manual-page-header__title\">{escaped_header_title}</div>
        </div>
        <div class=\"manual-page-header__rule\"></div>
    </header>
    <table class=\"manual-layout-table\">
        <thead>
            <tr>
                <td>
                    <div class=\"manual-header-spacer\"></div>
                </td>
            </tr>
        </thead>
        <tbody>
            <tr>
                <td>
                    <main class=\"manual\">
                        <p class=\"manual-build-meta\">{escaped_generated_at}</p>
{body_html}
                    </main>
                </td>
            </tr>
        </tbody>
    </table>
</body>
</html>
"""


def write_pdf(html_text: str, output_path: Path, base_url: str) -> None:
    try:
        from weasyprint import HTML
    except ImportError as exc:
        raise RuntimeError(
            "WeasyPrint is not installed. Run 'python -m pip install -r tools/manuals/requirements.txt'."
        ) from exc

    output_path.parent.mkdir(parents=True, exist_ok=True)
    HTML(string=html_text, base_url=base_url).write_pdf(str(output_path))


def main() -> int:
    args = parse_args()

    input_path = args.input.resolve()
    output_path = args.output.resolve()
    css_path = args.css.resolve()

    if not input_path.exists():
        raise FileNotFoundError(f"Manual source does not exist: {input_path}")
    if not css_path.exists():
        raise FileNotFoundError(f"CSS file does not exist: {css_path}")

    markdown_text = read_text(input_path)
    title = args.title.strip() or find_title(markdown_text, input_path)
    language = args.language or infer_manual_language(input_path)
    localization = MANUAL_LOCALIZATION[language]
    markdown_text = replace_math_expressions(markdown_text)
    asset_roots = normalize_roots(input_path, args.asset_root)
    generated_at_label = build_generated_at_label()
    header_logo_uri = to_data_uri(REPO_ROOT / "sniffy" / "graphics" / "logo_color.png")

    body_html, toc_html = render_markdown(markdown_text, args.toc_depth)
    body_html = inline_local_images(body_html, input_path.parent, asset_roots)
    body_html = insert_table_of_contents(body_html, toc_html)
    html_text = build_html_document(
        title,
        body_html,
        read_text(css_path),
        generated_at_label,
        header_logo_uri,
        localization["html_lang"],
        localization["header_title"],
    )

    if args.html_output is not None:
        html_output_path = args.html_output.resolve()
        html_output_path.parent.mkdir(parents=True, exist_ok=True)
        html_output_path.write_text(html_text, encoding="utf-8")

    if args.html_only:
        if args.html_output is not None:
            print(f"Built manual HTML: {args.html_output.resolve()}")
        else:
            print("Built manual HTML in memory.")
        return 0

    write_pdf(html_text, output_path, input_path.parent.as_uri())

    print(f"Built manual PDF: {output_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise