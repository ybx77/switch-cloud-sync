from PIL import Image, ImageDraw

S = 256
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

# vertical gradient: deep blue -> teal
grad = Image.new("RGB", (S, S))
g = grad.load()
for y in range(S):
    t = y / (S - 1)
    r = int(24 + (12 - 24) * t)
    gg = int(56 + (168 - 56) * t)
    b = int(140 + (162 - 140) * t)
    for x in range(S):
        g[x, y] = (r, gg, b)

mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=56, fill=255)
img.paste(grad, (0, 0), mask)

d = ImageDraw.Draw(img)

# soft highlight
glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ImageDraw.Draw(glow).ellipse([150, -70, 350, 130], fill=(255, 255, 255, 26))
img.alpha_composite(glow)

white = (246, 250, 255, 255)
# cloud
d.ellipse([52, 128, 128, 204], fill=white)
d.ellipse([92, 104, 176, 188], fill=white)
d.ellipse([140, 128, 202, 190], fill=white)
d.rounded_rectangle([92, 160, 176, 204], radius=20, fill=white)

# sync arrows inside the cloud (green / white)
accent = (28, 116, 96, 255)
accent2 = (53, 196, 138, 255)
cx, cy, r = 132, 163, 26
d.arc([cx - r, cy - r, cx + r, cy + r], start=200, end=340, fill=accent, width=9)
d.arc([cx - r, cy - r, cx + r, cy + r], start=20, end=160, fill=accent, width=9)

# arrow heads
d.polygon([(cx + 14, cy - 22), (cx + 30, cy - 12), (cx + 12, cy - 4)], fill=accent)
d.polygon([(cx - 14, cy + 22), (cx - 30, cy + 12), (cx - 12, cy + 4)], fill=accent)

# bottom accent bar
d.rounded_rectangle([28, 218, 228, 230], radius=6, fill=accent2)

img.convert("RGB").save("icon.jpg", quality=94, optimize=True)
print("wrote icon.jpg", img.size)
