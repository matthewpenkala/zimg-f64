"""Check local Markdown links and reject unfinished release placeholders."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LINK = re.compile(r"\[[^\]]+\]\(([^)]+)\)")


def main() -> None:
    findings = []
    markdown_files = sorted(ROOT.glob("*.md")) + sorted(
        (ROOT / "docs").rglob("*.md")
    )
    for document in markdown_files:
        text = document.read_text(encoding="utf-8")
        for target in LINK.findall(text):
            target = target.strip().strip("<>")
            if (
                not target
                or target.startswith(("http://", "https://", "#", "mailto:"))
            ):
                continue
            path_part = target.split("#", 1)[0]
            resolved = (document.parent / path_part).resolve()
            if not resolved.exists():
                findings.append(
                    f"{document.relative_to(ROOT)}: missing local link {target}"
                )
    validation = (ROOT / "VALIDATION.md").read_text(encoding="utf-8")
    if "PENDING" in validation:
        findings.append("VALIDATION.md contains PENDING release gates")
    if findings:
        raise SystemExit("\n".join(findings))
    print(f"documentation: PASS ({len(markdown_files)} Markdown files)")


if __name__ == "__main__":
    main()
