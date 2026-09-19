from pathlib import Path

root = Path(__file__).resolve().parents[1] / "learn"
hist = root / "historico-bios"
banner = (
    "> Feito na era BIOS. Não refazer. Metal atual: "
    "[PASSO 01e](../PASSO_01e.md).\n"
)
for name in ("PASSO_00.md", "PASSO_01.md", "PASSO_01b.md", "PASSO_01c.md"):
    src = (root / name).read_text(encoding="utf-8")
    lines = src.splitlines()
    while lines and (lines[0].startswith(">") or lines[0].strip() == ""):
        lines.pop(0)
    body = "\n".join(lines) + "\n"
    (hist / name).write_text(banner + body, encoding="utf-8", newline="\n")
    print("wrote", name)
