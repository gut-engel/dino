#!/usr/bin/env python3
"""Build the Dino VS Code extension (.vsix) without node/vsce.

A .vsix is a ZIP archive with a fixed layout:
  [Content_Types].xml      (OPC content types - REQUIRED name)
  extension.vsixmanifest   (package manifest)
  extension/...            (the extension files, package.json included)

Run:  python3 vscode-dino/vsix/build_vsix.py        # build the vsix
      python3 vscode-dino/vsix/build_vsix.py --print-name   # just print the filename
"""

import json
import os
import sys
import zipfile
import xml.sax.saxutils as sax

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXT_DIR = os.path.join(ROOT, "vscode-dino")
VSIX_DIR = os.path.join(EXT_DIR, "vsix")
OUT_DIR = ROOT

EXTENSION_FILES = [
    "package.json",
    "language-configuration.json",
    "README.md",
    "CHANGELOG.md",
    os.path.join("src", "extension.js"),
    os.path.join("syntaxes", "dino.tmLanguage.json"),
    os.path.join("icons", "icon.png"),
    os.path.join("icons", "thumbnail.svg"),
    os.path.join("fileicons", "dino-icon-theme.json"),
    os.path.join("fileicons", "images", "file-dark.svg"),
    os.path.join("fileicons", "images", "file-light.svg"),
    os.path.join("fileicons", "images", "folder-dark.svg"),
    os.path.join("fileicons", "images", "folder-light.svg"),
    os.path.join("fileicons", "images", "folder-open-dark.svg"),
    os.path.join("fileicons", "images", "folder-open-light.svg"),
]

# Fixed timestamp so builds are reproducible.
ZIP_TIME = (2026, 1, 1, 0, 0, 0)


def load_package_json():
    with open(os.path.join(EXT_DIR, "package.json"), encoding="utf-8") as f:
        return json.load(f)


def vsix_filename(pkg):
    return "{}-{}.vsix".format(pkg["name"], pkg["version"])


def render_manifest(pkg):
    with open(os.path.join(VSIX_DIR, "extension.vsixmanifest"), encoding="utf-8") as f:
        template = f.read()
    subs = {
        "@PUBLISHER@": pkg["publisher"],
        "@NAME@": pkg["name"],
        "@VERSION@": pkg["version"],
        "@DISPLAY_NAME@": sax.escape(pkg["displayName"]),
        "@DESCRIPTION@": sax.escape(pkg["description"]),
        "@TAGS@": " ".join(pkg.get("keywords", [])),
    }
    for key, value in subs.items():
        template = template.replace(key, value)
    return template


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--print-name":
        print(vsix_filename(load_package_json()))
        return 0

    pkg = load_package_json()
    out_path = os.path.join(OUT_DIR, vsix_filename(pkg))

    # extension/... files (read as bytes so binary assets such as the PNG
    # extension icon are packaged untouched)
    entries = {}
    for rel in EXTENSION_FILES:
        with open(os.path.join(EXT_DIR, rel), "rb") as f:
            entries["extension/" + rel] = f.read()

    # required OPC-level files
    entries["[Content_Types].xml"] = open(
        os.path.join(VSIX_DIR, "content-types.xml"), encoding="utf-8"
    ).read()
    entries["extension.vsixmanifest"] = render_manifest(pkg)

    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for name in sorted(entries):
            info = zipfile.ZipInfo(name, date_time=ZIP_TIME)
            info.external_attr = 0o644 << 16
            zf.writestr(info, entries[name])

    with zipfile.ZipFile(out_path) as zf:
        corrupt = zf.testzip()
    if corrupt:
        print("ERROR: corrupt entry in {}: {}".format(out_path, corrupt), file=sys.stderr)
        return 1

    print("Built {} ({} files)".format(out_path, len(entries)))
    return 0


if __name__ == "__main__":
    sys.exit(main())