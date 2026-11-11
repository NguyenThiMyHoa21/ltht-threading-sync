import pathlib

root = pathlib.Path(__file__).resolve().parent.parent
html_bytes = (root / "web" / "index.html").read_bytes()

lines = [
    "#ifndef WEB_ASSETS_H",
    "#define WEB_ASSETS_H",
    "",
    "#include <stddef.h>",
    "",
    f"static const size_t WEB_INDEX_HTML_LEN = {len(html_bytes)};",
    "static const unsigned char WEB_INDEX_HTML[] = {",
]

for i in range(0, len(html_bytes), 16):
    chunk = html_bytes[i : i + 16]
    hex_items = ", ".join(f"0x{b:02x}" for b in chunk)
    lines.append(f"    {hex_items},")

lines.extend([
    "};",
    "",
    "#endif",
    "",
])

(root / "src" / "web_assets.h").write_text("\n".join(lines), encoding="utf-8")
print(f"Generated src/web_assets.h ({len(html_bytes)} bytes)")
