import subprocess, numpy as np, glob
from PIL import Image, ImageDraw, ImageFont
font = ImageFont.truetype("/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf", 22)
def render(svg, px, fg):
    subprocess.run(["rsvg-convert", "-w", str(px), "-h", str(px), "-o", "/tmp/r.png", svg], check=True)
    a = np.array(Image.open("/tmp/r.png").convert("RGBA"))[..., 3]
    img = np.zeros(a.shape + (4,), np.uint8); img[..., :3] = fg; img[..., 3] = a
    return Image.fromarray(img)
def on(bg, im):
    b = Image.new("RGBA", im.size, bg); return Image.alpha_composite(b, im).convert("RGB")
rows = [("old (AI image)", "/work/promo/logo/out/camel-mark.svg")] + [(n, "out/camel-%s.svg" % n) for n in ("a-graph", "b-cells", "c-monoline")]
CELL = 300; COLS = ["black on white", "white on black", "film navy", "32 px", "16 px", "16 px (x6)"]
S = Image.new("RGB", (220 + CELL*6 + 70, 50 + len(rows)*(CELL+20)), "white"); d = ImageDraw.Draw(S)
for j, c in enumerate(COLS): d.text((220 + j*(CELL+12), 12), c, fill=(80, 80, 80), font=font)
for i, (name, path) in enumerate(rows):
    y = 50 + i*(CELL+20); d.text((10, y + CELL//2 - 10), name, fill=(0, 0, 0), font=font)
    x = 220
    S.paste(on((255,255,255,255), render(path, CELL, (0,0,0))), (x, y)); x += CELL+12
    S.paste(on((0,0,0,255), render(path, CELL, (255,255,255))), (x, y)); x += CELL+12
    S.paste(on((41,50,90,255), render(path, CELL, (216,222,245))), (x, y)); x += CELL+12
    s32 = on((255,255,255,255), render(path, 32, (0,0,0))); S.paste(s32, (x + CELL//2 - 16, y + CELL//2 - 16)); x += CELL+12
    s16 = on((255,255,255,255), render(path, 16, (0,0,0))); S.paste(s16, (x + CELL//2 - 8, y + CELL//2 - 8)); x += CELL+12
    S.paste(s16.resize((96, 96), Image.NEAREST), (x + CELL//2 - 48, y + CELL//2 - 48))
S.save("out/compare.png")
