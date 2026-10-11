# route.py ROUTE OUT.png: records h2bench.dem on the PC build from a route
# ("forward 60, right 20, forward+attack 40, ...", counts in 20 Hz ticks)
# and saves a contact sheet of every 32nd frame of its playback.
import os, shutil, subprocess, sys
from PIL import Image

route, sheet_path = sys.argv[1], sys.argv[2]
home = os.path.expanduser("~/.hexen2demo")
exe = os.path.expanduser("~/mc1-work/h2/mc1-hexen2/engine/hexen2/hexen2")
base = os.path.expanduser("~/mc1-work/h2/data")

lines = ["host_framerate 0.05", "record h2bench demo1"] + ["wait"] * 40
for step in route.split(","):
    keys, n = step.split()
    keys = keys.split("+")
    lines += ["+" + k for k in keys] + ["wait"] * int(n) + ["-" + k for k in keys]
lines += ["wait"] * 10 + ["stop", "toggleconsole", "quit"]
open(f"{home}/data1/rec.cfg", "w").write("\n".join(lines) + "\n")

env = dict(os.environ, SDL_VIDEODRIVER="dummy")
args = [exe, "-basedir", base, "-nosound", "-width", "320", "-height", "240"]
subprocess.run(args + ["+exec", "rec.cfg"], env=env, capture_output=True, timeout=120)
shutil.rmtree(f"{home}/frames", ignore_errors=True)
out = subprocess.run(args + ["-tdquit", "+scr_conspeed", "100000", "+vid_dumpframes", "5000", "+timedemo", "h2bench"],
                     env=env, capture_output=True, text=True, timeout=120).stdout
print([l for l in out.splitlines() if "frames" in l])

files = sorted(os.listdir(f"{home}/frames"))[20:]
picks = files[::32]
cols = 6
rows = (len(picks) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 160, rows * 120))
for i, f in enumerate(picks):
    data = open(f"{home}/frames/{f}", "rb").read()
    im = Image.frombytes("P", (320, 240), data[:76800])
    im.putpalette(data[76800:76800 + 768])
    sheet.paste(im.convert("RGB").resize((160, 120)), ((i % cols) * 160, (i // cols) * 120))
sheet.save(sheet_path)
