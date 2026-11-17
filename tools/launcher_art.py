# launcher_art.py <assets dir> <out dir>: the webOS launcher's pictures for Stremio Origin from the app's own art (the
# logo: its play mark with ORIGIN under it): bgImage.png (1920x1080: the splash backdrop, the logo centred, as store
# apps' launcher images) and imageForRecents.png (400x350, transparent: the logo, as the recents tile shows it)
import sys
from PIL import Image
a, out = sys.argv[1], sys.argv[2]
logo = Image.open(f"{a}/logo.png").convert("RGBA")
bg = Image.open(f"{a}/splashbg.jpg").convert("RGB").resize((1920, 1080), Image.LANCZOS).convert("RGBA")
L = 460; bg.alpha_composite(logo.resize((L, L), Image.LANCZOS), (960 - L // 2, 540 - L // 2))
bg.convert("RGB").save(f"{out}/bgImage.png", optimize=True)
r = Image.new("RGBA", (400, 350), (0, 0, 0, 0)); L = 250
r.alpha_composite(logo.resize((L, L), Image.LANCZOS), (200 - L // 2, 175 - L // 2 - 10))
r.save(f"{out}/imageForRecents.png", optimize=True)
print("ok")
