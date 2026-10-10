import sys
from PIL import Image, ImageDraw, ImageFont

BODY = [
    "...HBBBBBBBBB...",
    "...BBBBBBBBB....",
    "..BBBEBBBEBBB...",
    "...BBEBBBEBB....",
    "...SSSSSSSSS....",
    "...S.S...S.S....",
    "...S.S...S.S....",
    "...S.S...S.S....",
    "................",
    "................",
]

HEAD = {
    "flame": [
        ".......Y........",
        "......YF...Y....",
        ".....YFF..YF....",
        "....YFFLFYFF....",
        "....FFLLLLFF....",
        "...FFLLLLLLFF...",
    ],
    "drop": [
        "..........L.....",
        ".........LL.....",
        "........LAL.....",
        ".......LAAL.....",
        "....LLLAAL......",
        "...LAAAAALL.....",
    ],
    "sprout": [
        "................",
        ".........AA.....",
        "....AA..AAA.....",
        "....AAA.A.......",
        "......AAA.......",
        ".......A........",
    ],
    "crown": [
        "................",
        "....A..A..A.....",
        "....AA.A.AA.....",
        "....AAAAAAA.....",
        "....AHAHAHA.....",
        "....AAAAAAA.....",
    ],
    "helmet": [
        "................",
        ".......A........",
        ".....AAAAA......",
        "....AAHAAAA.....",
        "...AAAAAAAAA....",
        "...A.A.A.A.A....",
    ],
    "hood": [
        "................",
        "................",
        "......AAAA......",
        ".....AALLAA.....",
        "....AALAAAAA....",
        "...AAAAAAAAAA...",
    ],
    "horns": [
        "................",
        "...A.......A....",
        "...AA.....AA....",
        "....AA.L.AA.....",
        ".....AALAA......",
        "....AAAAAAA.....",
    ],
    "antenna": [
        ".......L........",
        "......LLL.......",
        ".......A........",
        ".......A........",
        ".....AAAAA......",
        "................",
    ],
}

PAL = {
    # name: outline, body, shade, highlight, eye, accent, accent-light
    "terracotta": ("#5A2A18", "#D97757", "#B85C3D", "#F4B89C", "#1F1E1D", "#F2C14E", "#FFE9A8"),
    "charcoal":   ("#121110", "#3D3A36", "#2A2825", "#6B6660", "#F0A27E", "#C9A227", "#E8D27A"),
    "fire":       ("#4A1408", "#E8562A", "#B8361A", "#FFB13B", "#2A0E06", "#FF7A1F", "#FFE066"),
    "water":      ("#0B2747", "#2FA8FF", "#1A72C4", "#C8F2FF", "#0B1F33", "#2FA8FF", "#9BE7FF"),
    "moss":       ("#1E2A0E", "#7FA34A", "#5C7A2E", "#C6E08A", "#16200A", "#4FBF3A", "#B8F07A"),
    "sand":       ("#4A3A1E", "#D8C08A", "#B39A62", "#F4E6BE", "#2A2014", "#8C6A3A", "#B08850"),
    "obsidian":   ("#120A1E", "#3B2A5A", "#2A1D42", "#6E58A0", "#FF4FD8", "#E8C25A", "#FF6FE0"),
    "stone":      ("#1A1C12", "#8A8F70", "#656A50", "#C8CCAE", "#1A1C12", "#B9B79C", "#E8E6CF"),
}


def build(head, pal):
    o, b, s, h, e, a, l = pal
    cmap = {"B": b, "S": s, "H": h, "E": e, "O": o, "A": a, "F": a, "L": l, "Y": l}
    rows = HEAD[head] + BODY
    px = {}
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                px[(x, y)] = cmap[ch]
    # 1 px outline around silhouette (4-neighbour)
    filled = set(px)
    for (x, y) in list(filled):
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            q = (x + dx, y + dy)
            if q not in filled and 0 <= q[0] < 16 and 0 <= q[1] < 16:
                px.setdefault(q, o)
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    for (x, y), c in px.items():
        img.putpixel((x, y), Image.new("RGB", (1, 1), c).getpixel((0, 0)) + (255,))
    return img


def sheet(items, title, path, scale=12):
    cell = 16 * scale
    pad = 28
    w = len(items) * (cell + pad) + pad
    hgt = cell + 110
    out = Image.new("RGB", (w, hgt), (43, 47, 30))
    d = ImageDraw.Draw(out)
    try:
        f = ImageFont.truetype("/home/lamter/caro-game/assets/fonts/VT323-Regular.ttf", 30)
        ft = ImageFont.truetype("/home/lamter/caro-game/assets/fonts/VT323-Regular.ttf", 36)
    except Exception:
        f = ft = ImageFont.load_default()
    d.text((pad, 10), title, fill=(232, 194, 90), font=ft)
    for i, (label, img) in enumerate(items):
        x = pad + i * (cell + pad)
        y = 56
        d.rectangle([x - 4, y - 4, x + cell + 3, y + cell + 3], fill=(61, 67, 41), outline=(86, 94, 58), width=3)
        out.paste(img.resize((cell, cell), Image.NEAREST), (x, y), img.resize((cell, cell), Image.NEAREST))
        d.text((x, y + cell + 10), label, fill=(244, 236, 208), font=f)
    out.save(path)


if __name__ == "__main__":
    outdir = sys.argv[1]
    sheet([
        ("Easy: Novice", build("hood", PAL["sand"])),
        ("Medium: Automaton", build("antenna", PAL["charcoal"])),
        ("Hard: Obsidian Warden", build("horns", PAL["obsidian"])),
    ], "Computer champions by difficulty", f"{outdir}/champions.png", scale=12)
    print("ok")
