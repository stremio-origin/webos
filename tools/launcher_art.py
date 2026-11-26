# launcher_art.py <assets dir> <out dir>: the webOS launcher's pictures for Stremio Origin from the app's own art (the
# logo: its play mark with ORIGIN under it): bgImage.png (1920x1080: the splash backdrop, the logo centred, as store
# apps' launcher images) launchSplash.png (the static webOS startup wallpaper) and imageForRecents.png (400x350, transparent: the logo, as the recents tile shows it)
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
# Compose the Origin mark on the app's 1920×1080 canvas.
launch = Image.new("RGBA", (1920, 1080), (12, 11, 17, 255))
for part, size, center_x, y in (
    (logo.crop((0, 0, 512, 400)), (227, 227), 960, 386),
    (logo.crop((0, 400, 512, 512)), (216, 29), 970, 684),
):
    part = part.crop(part.getbbox()).resize(size, Image.LANCZOS)
    launch.alpha_composite(part, (center_x - size[0] // 2, y))
launch.convert("RGB").save(f"{out}/launchSplash.png", optimize=True)
print("ok")
