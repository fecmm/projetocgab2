"""Gera as texturas de arquivo usadas na cena (assets/tapete.png, quadro1.png, quadro2.png).

Uso (na raiz do projeto):  python tools/gerar_texturas.py
Requer: pip install pillow numpy
"""
import os
import numpy as np
from PIL import Image

rng = np.random.default_rng(7)
SAIDA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")
os.makedirs(SAIDA, exist_ok=True)


def salvar(a, nome):
    Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).save(os.path.join(SAIDA, nome))


def mistura(a, b, t):
    t = np.clip(t, 0, 1)[..., None]
    return np.array(a, float) * (1 - t) + np.array(b, float) * t


# ---------- tapete (512x368): campo vinho, losangos e bordas ----------
W, H = 512, 368
y, x = np.mgrid[0:H, 0:W].astype(float)
u, v = x / W, y / H
d = np.minimum.reduce([u, 1 - u, v, 1 - v])
img = np.zeros((H, W, 3)); img[:] = (150, 45, 50)
cx, cy = (u - 0.5) * W, (v - 0.5) * H
diam = (np.abs(cx % 64 - 32) + np.abs(cy % 64 - 32)) / 32
img = mistura(img, (205, 150, 95), (diam < 0.55).astype(float) * 0.85)
img = mistura(img, (110, 30, 38), ((diam > 0.55) & (diam < 0.7)).astype(float) * 0.9)
img = np.where((d < 0.085)[..., None], np.array((120, 35, 42)), img)
img = np.where(((d > 0.040) & (d < 0.050))[..., None], np.array((235, 215, 170)), img)
img = np.where(((d > 0.065) & (d < 0.072))[..., None], np.array((235, 215, 170)), img)
img = np.where((d < 0.012)[..., None], np.array((235, 215, 170)), img)
fio = 0.93 + 0.07 * np.sin(x * 1.6) * np.sin(y * 1.6)
img = img * fio[..., None] * (0.94 + 0.06 * rng.random((H, W, 1)))
salvar(img, "tapete.png")

# ---------- quadro1: paisagem com lago (512x384) ----------
W, H = 512, 384
y, x = np.mgrid[0:H, 0:W].astype(float)
v = y / H
img = mistura((110, 170, 235), (255, 215, 170), (v - 0.2) / 0.5 * np.ones((H, W)))
sx, sy = 370, 120
sol = ((x - sx) ** 2 + (y - sy) ** 2) < 38 ** 2
brilho = np.exp(-(((x - sx) ** 2 + (y - sy) ** 2) / (2 * 90 ** 2)))
img = img + brilho[..., None] * np.array((60, 40, 0))
img[sol] = (255, 245, 190)
m1 = 230 + 28 * np.sin(x * 0.012 + 1.0) + 14 * np.sin(x * 0.031 + 2.0)
m2 = 262 + 22 * np.sin(x * 0.017 + 3.0) + 11 * np.sin(x * 0.045 + 6.0)
img[y > m1] = (95, 110, 150)
img[y > m2] = (60, 90, 80)
lago = y > 300
img[lago] = mistura((70, 130, 170), (230, 200, 160), 0.15 + 0.2 * np.sin(y[lago] * 0.6 + x[lago] * 0.02))
img[(y > m2 + 70) & ~lago] = (45, 110, 55)
img[y > 345] = (35, 85, 45)
img = img * (0.95 + 0.05 * rng.random((H, W, 1)))
salvar(img, "quadro1.png")

# ---------- quadro2: por do sol no mar (512x384) ----------
y, x = np.mgrid[0:H, 0:W].astype(float)
v = y / H
hor = 0.58
ceu = mistura((70, 40, 110), (255, 120, 70), v / hor)
ceu = mistura(ceu, (255, 200, 110), (v / hor - 0.75) * 4)
img = ceu.copy()
sx, sy = 256, int(H * hor)
img[(((x - sx) ** 2 + (y - sy) ** 2) < 70 ** 2) & (y < sy)] = (255, 235, 160)
mar = y >= sy
t = np.clip((y - sy) / (H - sy), 0, 1)
base = mistura((230, 130, 80), (25, 40, 95), t)
ondas = 0.5 + 0.5 * np.sin(y * 0.55 + np.sin(x * 0.04) * 2.0)
reflexo = np.exp(-((x - sx) ** 2) / (2 * (30 + 90 * t) ** 2)) * ondas
base = base + reflexo[..., None] * np.array((190, 120, 40)) * (1 - t[..., None] * 0.6)
img[mar] = base[mar]
img = img * (0.95 + 0.05 * rng.random((H, W, 1)))
salvar(img, "quadro2.png")
print("Texturas geradas em", os.path.abspath(SAIDA))
