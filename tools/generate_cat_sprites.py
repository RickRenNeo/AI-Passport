#!/usr/bin/env python3
"""Generate modern LABUBU-inspired 3D vinyl/plush LV_COLOR_FORMAT_I8 sprites
for the Cat Study Room.

Renders smooth-blended 3D SDF volumes with studio wrap lighting, warm
subsurface shadow bounce, Fresnel rim light, material-specific shading
(plush fur noise, matte vinyl, glazed ceramic, woodgrain, brushed gold),
and 2x/3x supersampled crisp glossy eyes/blush/details into Flash .rodata
using 256-entry RGBA LV_COLOR_FORMAT_I8 palettes (0 extra runtime heap).
"""

from __future__ import annotations

import math
import time
from pathlib import Path
from typing import Callable

from PIL import Image, ImageDraw, ImageFilter

REPO_ROOT = Path(__file__).resolve().parents[1]
OUT_C = REPO_ROOT / "assets" / "images" / "ecard_cat_sprites.c"
OUT_H = REPO_ROOT / "main" / "ecard_cat_sprites.h"
MAX_TOTAL_BYTES = 190 * 1024  # Safe margin inside 448 KB free ota_0 Flash

# ---------------------------------------------------------------------------
# Fast deterministic math & noise helpers
# ---------------------------------------------------------------------------

def clamp(v: float, lo: float = 0.0, hi: float = 1.0) -> float:
    return lo if v < lo else (hi if v > hi else v)


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def smoothstep(e0: float, e1: float, x: float) -> float:
    t = clamp((x - e0) / (e1 - e0))
    return t * t * (3.0 - 2.0 * t)


def smin(a: float, b: float, k: float) -> float:
    if k <= 0.0:
        return min(a, b)
    h = max(k - abs(a - b), 0.0) / k
    return min(a, b) - h * h * k * 0.25


def hash2(ix: int, iy: int) -> float:
    n = (ix * 374761393 + iy * 668265263) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def value_noise(x: float, y: float) -> float:
    ix = math.floor(x)
    iy = math.floor(y)
    fx = x - ix
    fy = y - iy
    ux = fx * fx * (3.0 - 2.0 * fx)
    uy = fy * fy * (3.0 - 2.0 * fy)
    a = hash2(ix, iy)
    b = hash2(ix + 1, iy)
    c = hash2(ix, iy + 1)
    d = hash2(ix + 1, iy + 1)
    return lerp(lerp(a, b, ux), lerp(c, d, ux), uy)


def fbm2(x: float, y: float) -> float:
    return value_noise(x, y) * 0.65 + value_noise(x * 2.13 + 11.7, y * 2.13 - 5.3) * 0.35


# ---------------------------------------------------------------------------
# 3D SDF Primitives
# ---------------------------------------------------------------------------

def sd_ellipsoid(x: float, y: float, z: float,
                 cx: float, cy: float, cz: float,
                 rx: float, ry: float, rz: float) -> float:
    kx = (x - cx) / rx
    ky = (y - cy) / ry
    kz = (z - cz) / rz
    k0 = math.sqrt(kx * kx + ky * ky + kz * kz)
    if k0 < 1e-6:
        return -min(rx, ry, rz)
    k1 = math.sqrt((kx / rx) ** 2 + (ky / ry) ** 2 + (kz / rz) ** 2)
    return k0 * (k0 - 1.0) / k1


def sd_rot_ellipsoid(x: float, y: float, z: float,
                     cx: float, cy: float, cz: float,
                     rx: float, ry: float, rz: float,
                     ang_rad: float) -> float:
    dx = x - cx
    dy = y - cy
    ca = math.cos(ang_rad)
    sa = math.sin(ang_rad)
    rxp = dx * ca + dy * sa
    ryp = -dx * sa + dy * ca
    return sd_ellipsoid(rxp, ryp, z - cz, 0.0, 0.0, 0.0, rx, ry, rz)


def sd_capsule(x: float, y: float, z: float,
               ax: float, ay: float, az: float,
               bx: float, by: float, bz: float,
               r: float) -> float:
    pax, pay, paz = x - ax, y - ay, z - az
    bax, bay, baz = bx - ax, by - ay, bz - az
    denom = bax * bax + bay * bay + baz * baz
    h = clamp((pax * bax + pay * bay + paz * baz) / denom) if denom > 1e-8 else 0.0
    dx = pax - bax * h
    dy = pay - bay * h
    dz = paz - baz * h
    return math.sqrt(dx * dx + dy * dy + dz * dz) - r


def sd_round_box(x: float, y: float, z: float,
                 cx: float, cy: float, cz: float,
                 hx: float, hy: float, hz: float,
                 r: float) -> float:
    qx = abs(x - cx) - hx
    qy = abs(y - cy) - hy
    qz = abs(z - cz) - hz
    ox = max(qx, 0.0)
    oy = max(qy, 0.0)
    oz = max(qz, 0.0)
    return math.sqrt(ox * ox + oy * oy + oz * oz) + min(max(qx, qy, qz), 0.0) - r


# ---------------------------------------------------------------------------
# Studio Lighting & Material Shader
# ---------------------------------------------------------------------------

# Warm upper-left studio key light + half-vector for Blinn-Phong
LX, LY, LZ = -0.40, -0.58, 0.71
_L_LEN = math.sqrt(LX * LX + LY * LY + LZ * LZ)
LX, LY, LZ = LX / _L_LEN, LY / _L_LEN, LZ / _L_LEN
HX, HY, HZ = LX, LY, LZ + 1.0
_H_LEN = math.sqrt(HX * HX + HY * HY + HZ * HZ)
HX, HY, HZ = HX / _H_LEN, HY / _H_LEN, HZ / _H_LEN

# Material dictionary:
# (lit_rgb, shadow_rgb, rim_rgb, spec_strength, shininess, mode)
MATERIALS: dict[str, tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int], float, float, str]] = {
    "plush_oat":   ((247, 217, 184), (194, 146, 114), (255, 245, 228), 0.04, 10.0, "plush"),
    "plush_cream": ((255, 249, 240), (222, 200, 182), (255, 255, 252), 0.03, 10.0, "plush"),
    "plush_pink":  ((255, 192, 206), (218, 128, 152), (255, 232, 240), 0.05, 12.0, "plush"),
    "plush_ear":   ((255, 202, 202), (226, 142, 148), (255, 236, 236), 0.04, 12.0, "plush"),
    "plush_cloud": ((253, 251, 255), (214, 204, 236), (255, 255, 255), 0.04, 12.0, "plush"),
    "plush_berry": ((242, 134, 166), (186, 74, 112),  (255, 214, 228), 0.08, 14.0, "plush"),
    "hoodie_sky":  ((168, 208, 242), (108, 152, 198), (232, 246, 255), 0.06, 12.0, "plush"),
    "vinyl_face":  ((255, 248, 239), (232, 206, 186), (255, 253, 250), 0.16, 22.0, "vinyl"),
    "vinyl_peach": ((255, 166, 158), (222, 104, 102), (255, 224, 218), 0.25, 26.0, "vinyl"),
    "vinyl_bear":  ((240, 190, 146), (186, 128, 86),  (255, 234, 208), 0.22, 26.0, "vinyl"),
    "vinyl_cat":   ((255, 252, 246), (222, 210, 202), (255, 255, 255), 0.34, 36.0, "ceramic"),
    "ceramic_wht": ((255, 252, 246), (218, 208, 198), (255, 255, 255), 0.42, 48.0, "ceramic"),
    "ceramic_pnk": ((255, 168, 188), (216, 104, 134), (255, 228, 236), 0.44, 50.0, "ceramic"),
    "ceramic_msh": ((255, 146, 114), (212, 82, 66),   (255, 220, 202), 0.48, 54.0, "ceramic"),
    "ceramic_mint":((178, 228, 202), (112, 174, 146), (234, 252, 242), 0.40, 44.0, "ceramic"),
    "ceramic_sky": ((162, 212, 250), (102, 156, 212), (230, 248, 255), 0.42, 46.0, "ceramic"),
    "terra_pot":   ((228, 144, 102), (168, 88, 58),   (250, 202, 174), 0.14, 18.0, "vinyl"),
    "succulent":   ((152, 218, 168), (82, 148, 104),  (222, 248, 226), 0.26, 30.0, "vinyl"),
    "succ_tip":    ((242, 148, 166), (172, 92, 118),  (255, 212, 222), 0.26, 30.0, "vinyl"),
    "wood_oak":    ((238, 190, 140), (182, 126, 78),  (255, 232, 202), 0.16, 22.0, "wood"),
    "wood_dark":   ((204, 146, 98),  (144, 90, 52),   (242, 204, 166), 0.14, 20.0, "wood"),
    "gold_mat":    ((255, 224, 116), (196, 134, 34),  (255, 248, 198), 0.56, 56.0, "gold"),
    "glow_cream":  ((255, 246, 206), (242, 206, 124), (255, 252, 232), 0.30, 32.0, "ceramic"),
    "moon_gold":   ((255, 234, 138), (232, 172, 58),  (255, 252, 210), 0.45, 48.0, "ceramic"),
    "red_cherry":  ((248, 86, 108),  (176, 36, 62),   (255, 196, 206), 0.62, 64.0, "ceramic"),
    "book_cover":  ((114, 166, 224), (68, 112, 172),  (210, 232, 255), 0.14, 18.0, "vinyl"),
    "book_page":   ((255, 251, 242), (226, 214, 198), (255, 255, 255), 0.08, 14.0, "vinyl"),
    "cat_cream_plush":   ((255, 249, 241), (224, 204, 186), (255, 255, 255), 0.04, 10.0, "plush"),
    "cat_caramel_plush": ((214, 164, 124), (150, 98, 66),   (255, 234, 210), 0.05, 12.0, "plush"),
    "outfit_denim":      ((136, 178, 222), (82, 122, 172),  (218, 238, 255), 0.08, 14.0, "plush"),
    "outfit_coral":      ((248, 128, 134), (186, 68, 82),   (255, 216, 220), 0.16, 20.0, "vinyl"),
    "ear_pink_soft":     ((255, 192, 198), (226, 134, 148), (255, 236, 240), 0.06, 12.0, "plush"),
    "nose_berry":        ((255, 146, 158), (212, 88, 106),  (255, 224, 230), 0.34, 34.0, "ceramic"),
}


def shade_material(mat_name: str, x: float, y: float, z: float,
                   nx: float, ny: float, nz: float,
                   ao: float, px_i: int, py_i: int,
                   extra_glow: float = 0.0) -> tuple[int, int, int]:
    lit, shd, rim_c, spec_s, shin, mode = MATERIALS[mat_name]

    # Soft studio wrap lighting
    ndotl = nx * LX + ny * LY + nz * LZ
    wrap = clamp((ndotl + 0.42) / 1.42)
    wrap = wrap * 0.85 + 0.15 * ao
    t = clamp(wrap * (0.65 + 0.35 * ao))

    r = lerp(shd[0], lit[0], t)
    g = lerp(shd[1], lit[1], t)
    b = lerp(shd[2], lit[2], t)

    # Material micro-texture
    if mode == "plush":
        n = fbm2(px_i * 0.42, py_i * 0.42) - 0.5
        grain = n * 24.0
        r += grain
        g += grain * 0.95
        b += grain * 0.90
    elif mode == "wood":
        w = math.sin(y * 28.0 + fbm2(px_i * 0.12, py_i * 0.25) * 4.5) * 8.0
        r += w
        g += w * 0.78
        b += w * 0.55

    # Studio rim light (top-right / edge halo)
    rim_dir = clamp(-nx * 0.35 - ny * 0.55 + 0.55)
    fresnel = ((1.0 - clamp(nz)) ** 2.3) * (0.45 + 0.55 * rim_dir)
    rim_amt = clamp(fresnel * 0.48)
    r = lerp(r, rim_c[0], rim_amt)
    g = lerp(g, rim_c[1], rim_amt)
    b = lerp(b, rim_c[2], rim_amt)

    # Soft specular
    ndoth = max(0.0, nx * HX + ny * HY + nz * HZ)
    spec = (ndoth ** shin) * spec_s * ao
    if mode == "gold":
        r += spec * 255.0
        g += spec * 238.0
        b += spec * 170.0
    else:
        r += spec * 255.0
        g += spec * 252.0
        b += spec * 246.0

    # Warm bottom bounce / lamp self-glow
    if extra_glow > 0.0:
        r = lerp(r, 255.0, extra_glow)
        g = lerp(g, 242.0, extra_glow)
        b = lerp(b, 192.0, extra_glow)

    return (
        int(clamp(r, 0.0, 255.0)),
        int(clamp(g, 0.0, 255.0)),
        int(clamp(b, 0.0, 255.0)),
    )


def render_sdf_scene(
    w: int,
    h: float,
    eval_fn: Callable[[float, float, float], tuple[float, str]],
    ssaa: int = 2,
    z_start: float = 0.75,
    z_min: float = -0.55,
    edge_px: float = 1.4,
    bbox: tuple[int, int, int, int] | None = None,
    glow_fn: Callable[[float, float, float], float] | None = None,
    fur_edge: bool = False,
) -> Image.Image:
    """Raymarch a 3D SDF scene at ssaa*w x ssaa*h and downsample with LANCZOS."""
    w2 = int(w * ssaa)
    h2 = int(h * ssaa)
    im = Image.new("RGBA", (w2, h2), (0, 0, 0, 0))
    px = im.load()

    scale = 2.0 / max(w2, h2)
    aa_dist = edge_px * scale
    eps = 0.008

    if bbox is None:
        x0, y0, x1, y1 = 0, 0, w2, h2
    else:
        x0 = max(0, bbox[0] * ssaa)
        y0 = max(0, bbox[1] * ssaa)
        x1 = min(w2, bbox[2] * ssaa)
        y1 = min(h2, bbox[3] * ssaa)

    cx_off = w2 * 0.5
    cy_off = h2 * 0.5

    for py_i in range(y0, y1):
        y = (py_i - cy_off) * scale
        for px_i in range(x0, x1):
            x = (px_i - cx_off) * scale
            z = z_start
            d = 10.0
            min_d = 10.0
            best_z = z
            mat = "vinyl_face"
            best_mat = mat
            for _ in range(22):
                d, mat = eval_fn(x, y, z)
                if d < min_d:
                    min_d = d
                    best_z = z
                    best_mat = mat
                if d < 0.002:
                    break
                z -= max(d, 0.004) * 0.92
                if z < z_min:
                    break

            hit = (d < 0.002) or (min_d <= 0.0)
            if hit or min_d < aa_dist:
                sz = z if hit else best_z
                smat = mat if hit else best_mat
                # 3D gradient normal
                dx1, _ = eval_fn(x + eps, y, sz)
                dx2, _ = eval_fn(x - eps, y, sz)
                dy1, _ = eval_fn(x, y + eps, sz)
                dy2, _ = eval_fn(x, y - eps, sz)
                dz1, _ = eval_fn(x, y, sz + eps)
                dz2, _ = eval_fn(x, y, sz - eps)
                nx = dx1 - dx2
                ny = dy1 - dy2
                nz = dz1 - dz2
                nl = math.sqrt(nx * nx + ny * ny + nz * nz) + 1e-8
                nx, ny, nz = nx / nl, ny / nl, nz / nl

                # Cheap 1-sample ambient occlusion along normal
                ao_d, _ = eval_fn(x + nx * 0.06, y + ny * 0.06, sz + nz * 0.06)
                ao = clamp(ao_d / 0.06, 0.25, 1.0)

                eg = glow_fn(x, y, sz) if glow_fn else 0.0
                r, g, b = shade_material(smat, x, y, sz, nx, ny, nz, ao, px_i, py_i, eg)
                if hit:
                    a = 255
                else:
                    cov = clamp((aa_dist - min_d) / aa_dist)
                    a = int(cov * 255.0)
                if a > 0:
                    px[px_i, py_i] = (r, g, b, a)

    if ssaa > 1:
        return im.resize((w, int(h)), Image.Resampling.LANCZOS)
    return im


def make_soft_shadow(w: int, h: int, cx: float, cy: float,
                     rx: float, ry: float, max_alpha: int = 62,
                     color: tuple[int, int, int] = (92, 62, 46)) -> Image.Image:
    """Create a smooth elliptical contact shadow."""
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    x0 = max(0, int(cx - rx - 2))
    x1 = min(w, int(cx + rx + 3))
    y0 = max(0, int(cy - ry - 2))
    y1 = min(h, int(cy + ry + 3))
    for py_i in range(y0, y1):
        dy = (py_i - cy) / ry
        dy2 = dy * dy
        if dy2 >= 1.0:
            continue
        for px_i in range(x0, x1):
            dx = (px_i - cx) / rx
            r2 = dx * dx + dy2
            if r2 < 1.0:
                a = int(max_alpha * ((1.0 - r2) ** 1.65))
                if a > 0:
                    px[px_i, py_i] = (color[0], color[1], color[2], a)
    return im


def draw_star4(draw: ImageDraw.ImageDraw, cx: float, cy: float,
               r_out: float, r_in: float, fill: tuple[int, int, int, int]) -> None:
    pts = []
    for i in range(8):
        ang = i * math.pi / 4.0 - math.pi / 2.0
        rad = r_out if (i % 2 == 0) else r_in
        pts.append((cx + math.cos(ang) * rad, cy + math.sin(ang) * rad))
    draw.polygon(pts, fill=fill)


def draw_heart(draw: ImageDraw.ImageDraw, cx: float, cy: float,
               size: float, fill: tuple[int, int, int, int]) -> None:
    pts = []
    for i in range(28):
        t = i * 2.0 * math.pi / 28.0
        hx = 16.0 * (math.sin(t) ** 3)
        hy = -(13.0 * math.cos(t) - 5.0 * math.cos(2 * t)
               - 2.0 * math.cos(3 * t) - math.cos(4 * t))
        pts.append((cx + hx * size / 18.0, cy + hy * size / 18.0))
    draw.polygon(pts, fill=fill)


def sd_2d_tri(px: float, py: float,
              ax: float, ay: float,
              bx: float, by: float,
              cx: float, cy: float) -> float:
    """Exact signed distance to a 2D triangle (negative inside)."""
    e0x, e0y = bx - ax, by - ay
    e1x, e1y = cx - bx, cy - by
    e2x, e2y = ax - cx, ay - cy
    v0x, v0y = px - ax, py - ay
    v1x, v1y = px - bx, py - by
    v2x, v2y = px - cx, py - cy

    t0 = clamp((v0x * e0x + v0y * e0y) / (e0x * e0x + e0y * e0y))
    t1 = clamp((v1x * e1x + v1y * e1y) / (e1x * e1x + e1y * e1y))
    t2 = clamp((v2x * e2x + v2y * e2y) / (e2x * e2x + e2y * e2y))

    pq0x, pq0y = v0x - e0x * t0, v0y - e0y * t0
    pq1x, pq1y = v1x - e1x * t1, v1y - e1y * t1
    pq2x, pq2y = v2x - e2x * t2, v2y - e2y * t2

    s = 1.0 if (e0x * e2y - e0y * e2x) >= 0.0 else -1.0
    d0 = (pq0x * pq0x + pq0y * pq0y, s * (v0x * e0y - v0y * e0x))
    d1 = (pq1x * pq1x + pq1y * pq1y, s * (v1x * e1y - v1y * e1x))
    d2 = (pq2x * pq2x + pq2y * pq2y, s * (v2x * e2y - v2y * e2x))
    md = min(d0[0], d1[0], d2[0])
    ms = min(d0[1], d1[1], d2[1])
    return -math.sqrt(md) * (1.0 if ms > 0.0 else -1.0)


def sd_cat_ear(x: float, y: float, z: float,
               cx: float, cy: float, cz: float,
               w_half: float, height: float, depth: float,
               ang_rad: float, round_r: float = 0.034) -> float:
    """3D rounded triangular cat ear pointing upward-outward."""
    dx = x - cx
    dy = y - cy
    ca = math.cos(ang_rad)
    sa = math.sin(ang_rad)
    lx = dx * ca + dy * sa
    ly = -dx * sa + dy * ca
    lz = z - cz

    d2 = sd_2d_tri(lx, ly, -w_half, 0.0, w_half, 0.0, 0.0, -height)
    t_h = clamp(-ly / max(height, 1e-4))
    z_half = depth * (1.0 - 0.55 * t_h)
    dz = abs(lz) - z_half
    ox = max(d2, 0.0)
    oz = max(dz, 0.0)
    return math.sqrt(ox * ox + oz * oz) + min(max(d2, dz), 0.0) - round_r


def sd_cat_tail(x: float, y: float, z: float,
                dx_off: float = 0.0, dy_off: float = 0.0, thick: float = 0.072) -> float:
    """Graceful curved J/S-shaped cat tail curling up on the right."""
    t1 = sd_capsule(x, y, z, 0.22, 0.44, -0.10, 0.46, 0.40, -0.05, thick)
    t2 = sd_capsule(x, y, z, 0.46, 0.40, -0.05, 0.58 + dx_off, 0.14 + dy_off, 0.02, thick * 0.94)
    t3 = sd_ellipsoid(x, y, z, 0.54 + dx_off, 0.07 + dy_off, 0.05, thick * 1.12, thick * 1.12, thick * 1.05)
    return smin(smin(t1, t2, 0.04), t3, 0.04)


# ---------------------------------------------------------------------------
# Cat Character ("奶糖布偶喵 / Ragdoll Cat", Option A, 84x78)
# ---------------------------------------------------------------------------

def render_cat_pose(pose: str) -> Image.Image:
    W, H = 84, 78
    bob_y = 0.105 if pose == "focus_1" else (0.070 if pose == "done" else 0.090)
    ear_r_tilt = 0.66 if pose == "focus_2" else 0.56
    ear_l_tilt = -0.56
    tail_dx = 0.04 if pose == "focus_1" else (0.07 if pose == "focus_2" else (-0.04 if pose == "focus_3" else 0.0))
    tail_dy = -0.08 if pose == "focus_2" else (-0.03 if pose == "focus_3" else 0.0)
    paws_up = (pose == "done")
    has_book = pose in ("focus_0", "focus_1", "focus_2", "focus_3", "idle")
    has_mug = (pose == "break")

    def cat_eval(x: float, y: float, z: float) -> tuple[float, str]:
        by = bob_y
        # 1. Chubby mochi-cat head (wider than tall -> unmistakable feline cheek volume)
        head_main = sd_ellipsoid(x, y, z, 0.0, -0.18 + by, 0.02, 0.39, 0.31, 0.34)
        cheek_l = sd_ellipsoid(x, y, z, -0.17, -0.08 + by, 0.04, 0.26, 0.20, 0.28)
        cheek_r = sd_ellipsoid(x, y, z,  0.17, -0.08 + by, 0.04, 0.26, 0.20, 0.28)
        head = smin(smin(head_main, cheek_l, 0.09), cheek_r, 0.09)

        # Tiny forehead fur tuft
        tuft = sd_ellipsoid(x, y, z, 0.02, -0.47 + by, 0.08, 0.072, 0.052, 0.06)
        head = smin(head, tuft, 0.03)

        # Pointed triangular CAT EARS at upper-outer corners, angled outward
        ear_l = sd_cat_ear(x, y, z, -0.25, -0.37 + by, 0.00, 0.135, 0.27, 0.055, ear_l_tilt, 0.034)
        ear_r = sd_cat_ear(x, y, z,  0.25, -0.37 + by, 0.00, 0.135, 0.27, 0.055, ear_r_tilt, 0.034)
        head_with_ears = smin(smin(head, ear_l, 0.045), ear_r, 0.045)

        # Concave sakura-pink inner ear triangles
        iear_l = sd_cat_ear(x, y, z, -0.255, -0.38 + by, 0.052, 0.072, 0.155, 0.024, ear_l_tilt, 0.018)
        iear_r = sd_cat_ear(x, y, z,  0.255, -0.38 + by, 0.052, 0.072, 0.155, 0.024, ear_r_tilt, 0.018)
        d_iear = min(iear_l, iear_r)

        # Cozy denim hoodie torso + cream plush neck collar + small gold bell
        torso = sd_ellipsoid(x, y, z, 0.0, 0.30 + by * 0.4, -0.02, 0.29, 0.27, 0.25)
        collar = sd_ellipsoid(x, y, z, 0.0, 0.11 + by * 0.6, 0.03, 0.25, 0.082, 0.22)
        bell = sd_ellipsoid(x, y, z, 0.0, 0.20 + by * 0.5, 0.25, 0.058, 0.058, 0.055)

        # Curved Ragdoll cat tail
        d_tail = sd_cat_tail(x, y, z, tail_dx, tail_dy)

        # Sculpted tiny 3D cat nose
        nose = sd_ellipsoid(x, y, z, 0.0, -0.075 + by, 0.36, 0.036, 0.024, 0.028)

        # Chubby vinyl paws
        if paws_up:
            paw_l = sd_capsule(x, y, z, -0.22, 0.26, 0.08, -0.43, 0.06, 0.18, 0.088)
            paw_r = sd_capsule(x, y, z,  0.22, 0.26, 0.08,  0.43, 0.06, 0.18, 0.088)
        else:
            paw_l = sd_ellipsoid(x, y, z, -0.20, 0.41, 0.23, 0.095, 0.078, 0.095)
            paw_r = sd_ellipsoid(x, y, z,  0.20, 0.41, 0.23, 0.095, 0.078, 0.095)
        d_paws = min(paw_l, paw_r)

        # Props: study book or ceramic strawberry mug
        d_prop = 9.0
        prop_mat = "book_cover"
        if has_book:
            if pose == "idle":
                cover = sd_round_box(x, y, z, 0.0, 0.56, 0.20, 0.25, 0.12, 0.055, 0.025)
                pages = sd_round_box(x, y, z, 0.0, 0.53, 0.21, 0.22, 0.09, 0.042, 0.02)
                d_prop, prop_mat = (pages, "book_page") if pages < cover else (cover, "book_cover")
            else:
                cover_l = sd_round_box(x + 0.01, y + x * 0.11, z, -0.16, 0.56, 0.21, 0.16, 0.11, 0.024, 0.018)
                cover_r = sd_round_box(x - 0.01, y - x * 0.11, z,  0.16, 0.56, 0.21, 0.16, 0.11, 0.024, 0.018)
                cover = smin(cover_l, cover_r, 0.025)
                page_l = sd_round_box(x + 0.01, y + x * 0.11, z, -0.15, 0.52, 0.23, 0.14, 0.095, 0.026, 0.016)
                page_r = sd_round_box(x - 0.01, y - x * 0.11, z,  0.15, 0.52, 0.23, 0.14, 0.095, 0.026, 0.016)
                pages = smin(page_l, page_r, 0.02)
                d_prop, prop_mat = (pages, "book_page") if pages < cover else (cover, "book_cover")
        elif has_mug:
            mug = sd_ellipsoid(x, y, z, 0.0, 0.45, 0.26, 0.14, 0.145, 0.13)
            mug_rim = sd_ellipsoid(x, y, z, 0.0, 0.34, 0.26, 0.125, 0.035, 0.115)
            d_prop = smin(mug, mug_rim, 0.02)
            prop_mat = "ceramic_pnk"

        # Ragdoll Option A surface material assignment:
        # Caramel-oat plush ears + gentle forehead V-crown (above eyes!),
        # keeping eye sockets & cheeks clean cream vinyl for luminous contrast.
        head_mat = "cat_cream_plush"
        if ear_l < head - 0.008 or ear_r < head - 0.008:
            head_mat = "cat_caramel_plush"
        else:
            crown_l = sd_rot_ellipsoid(x, y, z, -0.19, -0.34 + by, 0.16, 0.16, 0.095, 0.18, -0.32)
            crown_r = sd_rot_ellipsoid(x, y, z,  0.19, -0.34 + by, 0.16, 0.16, 0.095, 0.18,  0.32)
            if min(crown_l, crown_r) < 0.01 and head_with_ears < 0.03:
                head_mat = "cat_caramel_plush"
            else:
                face_vinyl = sd_ellipsoid(x, y, z, 0.0, -0.10 + by, 0.20, 0.31, 0.20, 0.22)
                if face_vinyl < 0.015 and head_with_ears < 0.03:
                    head_mat = "vinyl_face"

        best_d, best_m = head_with_ears, head_mat
        if d_iear < best_d:
            best_d, best_m = d_iear, "ear_pink_soft"
        if torso < best_d:
            best_d, best_m = torso, "outfit_denim"
        if collar < best_d:
            best_d, best_m = collar, "cat_cream_plush"
        if bell < best_d:
            best_d, best_m = bell, "gold_mat"
        if d_tail < best_d:
            best_d, best_m = d_tail, ("cat_caramel_plush" if y < 0.28 else "cat_cream_plush")
        if nose < best_d:
            best_d, best_m = nose, "nose_berry"
        if d_paws < best_d:
            best_d, best_m = d_paws, "vinyl_face"
        if d_prop < best_d:
            best_d, best_m = d_prop, prop_mat
        return best_d, best_m

    base = render_sdf_scene(
        W, H, cat_eval, ssaa=2, z_start=0.65, z_min=-0.45,
        edge_px=1.35, bbox=(4, 2, W - 4, H - 1), fur_edge=True,
    )

    # Soft desk contact shadow underneath cat/book
    shadow = make_soft_shadow(W, H, W * 0.50, H * 0.88, W * 0.32, H * 0.075, max_alpha=46)
    canvas = Image.alpha_composite(shadow, base)

    # Crisp 3x SSAA facial details: blush, sapphire glass eyes, sweet :3 cat smile, whiskers
    S = 3
    w3, h3 = W * S, H * S
    face_ov = Image.new("RGBA", (w3, h3), (0, 0, 0, 0))
    sc = max(w3, h3) * 0.5
    ox = w3 * 0.5
    oy = h3 * 0.5

    def pt(nx: float, ny: float) -> tuple[float, float]:
        return (ox + nx * sc, oy + (ny + bob_y) * sc)

    # 1. Soft airbrushed coral-pink cheek blush
    blush = Image.new("RGBA", (w3, h3), (0, 0, 0, 0))
    db = ImageDraw.Draw(blush)
    for sx in (-0.21, 0.21):
        bx, by_p = pt(sx, -0.055)
        rx, ry = 0.082 * sc, 0.048 * sc
        db.ellipse((bx - rx, by_p - ry, bx + rx, by_p + ry), fill=(255, 128, 148, 138))
    blush = blush.filter(ImageFilter.GaussianBlur(radius=4.0))
    face_ov = Image.alpha_composite(face_ov, blush)
    d3 = ImageDraw.Draw(face_ov)

    # 2. Luminous Ragdoll sapphire-blue glossy resin eyes (or curved blink/sleepy eyes)
    eye_y = -0.155
    eye_dx = 0.145
    if pose in ("focus_1", "focus_3", "break"):
        for sign in (-1.0, 1.0):
            ex, ey = pt(sign * eye_dx, eye_y + 0.015)
            r_arc = 0.058 * sc
            if pose == "focus_1":
                d3.arc((ex - r_arc, ey - r_arc * 0.8, ex + r_arc, ey + r_arc * 0.8),
                       start=15, end=165, fill=(68, 42, 34, 248), width=max(3, int(0.022 * sc)))
            else:
                d3.arc((ex - r_arc, ey - r_arc * 0.75, ex + r_arc, ey + r_arc * 0.9),
                       start=195, end=345, fill=(68, 42, 34, 248), width=max(3, int(0.024 * sc)))
    elif pose == "paused":
        for sign in (-1.0, 1.0):
            ex, ey = pt(sign * eye_dx, eye_y + 0.01)
            rx, ry = 0.056 * sc, 0.038 * sc
            d3.pieslice((ex - rx, ey - ry, ex + rx, ey + ry),
                        start=0, end=180, fill=(38, 52, 92, 250))
            d3.line((ex - rx * 1.05, ey, ex + rx * 1.05, ey),
                    fill=(68, 42, 34, 255), width=max(3, int(0.018 * sc)))
    else:
        look_dy = 0.012 if pose.startswith("focus") else 0.0
        for sign in (-1.0, 1.0):
            ex, ey = pt(sign * eye_dx, eye_y + look_dy)
            rx, ry = 0.064 * sc, 0.068 * sc
            # Deep sapphire glass orb + luminous sky-blue lower iris + studio highlights
            d3.ellipse((ex - rx, ey - ry, ex + rx, ey + ry), fill=(28, 44, 86, 255))
            d3.chord((ex - rx * 0.84, ey - ry * 0.45, ex + rx * 0.84, ey + ry * 0.88),
                     start=14, end=166, fill=(86, 154, 234, 235))
            d3.ellipse((ex - rx * 0.50, ey - ry * 0.46, ex + rx * 0.50, ey + ry * 0.42),
                       fill=(18, 28, 56, 255))
            cx1, cy1, cr1 = ex - rx * 0.28, ey - ry * 0.28, rx * 0.34
            d3.ellipse((cx1 - cr1, cy1 - cr1 * 1.1, cx1 + cr1, cy1 + cr1 * 1.1),
                       fill=(255, 255, 255, 252))
            cx2, cy2, cr2 = ex + rx * 0.30, ey + ry * 0.34, rx * 0.16
            d3.ellipse((cx2 - cr2, cy2 - cr2, cx2 + cr2, cy2 + cr2),
                       fill=(255, 248, 236, 235))
            if pose == "done":
                draw_star4(d3, ex + rx * 0.04, ey - ry * 0.02, cr1 * 0.88, cr1 * 0.34,
                           (255, 242, 168, 245))

    # Tiny soft brows
    for sign in (-1.0, 1.0):
        bx, by_p = pt(sign * eye_dx, eye_y - 0.105)
        br = 0.028 * sc
        d3.arc((bx - br, by_p - br * 0.6, bx + br, by_p + br * 0.6),
               start=205, end=335, fill=(120, 78, 58, 195), width=max(2, int(0.012 * sc)))

    # 3. Sweet fang-free ':3' / 'ω' cat smile (or peaceful 'o' when sleepy)
    mx, my = pt(0.0, -0.026)
    if pose == "paused":
        mw_p, mh_p = 0.026 * sc, 0.024 * sc
        d3.ellipse((mx - mw_p, my - mh_p * 0.2, mx + mw_p, my + mh_p * 1.4),
                   fill=(235, 130, 140, 230))
    else:
        rw = (0.050 if pose == "done" else 0.044) * sc
        rh = (0.038 if pose == "done" else 0.032) * sc
        mw_w = max(4, int(0.022 * sc))
        mw_col = (88, 48, 42, 248)
        d3.arc((mx - rw * 2.0, my - rh, mx, my + rh), start=15, end=170, fill=mw_col, width=mw_w)
        d3.arc((mx, my - rh, mx + rw * 2.0, my + rh), start=10, end=165, fill=mw_col, width=mw_w)

    # 4. Crisp subtle cat whiskers (3 delicate strokes per cheek)
    wh_col = (128, 94, 76, 200)
    wh_w = max(3, int(0.013 * sc))
    for sign in (-1.0, 1.0):
        for dy_w, tilt_w in ((-0.028, -0.018), (0.0, 0.004), (0.028, 0.024)):
            p0 = pt(sign * 0.25, -0.045 + dy_w)
            p1 = pt(sign * 0.39, -0.045 + dy_w + tilt_w)
            d3.line((p0, p1), fill=wh_col, width=wh_w)

    # 5. Book gold star emblem / turning page / pose sparkles
    if pose == "idle":
        sx, sy = ox + 0.0 * sc, oy + 0.55 * sc
        draw_star4(d3, sx, sy, 0.052 * sc, 0.022 * sc, (255, 220, 96, 245))
    elif pose == "focus_2":
        px_b, py_b = ox + 0.04 * sc, oy + 0.46 * sc
        d3.arc((px_b - 0.14 * sc, py_b - 0.08 * sc, px_b + 0.14 * sc, py_b + 0.12 * sc),
               start=200, end=330, fill=(255, 252, 242, 240), width=max(2, int(0.016 * sc)))
        draw_star4(d3, ox + 0.24 * sc, oy + 0.34 * sc, 0.045 * sc, 0.018 * sc, (255, 218, 90, 245))
    elif pose == "paused":
        zx1, zy1 = ox + 0.44 * sc, oy - 0.48 * sc
        w_z = 0.065 * sc
        d3.line((zx1 - w_z, zy1 - w_z, zx1 + w_z, zy1 - w_z, zx1 - w_z, zy1 + w_z, zx1 + w_z, zy1 + w_z),
                fill=(136, 174, 236, 245), width=max(3, int(0.018 * sc)))
        zx2, zy2 = ox + 0.56 * sc, oy - 0.66 * sc
        w_z2 = 0.045 * sc
        d3.line((zx2 - w_z2, zy2 - w_z2, zx2 + w_z2, zy2 - w_z2, zx2 - w_z2, zy2 + w_z2, zx2 + w_z2, zy2 + w_z2),
                fill=(164, 196, 246, 235), width=max(2, int(0.014 * sc)))
    elif pose == "break":
        draw_heart(d3, ox + 0.0 * sc, oy + 0.46 * sc, 0.048 * sc, (255, 244, 246, 238))
        for dx_s, dy_s in ((-0.05, 0.26), (0.05, 0.22)):
            sx, sy = ox + dx_s * sc, oy + dy_s * sc
            d3.arc((sx - 0.03 * sc, sy - 0.05 * sc, sx + 0.03 * sc, sy + 0.05 * sc),
                   start=100, end=270, fill=(255, 250, 242, 185), width=max(2, int(0.014 * sc)))
    elif pose == "done":
        draw_heart(d3, ox + 0.0 * sc, oy - 0.66 * sc, 0.088 * sc, (255, 108, 134, 248))
        draw_star4(d3, ox - 0.56 * sc, oy - 0.44 * sc, 0.068 * sc, 0.026 * sc, (255, 214, 72, 248))
        draw_star4(d3, ox + 0.58 * sc, oy - 0.40 * sc, 0.058 * sc, 0.022 * sc, (255, 214, 72, 245))

    face_1x = face_ov.resize((W, H), Image.Resampling.LANCZOS)
    return Image.alpha_composite(canvas, face_1x)


# ---------------------------------------------------------------------------
# Desk Table (196x52) & Lamp Glow Halo (92x92)
# ---------------------------------------------------------------------------

def render_desk_table() -> Image.Image:
    W, H = 196, 52
    S = 2
    w2, h2 = W * S, H * S
    im = Image.new("RGBA", (w2, h2), (0, 0, 0, 0))

    # Soft elliptical floor contact shadows under left/right legs
    for lx_c in (30 * S, 166 * S):
        sh = make_soft_shadow(w2, h2, lx_c, 47.5 * S, 14 * S, 3.2 * S, max_alpha=68)
        im = Image.alpha_composite(im, sh)

    ov = Image.new("RGBA", (w2, h2), (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)

    # Tapered sleek Scandinavian legs (cream-oak with subtle shading + brass foot caps)
    for bx_top, bx_bot in ((28 * S, 24 * S), (168 * S, 172 * S)):
        poly = [
            (bx_top - 4 * S, 14 * S),
            (bx_top + 4 * S, 14 * S),
            (bx_bot + 2.8 * S, 46 * S),
            (bx_bot - 2.8 * S, 46 * S),
        ]
        d.polygon(poly, fill=(214, 172, 128, 255))
        d.line((bx_top - 2.2 * S, 14 * S, bx_bot - 1.4 * S, 46 * S),
               fill=(238, 202, 162, 255), width=3)
        d.rounded_rectangle((bx_bot - 3.2 * S, 41 * S, bx_bot + 3.2 * S, 47 * S),
                            radius=2 * S, fill=(224, 174, 72, 255))

    # Desk under-apron shadow bar
    d.rounded_rectangle((22 * S, 12 * S, (W - 22) * S, 19 * S),
                        radius=3 * S, fill=(176, 122, 78, 255))

    # Thick sculpted pill-rounded honey-oak desktop
    d.rounded_rectangle((4 * S, 3 * S, (W - 4) * S, 15 * S),
                        radius=5 * S, fill=(212, 156, 104, 255))
    d.rounded_rectangle((4 * S, 3 * S, (W - 4) * S, 12 * S),
                        radius=5 * S, fill=(244, 204, 156, 255))
    d.rounded_rectangle((8 * S, 4 * S, (W - 8) * S, 6.5 * S),
                        radius=2 * S, fill=(252, 224, 188, 255))
    d.rounded_rectangle((6 * S, 12.5 * S, (W - 6) * S, 15 * S),
                        radius=2 * S, fill=(196, 138, 88, 255))

    im = Image.alpha_composite(im, ov)
    return im.resize((W, H), Image.Resampling.LANCZOS)


def render_lamp_glow() -> Image.Image:
    W, H = 92, 92
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = im.load()
    cx, cy = W * 0.5, H * 0.5
    max_r = W * 0.49
    for y in range(H):
        dy = y - cy
        for x in range(W):
            dx = x - cx
            dist = math.sqrt(dx * dx + dy * dy) / max_r
            if dist < 1.0:
                fall = (1.0 - dist) ** 2.1
                a = int(140 * fall)
                if a > 0:
                    r = 255
                    g = int(lerp(218, 244, 1.0 - dist))
                    b = int(lerp(116, 186, 1.0 - dist))
                    px[x, y] = (r, g, b, a)
    return im


# ---------------------------------------------------------------------------
# Chairs (76x58)
# ---------------------------------------------------------------------------

def render_chair(variant: int) -> Image.Image:
    W, H = 76, 58

    def chair_eval(x: float, y: float, z: float) -> tuple[float, str]:
        if variant == 0:
            # Scandinavian curved wood & cream cushion chair
            back = sd_round_box(x, y, z, 0.0, -0.18, -0.10, 0.34, 0.24, 0.055, 0.08)
            post_l = sd_capsule(x, y, z, -0.24, 0.0, -0.10, -0.24, 0.22, -0.06, 0.042)
            post_r = sd_capsule(x, y, z,  0.24, 0.0, -0.10,  0.24, 0.22, -0.06, 0.042)
            seat = sd_ellipsoid(x, y, z, 0.0, 0.26, 0.05, 0.38, 0.095, 0.22)
            frame = smin(back, min(post_l, post_r), 0.03)
            leg_l = sd_capsule(x, y, z, -0.28, 0.30, 0.0, -0.32, 0.58, 0.0, 0.038)
            leg_r = sd_capsule(x, y, z,  0.28, 0.30, 0.0,  0.32, 0.58, 0.0, 0.038)
            d_wood = min(frame, leg_l, leg_r)
            if seat < d_wood:
                return seat, "plush_cream"
            return d_wood, "wood_oak"

        if variant == 1:
            # Sakura Plush Cat-Ear Chair
            back = sd_ellipsoid(x, y, z, 0.0, -0.06, -0.08, 0.42, 0.32, 0.14)
            ear_l = sd_rot_ellipsoid(x, y, z, -0.27, -0.44, -0.08, 0.12, 0.18, 0.07, -0.20)
            ear_r = sd_rot_ellipsoid(x, y, z,  0.27, -0.44, -0.08, 0.12, 0.18, 0.07,  0.20)
            d_pink = smin(back, smin(ear_l, ear_r, 0.04), 0.06)
            ie_l = sd_rot_ellipsoid(x, y, z, -0.26, -0.43, 0.0, 0.06, 0.10, 0.04, -0.20)
            ie_r = sd_rot_ellipsoid(x, y, z,  0.26, -0.43, 0.0, 0.06, 0.10, 0.04,  0.20)
            seat = sd_ellipsoid(x, y, z, 0.0, 0.27, 0.06, 0.42, 0.11, 0.24)
            d_cream = min(ie_l, ie_r, seat)
            leg_l = sd_capsule(x, y, z, -0.26, 0.34, 0.0, -0.30, 0.58, 0.0, 0.04)
            leg_r = sd_capsule(x, y, z,  0.26, 0.34, 0.0,  0.30, 0.58, 0.0, 0.04)
            d_wood = min(leg_l, leg_r)
            best_d, best_m = d_pink, "plush_pink"
            if d_cream < best_d:
                best_d, best_m = d_cream, "plush_cream"
            if d_wood < best_d:
                best_d, best_m = d_wood, "wood_oak"
            return best_d, best_m

        if variant == 2:
            # Fluffy Marshmallow Cloud Beanbag
            p1 = sd_ellipsoid(x, y, z, -0.24, -0.04, -0.04, 0.24, 0.26, 0.18)
            p2 = sd_ellipsoid(x, y, z,  0.00, -0.18, -0.05, 0.27, 0.28, 0.18)
            p3 = sd_ellipsoid(x, y, z,  0.24, -0.04, -0.04, 0.24, 0.26, 0.18)
            base = sd_ellipsoid(x, y, z, 0.0, 0.24, 0.05, 0.46, 0.22, 0.25)
            cloud = smin(smin(p1, p2, 0.08), smin(p3, base, 0.09), 0.09)
            leg_l = sd_capsule(x, y, z, -0.24, 0.40, 0.0, -0.26, 0.58, 0.0, 0.04)
            leg_r = sd_capsule(x, y, z,  0.24, 0.40, 0.0,  0.26, 0.58, 0.0, 0.04)
            d_leg = min(leg_l, leg_r)
            if d_leg < cloud:
                return d_leg, "ceramic_sky"
            return cloud, "plush_cloud"

        # variant == 3: Royal Velvet Crown Chair
        back = sd_ellipsoid(x, y, z, 0.0, -0.04, -0.08, 0.40, 0.34, 0.14)
        tuft_l = sd_ellipsoid(x, y, z, -0.26, -0.18, -0.06, 0.18, 0.22, 0.12)
        tuft_r = sd_ellipsoid(x, y, z,  0.26, -0.18, -0.06, 0.18, 0.22, 0.12)
        seat = sd_ellipsoid(x, y, z, 0.0, 0.28, 0.06, 0.44, 0.11, 0.24)
        d_velvet = smin(smin(back, tuft_l, 0.06), smin(tuft_r, seat, 0.06), 0.06)
        # Crown peaks
        c_base = sd_round_box(x, y, z, 0.0, -0.37, -0.02, 0.20, 0.045, 0.04, 0.02)
        c1 = sd_ellipsoid(x, y, z, -0.16, -0.47, -0.02, 0.048, 0.10, 0.045)
        c2 = sd_ellipsoid(x, y, z,  0.00, -0.52, -0.02, 0.055, 0.13, 0.048)
        c3 = sd_ellipsoid(x, y, z,  0.16, -0.47, -0.02, 0.048, 0.10, 0.045)
        leg_l = sd_capsule(x, y, z, -0.26, 0.35, 0.0, -0.28, 0.58, 0.0, 0.042)
        leg_r = sd_capsule(x, y, z,  0.26, 0.35, 0.0,  0.28, 0.58, 0.0, 0.042)
        d_gold = min(smin(c_base, min(c1, c2, c3), 0.02), leg_l, leg_r)
        if d_gold < d_velvet:
            return d_gold, "gold_mat"
        return d_velvet, "plush_berry"

    im = render_sdf_scene(W, H, chair_eval, ssaa=2, z_start=0.55, z_min=-0.4,
                          edge_px=1.35, bbox=(4, 2, W - 4, H - 2), fur_edge=True)
    sh = make_soft_shadow(W, H, W * 0.5, H * 0.90, W * 0.36, 4.5, max_alpha=44)
    return Image.alpha_composite(sh, im)


# ---------------------------------------------------------------------------
# Lamps (48x54)
# ---------------------------------------------------------------------------

def render_lamp(variant: int) -> Image.Image:
    W, H = 48, 54

    def lamp_eval(x: float, y: float, z: float) -> tuple[float, str]:
        if variant == 1:
            # Iconic Bauhaus/Nesso Coral Mushroom Lamp
            dome = sd_ellipsoid(x, y, z, 0.0, -0.18, 0.0, 0.56, 0.32, 0.50)
            # Slice bottom flat cleanly
            dome = max(dome, y - (-0.08))
            glow_under = sd_ellipsoid(x, y, z, 0.0, -0.08, 0.02, 0.45, 0.06, 0.40)
            stem = sd_capsule(x, y, z, 0.0, -0.08, 0.0, 0.0, 0.60, 0.0, 0.14)
            base = sd_ellipsoid(x, y, z, 0.0, 0.62, 0.0, 0.36, 0.12, 0.32)
            stem_base = smin(stem, base, 0.10)
            best_d, best_m = dome, "ceramic_msh"
            if glow_under < best_d:
                best_d, best_m = glow_under, "glow_cream"
            if stem_base < best_d:
                best_d, best_m = stem_base, "ceramic_wht"
            return best_d, best_m

        if variant == 2:
            # Sculpted Crescent Moon & Hanging Star Nightlight
            outer = sd_ellipsoid(x, y, z, -0.02, -0.16, 0.0, 0.38, 0.40, 0.16)
            cut = sd_ellipsoid(x, y, z, 0.17, -0.18, 0.0, 0.31, 0.34, 0.25)
            moon = max(outer, -cut)
            star = sd_ellipsoid(x, y, z, 0.14, -0.05, 0.04, 0.10, 0.10, 0.08)
            stand = sd_capsule(x, y, z, -0.06, 0.20, -0.02, -0.06, 0.58, -0.02, 0.045)
            ring = sd_capsule(x, y, z, -0.06, 0.56, 0.0, 0.0, 0.56, 0.0, 0.045)
            base = sd_ellipsoid(x, y, z, 0.0, 0.64, 0.0, 0.38, 0.11, 0.32)
            best_d, best_m = moon, "moon_gold"
            if star < best_d:
                best_d, best_m = star, "glow_cream"
            if min(stand, ring) < best_d:
                best_d, best_m = min(stand, ring), "gold_mat"
            if base < best_d:
                best_d, best_m = base, "ceramic_sky"
            return best_d, best_m

        # variant == 3: Vinyl Chibi Bear Lamp with Glowing Orb Shade
        orb = sd_ellipsoid(x, y, z, 0.0, -0.34, 0.04, 0.34, 0.32, 0.32)
        head = sd_ellipsoid(x, y, z, 0.0, 0.12, 0.0, 0.28, 0.23, 0.24)
        ear_l = sd_ellipsoid(x, y, z, -0.22, -0.04, -0.02, 0.095, 0.095, 0.08)
        ear_r = sd_ellipsoid(x, y, z,  0.22, -0.04, -0.02, 0.095, 0.095, 0.08)
        body = sd_ellipsoid(x, y, z, 0.0, 0.42, 0.0, 0.25, 0.20, 0.22)
        bear = smin(smin(head, min(ear_l, ear_r), 0.03), body, 0.06)
        base = sd_ellipsoid(x, y, z, 0.0, 0.64, 0.0, 0.38, 0.10, 0.32)
        best_d, best_m = orb, "glow_cream"
        if bear < best_d:
            best_d, best_m = bear, "vinyl_bear"
        if base < best_d:
            best_d, best_m = base, "ceramic_mint"
        return best_d, best_m

    def lamp_glow_fn(x: float, y: float, z: float) -> float:
        if variant == 1 and y > -0.14 and y < 0.10:
            return 0.28
        if variant == 3 and y < -0.04:
            return 0.35
        return 0.0

    im = render_sdf_scene(W, H, lamp_eval, ssaa=2, z_start=0.65, z_min=-0.45,
                          edge_px=1.3, bbox=(2, 2, W - 2, H - 2), glow_fn=lamp_glow_fn)
    sh = make_soft_shadow(W, H, W * 0.5, H * 0.87, W * 0.34, 4.0, max_alpha=58)
    out = Image.alpha_composite(sh, im)

    # Crisp bear eyes/snout or moon sparkle at 3x SSAA
    if variant in (2, 3):
        S = 3
        ov = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
        d3 = ImageDraw.Draw(ov)
        sc = max(W * S, H * S) * 0.5
        ox, oy = W * S * 0.5, H * S * 0.5
        if variant == 2:
            draw_star4(d3, ox + 0.14 * sc, oy - 0.05 * sc, 0.13 * sc, 0.045 * sc, (255, 252, 224, 245))
        else:
            # Cute mini bear muzzle, nose, glossy eyes
            mx, my = ox, oy + 0.16 * sc
            d3.ellipse((mx - 0.10 * sc, my - 0.065 * sc, mx + 0.10 * sc, my + 0.065 * sc),
                       fill=(255, 244, 232, 250))
            d3.ellipse((mx - 0.032 * sc, my - 0.035 * sc, mx + 0.032 * sc, my + 0.015 * sc),
                       fill=(74, 42, 32, 255))
            for sgn in (-1, 1):
                ex = ox + sgn * 0.10 * sc
                ey = oy + 0.09 * sc
                er = 0.028 * sc
                d3.ellipse((ex - er, ey - er, ex + er, ey + er), fill=(48, 26, 22, 255))
                d3.ellipse((ex - er * 0.45, ey - er * 0.45, ex + er * 0.1, ey + er * 0.1),
                           fill=(255, 255, 255, 240))
        out = Image.alpha_composite(out, ov.resize((W, H), Image.Resampling.LANCZOS))
    return out


# ---------------------------------------------------------------------------
# Desk Items (42x40)
# ---------------------------------------------------------------------------

def render_desk_item(variant: int) -> Image.Image:
    W, H = 42, 40

    if variant == 0:
        # Pastel Ceramic Pencil Cup + Marker, Coral Pencil & Mini Ruler (3x SSAA)
        S = 3
        im = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        # Pens & ruler poking out
        d.rounded_rectangle((12 * S, 4 * S, 18 * S, 24 * S), radius=2 * S, fill=(255, 214, 92, 255))
        d.polygon([(12 * S, 5 * S), (15 * S, 1 * S), (18 * S, 5 * S)], fill=(252, 228, 198, 255))
        d.polygon([(14 * S, 2.5 * S), (15 * S, 1 * S), (16 * S, 2.5 * S)], fill=(68, 46, 42, 255))

        d.rounded_rectangle((20 * S, 6 * S, 26 * S, 24 * S), radius=2 * S, fill=(248, 134, 152, 255))
        d.rounded_rectangle((20 * S, 6 * S, 26 * S, 10 * S), radius=2 * S, fill=(255, 232, 214, 255))

        d.rounded_rectangle((27 * S, 9 * S, 31 * S, 24 * S), radius=1 * S, fill=(138, 202, 246, 255))

        # Ribbed matte cream-mint cup
        d.rounded_rectangle((9 * S, 15 * S, 33 * S, 35 * S), radius=6 * S, fill=(156, 214, 184, 255))
        d.rounded_rectangle((9 * S, 15 * S, 22 * S, 34 * S), radius=5 * S, fill=(186, 234, 208, 255))
        d.rounded_rectangle((11 * S, 17 * S, 14 * S, 31 * S), radius=2 * S, fill=(216, 246, 230, 255))
        # Rim
        d.rounded_rectangle((8 * S, 14 * S, 34 * S, 18 * S), radius=2 * S, fill=(248, 244, 236, 255))
        im = im.resize((W, H), Image.Resampling.LANCZOS)
        sh = make_soft_shadow(W, H, W * 0.5, 35.0, 14.0, 3.5, max_alpha=56)
        return Image.alpha_composite(sh, im)

    def item_eval(x: float, y: float, z: float) -> tuple[float, str]:
        if variant == 1:
            # Frosted Strawberry Cupcake Ceramic Mug
            mug = sd_round_box(x, y, z, -0.04, 0.24, 0.0, 0.34, 0.30, 0.30, 0.14)
            handle = sd_capsule(x, y, z, 0.32, 0.12, 0.0, 0.52, 0.24, 0.0, 0.075)
            h_bot = sd_capsule(x, y, z, 0.52, 0.24, 0.0, 0.32, 0.36, 0.0, 0.075)
            d_mug = smin(mug, smin(handle, h_bot, 0.04), 0.04)
            cream1 = sd_ellipsoid(x, y, z, -0.04, -0.08, 0.02, 0.42, 0.17, 0.36)
            cream2 = sd_ellipsoid(x, y, z, -0.04, -0.24, 0.03, 0.30, 0.14, 0.26)
            d_cream = smin(cream1, cream2, 0.05)
            cherry = sd_ellipsoid(x, y, z, -0.04, -0.42, 0.06, 0.14, 0.14, 0.14)
            best_d, best_m = d_mug, "ceramic_pnk"
            if d_cream < best_d:
                best_d, best_m = d_cream, "plush_cream"
            if cherry < best_d:
                best_d, best_m = cherry, "red_cherry"
            return best_d, best_m

        if variant == 2:
            # Terracotta Pot + Plump 3D Echeveria Succulent Rosette
            pot_top = sd_round_box(x, y, z, 0.0, 0.14, 0.0, 0.38, 0.06, 0.34, 0.05)
            pot_body = sd_round_box(x, y, z, 0.0, 0.40, 0.0, 0.30, 0.22, 0.28, 0.09)
            d_pot = smin(pot_top, pot_body, 0.03)
            d_leaf = 9.0
            leaf_mat = "succulent"
            petals = [
                (-0.34, -0.04, 0.04, 0.20, 0.11, 0.14, 0.35),
                ( 0.34, -0.04, 0.04, 0.20, 0.11, 0.14, -0.35),
                (-0.20, -0.18, 0.10, 0.17, 0.12, 0.14, 0.55),
                ( 0.20, -0.18, 0.10, 0.17, 0.12, 0.14, -0.55),
                ( 0.00, -0.06, 0.20, 0.17, 0.11, 0.15, 0.0),
                ( 0.00, -0.30, 0.06, 0.13, 0.16, 0.13, 0.0),
                (-0.12, -0.28, 0.14, 0.11, 0.14, 0.12, 0.25),
                ( 0.12, -0.28, 0.14, 0.11, 0.14, 0.12, -0.25),
            ]
            for px_c, py_c, pz_c, rx, ry, rz, ang in petals:
                lp = sd_rot_ellipsoid(x, y, z, px_c, py_c, pz_c, rx, ry, rz, ang)
                d_leaf = smin(d_leaf, lp, 0.03)
            if y < -0.38 or abs(x) > 0.45:
                leaf_mat = "succ_tip"
            if d_leaf < d_pot:
                return d_leaf, leaf_mat
            return d_pot, "terra_pot"

        # variant == 3: Glossy Porcelain Lucky Cat Figurine
        body = sd_ellipsoid(x, y, z, 0.0, 0.26, 0.0, 0.36, 0.36, 0.32)
        head = sd_ellipsoid(x, y, z, 0.0, -0.22, 0.02, 0.35, 0.28, 0.30)
        ear_l = sd_rot_ellipsoid(x, y, z, -0.24, -0.50, 0.0, 0.10, 0.15, 0.08, -0.25)
        ear_r = sd_rot_ellipsoid(x, y, z,  0.24, -0.50, 0.0, 0.10, 0.15, 0.08,  0.25)
        paw_wave = sd_capsule(x, y, z, -0.28, 0.18, 0.12, -0.44, -0.14, 0.18, 0.11)
        paw_l = sd_ellipsoid(x, y, z, 0.26, 0.28, 0.16, 0.11, 0.12, 0.11)
        d_cat = smin(smin(body, head, 0.06), min(ear_l, ear_r, paw_wave, paw_l), 0.04)
        collar = sd_ellipsoid(x, y, z, 0.0, 0.03, 0.08, 0.30, 0.065, 0.26)
        bell = sd_ellipsoid(x, y, z, 0.0, 0.11, 0.34, 0.09, 0.09, 0.09)
        best_d, best_m = d_cat, "vinyl_cat"
        if collar < best_d:
            best_d, best_m = collar, "red_cherry"
        if bell < best_d:
            best_d, best_m = bell, "gold_mat"
        return best_d, best_m

    im = render_sdf_scene(W, H, item_eval, ssaa=2, z_start=0.65, z_min=-0.45,
                          edge_px=1.3, bbox=(2, 2, W - 2, H - 2))
    sh = make_soft_shadow(W, H, W * 0.5, H * 0.87, W * 0.34, 3.6, max_alpha=56)
    out = Image.alpha_composite(sh, im)

    if variant == 3:
        S = 3
        ov = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
        d3 = ImageDraw.Draw(ov)
        sc = max(W * S, H * S) * 0.5
        ox, oy = W * S * 0.5, H * S * 0.5
        # Lucky cat happy curved eyes, coral blush, red ears, coin oval
        for sgn in (-1, 1):
            ex = ox + sgn * 0.14 * sc
            ey = oy - 0.24 * sc
            r_a = 0.055 * sc
            d3.arc((ex - r_a, ey - r_a, ex + r_a, ey + r_a),
                   start=195, end=345, fill=(48, 28, 24, 250), width=max(2, int(0.02 * sc)))
            bx = ox + sgn * 0.20 * sc
            by_p = oy - 0.15 * sc
            d3.ellipse((bx - 0.055 * sc, by_p - 0.035 * sc, bx + 0.055 * sc, by_p + 0.035 * sc),
                       fill=(255, 134, 152, 160))
        # Cute nose + gold coin on belly
        d3.ellipse((ox - 0.03 * sc, oy - 0.20 * sc, ox + 0.03 * sc, oy - 0.16 * sc),
                   fill=(242, 92, 118, 255))
        d3.rounded_rectangle((ox + 0.06 * sc, oy + 0.24 * sc, ox + 0.22 * sc, oy + 0.44 * sc),
                             radius=int(0.04 * sc), fill=(250, 206, 76, 255))
        out = Image.alpha_composite(out, ov.resize((W, H), Image.Resampling.LANCZOS))
    return out


# ---------------------------------------------------------------------------
# Wall Decor (72x52)
# ---------------------------------------------------------------------------

def render_wdecor(variant: int) -> Image.Image:
    W, H = 72, 52
    S = 3
    w3, h3 = W * S, H * S
    im = Image.new("RGBA", (w3, h3), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    if variant == 0:
        # Modern Arched Nordic Window with Sunlit Gradient Sky, 3D Clouds & Sill
        # Soft wall shadow behind frame
        d.rounded_rectangle((11 * S, 4 * S, 63 * S, 44 * S), radius=24 * S, fill=(140, 110, 90, 48))
        # Outer warm oak arch frame
        d.rounded_rectangle((9 * S, 2 * S, 61 * S, 43 * S), radius=25 * S, fill=(216, 166, 116, 255))
        d.rounded_rectangle((10 * S, 3 * S, 60 * S, 42 * S), radius=24 * S, fill=(238, 194, 146, 255))

        # Sky glass pane mask
        pane = Image.new("RGBA", (w3, h3), (0, 0, 0, 0))
        dp = ImageDraw.Draw(pane)
        for py in range(6 * S, 40 * S):
            t = (py - 6 * S) / (34.0 * S)
            r = int(lerp(132, 255, t * 0.85))
            g = int(lerp(202, 238, t))
            b = int(lerp(252, 224, t))
            dp.line((13 * S, py, 57 * S, py), fill=(r, g, b, 255))

        # Soft 3D fluffy clouds inside pane
        dp.ellipse((15 * S, 18 * S, 34 * S, 29 * S), fill=(255, 255, 255, 238))
        dp.ellipse((21 * S, 13 * S, 38 * S, 26 * S), fill=(255, 255, 255, 245))
        dp.ellipse((39 * S, 22 * S, 55 * S, 32 * S), fill=(255, 252, 248, 225))
        # Warm sun glow
        dp.ellipse((41 * S, 8 * S, 53 * S, 20 * S), fill=(255, 238, 152, 245))
        # Diagonal glass reflection (opaque tint so pane stays 255 alpha)
        dp.polygon([(14 * S, 36 * S), (26 * S, 8 * S), (33 * S, 8 * S), (20 * S, 36 * S)],
                   fill=(235, 248, 255, 255))

        mask = Image.new("L", (w3, h3), 0)
        dm = ImageDraw.Draw(mask)
        dm.rounded_rectangle((13 * S, 6 * S, 57 * S, 40 * S), radius=21 * S, fill=255)
        im = Image.composite(pane, im, mask)
        d = ImageDraw.Draw(im)

        # Window mullions
        d.rectangle((34 * S, 5 * S, 36.5 * S, 40 * S), fill=(238, 194, 146, 255))
        d.rectangle((12 * S, 24 * S, 58 * S, 26.5 * S), fill=(238, 194, 146, 255))
        # Rounded oak window sill
        d.rounded_rectangle((5 * S, 40 * S, 65 * S, 46 * S), radius=3 * S, fill=(228, 178, 128, 255))
        d.rounded_rectangle((6 * S, 40 * S, 64 * S, 42.5 * S), radius=2 * S, fill=(250, 214, 172, 255))
        # Tiny cute mini potted leaf on sill
        d.rounded_rectangle((49 * S, 34 * S, 56 * S, 40 * S), radius=2 * S, fill=(224, 134, 96, 255))
        d.ellipse((48 * S, 29 * S, 54 * S, 35 * S), fill=(116, 198, 142, 255))
        d.ellipse((52 * S, 28 * S, 58 * S, 34 * S), fill=(142, 214, 162, 255))

    elif variant == 1:
        # Rounded Oak Corkboard with Polaroids, Washi Tape & Glossy Pins
        d.rounded_rectangle((6 * S, 5 * S, 68 * S, 48 * S), radius=6 * S, fill=(130, 100, 80, 42))
        d.rounded_rectangle((4 * S, 3 * S, 66 * S, 46 * S), radius=6 * S, fill=(214, 164, 114, 255))
        d.rounded_rectangle((8 * S, 7 * S, 62 * S, 42 * S), radius=4 * S, fill=(234, 194, 150, 255))
        # Subtle cork texture dots
        for iy in range(10 * S, 40 * S, 4 * S):
            for ix in range(11 * S, 60 * S, 5 * S):
                if hash2(ix, iy) > 0.45:
                    d.ellipse((ix, iy, ix + S, iy + S), fill=(218, 174, 128, 255))

        # Polaroid 1 (left - cute cat sticker photo)
        d.rounded_rectangle((12 * S, 11 * S, 32 * S, 36 * S), radius=2 * S, fill=(255, 253, 249, 255))
        d.rounded_rectangle((14 * S, 13 * S, 30 * S, 29 * S), radius=1 * S, fill=(182, 224, 252, 255))
        d.ellipse((18 * S, 17 * S, 26 * S, 25 * S), fill=(255, 246, 236, 255))
        d.polygon([(18 * S, 19 * S), (19 * S, 15 * S), (21 * S, 18 * S)], fill=(244, 206, 168, 255))
        d.polygon([(26 * S, 19 * S), (25 * S, 15 * S), (23 * S, 18 * S)], fill=(244, 206, 168, 255))
        # Pink pushpin
        d.ellipse((20.5 * S, 9.5 * S, 24.5 * S, 13.5 * S), fill=(248, 116, 146, 255))
        d.ellipse((21.5 * S, 10.2 * S, 22.8 * S, 11.5 * S), fill=(255, 232, 238, 255))

        # Polaroid 2 (right - sunny hills + sticky note)
        d.rounded_rectangle((37 * S, 13 * S, 57 * S, 37 * S), radius=2 * S, fill=(255, 252, 246, 255))
        d.rounded_rectangle((39 * S, 15 * S, 55 * S, 29 * S), radius=1 * S, fill=(255, 226, 234, 255))
        d.pieslice((39 * S, 22 * S, 55 * S, 34 * S), 180, 360, fill=(142, 208, 162, 255))
        d.ellipse((48 * S, 17 * S, 53 * S, 22 * S), fill=(255, 214, 92, 255))
        # Mint washi tape strip
        d.rounded_rectangle((43 * S, 11 * S, 52 * S, 15 * S), radius=1 * S, fill=(166, 226, 200, 255))

    elif variant == 2:
        # Pastel Linen Bunting Flags + Floating Mini Oak Shelf with Books
        d.arc((4 * S, -4 * S, 68 * S, 16 * S), start=12, end=168,
              fill=(172, 138, 112, 255), width=2 * S)
        flag_colors = [
            (252, 152, 172, 255),
            (255, 218, 118, 255),
            (148, 216, 176, 255),
            (148, 206, 248, 255),
            (204, 176, 244, 255),
        ]
        for idx, fc in enumerate(flag_colors):
            fx = 12 * S + idx * 12 * S
            fy = int((11 - abs(idx - 2) * 1.6) * S)
            d.polygon([(fx - 4.5 * S, fy), (fx + 4.5 * S, fy), (fx, fy + 10 * S)], fill=fc)
            d.ellipse((fx - 1.5 * S, fy + 2 * S, fx + 1.5 * S, fy + 5 * S), fill=(255, 250, 246, 255))

        # Floating mini oak wall shelf
        d.rounded_rectangle((10 * S, 37 * S, 62 * S, 43 * S), radius=2 * S, fill=(140, 108, 86, 42))
        d.rounded_rectangle((9 * S, 35 * S, 61 * S, 41 * S), radius=2.5 * S, fill=(224, 174, 126, 255))
        d.rounded_rectangle((10 * S, 35 * S, 60 * S, 37 * S), radius=1.5 * S, fill=(248, 212, 174, 255))
        for bx, bh, bc in ((15, 13, (124, 184, 238, 255)),
                           (21, 15, (246, 146, 168, 255)),
                           (27, 12, (134, 204, 162, 255))):
            d.rounded_rectangle((bx * S, (35 - bh) * S, (bx + 5) * S, 35 * S),
                                radius=1 * S, fill=bc)
        d.ellipse((43 * S, 24 * S, 53 * S, 35 * S), fill=(255, 248, 240, 255))
        d.ellipse((45 * S, 26 * S, 48 * S, 29 * S), fill=(255, 255, 255, 255))

    else:
        # Sculpted Cat-Ear Ceramic Wall Clock
        cx, cy = 36 * S, 24 * S
        r_out = 16 * S
        d.ellipse((cx - r_out + 2 * S, cy - r_out + 2 * S, cx + r_out + 2 * S, cy + r_out + 2 * S),
                  fill=(130, 100, 82, 46))
        for sgn in (-1, 1):
            ex = cx + sgn * 10.5 * S
            ey = cy - 14 * S
            d.ellipse((ex - 5 * S, ey - 6.5 * S, ex + 5 * S, ey + 6.5 * S), fill=(244, 198, 158, 255))
            d.ellipse((ex - 2.8 * S, ey - 3.8 * S, ex + 2.8 * S, ey + 3.8 * S), fill=(255, 192, 200, 255))
        d.ellipse((cx - r_out, cy - r_out, cx + r_out, cy + r_out), fill=(244, 198, 158, 255))
        d.ellipse((cx - r_out + 1 * S, cy - r_out + 1 * S, cx + r_out - 2 * S, cy + r_out - 2 * S),
                  fill=(255, 224, 190, 255))
        r_in = 12 * S
        d.ellipse((cx - r_in, cy - r_in, cx + r_in, cy + r_in), fill=(255, 251, 244, 255))
        for k in range(4):
            a = k * math.pi / 2.0
            hx = cx + math.cos(a) * 9 * S
            hy = cy + math.sin(a) * 9 * S
            d.ellipse((hx - 1.3 * S, hy - 1.3 * S, hx + 1.3 * S, hy + 1.3 * S),
                      fill=(242, 134, 154, 255))
        d.line((cx, cy, cx, cy - 7 * S), fill=(116, 74, 52, 255), width=2 * S)
        d.line((cx, cy, cx + 5.5 * S, cy - 2 * S), fill=(116, 74, 52, 255), width=2 * S)
        d.ellipse((cx - 2 * S, cy - 2 * S, cx + 2 * S, cy + 2 * S), fill=(238, 184, 72, 255))
        d.arc((cx - 10 * S, cy - 10 * S, cx + 10 * S, cy + 10 * S),
              start=200, end=270, fill=(232, 242, 252, 255), width=2 * S)
        d.line((cx - 4 * S, cy + r_out - S, cx - 4 * S, 45 * S), fill=(214, 168, 76, 255), width=S)
        d.line((cx + 4 * S, cy + r_out - S, cx + 4 * S, 43 * S), fill=(214, 168, 76, 255), width=S)
        d.ellipse((cx - 6 * S, 44 * S, cx - 2 * S, 48 * S), fill=(242, 194, 82, 255))
        d.ellipse((cx + 2 * S, 42 * S, cx + 6 * S, 46 * S), fill=(242, 194, 82, 255))

    return im.resize((W, H), Image.Resampling.LANCZOS)


# ---------------------------------------------------------------------------
# Rugs (140x30) — Perspective Boucle Plush Rugs
# ---------------------------------------------------------------------------

def render_rug(variant: int) -> Image.Image:
    W, H = 140, 30
    S = 2
    w2, h2 = W * S, H * S
    im = Image.new("RGBA", (w2, h2), (0, 0, 0, 0))
    px = im.load()

    cx, cy = w2 * 0.5, h2 * 0.52
    rx_main, ry_main = w2 * 0.45, h2 * 0.37

    for py_i in range(h2):
        for px_i in range(w2):
            dx = (px_i - cx) / rx_main
            dy = (py_i - cy) / ry_main

            if variant == 3:
                # 3D Cat-Paw Shaped Rug (central pad + 4 toe beans on upper arch)
                d_main = math.sqrt((dx * 1.15) ** 2 + ((dy - 0.12) * 1.12) ** 2) - 0.72
                toes = [(-0.56, -0.52), (-0.20, -0.74), (0.20, -0.74), (0.56, -0.52)]
                for tx, ty in toes:
                    dt = math.sqrt(((dx - tx) * 2.8) ** 2 + ((dy - ty) * 2.3) ** 2) - 0.58
                    d_main = smin(d_main, dt, 0.14)
                dist = d_main + 1.0
            else:
                # Soft oval/pill rug with gentle wavy edge for rainbow
                wave = 0.025 * math.sin(dx * 14.0) if variant == 2 else 0.0
                dist = math.sqrt(dx * dx + (dy + wave) * (dy + wave))

            # Soft shadow halo slightly below rug
            sh_dy = (py_i - (cy + 3.0)) / (ry_main * 1.04)
            sh_dist = math.sqrt((dx * 0.98) ** 2 + sh_dy * sh_dy)

            fuzz = (fbm2(px_i * 0.6, py_i * 0.6) - 0.5) * 0.05
            d_eff = dist + fuzz

            if d_eff < 1.04:
                # Boucle plush surface shading (lit top -> soft lip edge)
                shade = lerp(1.0, 0.86, clamp((dy + 1.0) * 0.5))
                if dist > 0.88:
                    shade *= 0.92
                noise = (fbm2(px_i * 0.45, py_i * 0.45) - 0.5) * 18.0

                if variant == 1:
                    # Cream polka-dot boucle rug
                    r, g, b = 255, 248, 236
                    dots = [
                        (-0.52, -0.15, (250, 152, 174)),
                        (-0.22,  0.28, (255, 212, 102)),
                        ( 0.08, -0.25, (148, 214, 176)),
                        ( 0.36,  0.22, (148, 202, 246)),
                        ( 0.62, -0.12, (250, 152, 174)),
                    ]
                    for dot_x, dot_y, col in dots:
                        dd = math.sqrt(((dx - dot_x) * 2.2) ** 2 + (dy - dot_y) ** 2)
                        if dd < 0.30:
                            mix = smoothstep(0.30, 0.22, dd)
                            r = lerp(r, col[0], mix)
                            g = lerp(g, col[1], mix)
                            b = lerp(b, col[2], mix)
                elif variant == 2:
                    # Soft pastel rainbow striped rug
                    stripes = [
                        (252, 164, 180),
                        (255, 206, 146),
                        (255, 232, 142),
                        (166, 224, 184),
                        (162, 212, 248),
                        (206, 184, 244),
                    ]
                    band = clamp((dy + 0.95) / 1.90) * (len(stripes) - 0.01)
                    idx = int(band)
                    frac = band - idx
                    c0 = stripes[idx]
                    c1 = stripes[min(idx + 1, len(stripes) - 1)]
                    m = smoothstep(0.72, 1.0, frac)
                    r = lerp(c0[0], c1[0], m)
                    g = lerp(c0[1], c1[1], m)
                    b = lerp(c0[2], c1[2], m)
                else:
                    # Variant 3: Cream plush paw rug with sakura pink pads
                    r, g, b = 255, 247, 238
                    pad_c = math.sqrt((dx * 1.9) ** 2 + ((dy - 0.14) * 1.9) ** 2)
                    in_pad = pad_c < 0.66
                    for tx, ty in ((-0.54, -0.48), (-0.19, -0.68), (0.19, -0.68), (0.54, -0.48)):
                        if math.sqrt(((dx - tx) * 4.5) ** 2 + ((dy - ty) * 3.8) ** 2) < 0.56:
                            in_pad = True
                    if in_pad:
                        r, g, b = 255, 178, 198

                r = int(clamp(r * shade + noise, 0, 255))
                g = int(clamp(g * shade + noise * 0.95, 0, 255))
                b = int(clamp(b * shade + noise * 0.9, 0, 255))
                cov = 1.0 if d_eff < 0.96 else clamp((1.04 - d_eff) / 0.08)
                px[px_i, py_i] = (r, g, b, int(cov * 255))
            elif variant != 3 and sh_dist < 1.04:
                a = int(52 * ((1.0 - clamp(sh_dist / 1.04)) ** 1.5))
                if a > 0:
                    px[px_i, py_i] = (104, 72, 52, a)

    return im.resize((W, H), Image.Resampling.LANCZOS)


# ---------------------------------------------------------------------------
# LV_COLOR_FORMAT_I8 Quantizer & C/H Emitter
# ---------------------------------------------------------------------------

def encode_i8(im: Image.Image) -> tuple[int, int, int, bytes]:
    """Quantize an RGBA image to 256-entry LV_COLOR_FORMAT_I8 (index 0 = transparent)."""
    w, h = im.size
    rgba = im.convert("RGBA")
    raw = rgba.load()

    # Clean tiny alpha noise (< 4) to exact transparent (0,0,0,0)
    for y in range(h):
        for x in range(w):
            r, g, b, a = raw[x, y]
            if a < 4:
                raw[x, y] = (0, 0, 0, 0)

    # Quantize with 255 slots so index 0 can stay strictly (0, 0, 0, 0)
    q = rgba.quantize(colors=255, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    pal_raw = q.getpalette("RGBA") or []
    n_pal = len(pal_raw) // 4
    palette: list[tuple[int, int, int, int]] = [(0, 0, 0, 0)]
    for i in range(n_pal):
        palette.append((
            pal_raw[i * 4 + 0],
            pal_raw[i * 4 + 1],
            pal_raw[i * 4 + 2],
            pal_raw[i * 4 + 3],
        ))
    while len(palette) < 256:
        palette.append((0, 0, 0, 0))
    palette = palette[:256]

    q_px = q.load()
    indices = bytearray(w * h)
    for y in range(h):
        row_off = y * w
        for x in range(w):
            if raw[x, y][3] == 0:
                indices[row_off + x] = 0
            else:
                idx = q_px[x, y] + 1
                indices[row_off + x] = idx if idx < 256 else 255

    buf = bytearray()
    # LVGL 9 lv_color32_t layout: blue, green, red, alpha
    for r, g, b, a in palette:
        buf.extend((b, g, r, a))
    buf.extend(indices)
    assert len(buf) == 1024 + w * h
    return w, h, w, bytes(buf)


def build_all_sprites() -> dict[str, Image.Image]:
    sprites: dict[str, Image.Image] = {}
    for pose in ("focus_0", "focus_1", "focus_2", "focus_3"):
        sprites[f"ecard_cat_{pose}"] = render_cat_pose(pose)
    for pose in ("idle", "paused", "break", "done"):
        sprites[f"ecard_cat_pose_{pose}"] = render_cat_pose(pose)

    for v in range(4):
        sprites[f"ecard_chair_{v}"] = render_chair(v)
    for v in (1, 2, 3):
        sprites[f"ecard_lamp_{v}"] = render_lamp(v)
    for v in range(4):
        sprites[f"ecard_wdecor_{v}"] = render_wdecor(v)
    for v in range(4):
        sprites[f"ecard_desk_{v}"] = render_desk_item(v)
    for v in (1, 2, 3):
        sprites[f"ecard_rug_{v}"] = render_rug(v)

    sprites["ecard_desk_table"] = render_desk_table()
    sprites["ecard_lamp_glow"] = render_lamp_glow()
    return sprites


def main() -> None:
    t0 = time.time()
    sprites = build_all_sprites()

    c_lines: list[str] = [
        "/* Generated by tools/generate_cat_sprites.py. Do not edit by hand. */",
        '#include "lvgl.h"',
        '#include "ecard_cat_sprites.h"',
        "",
    ]

    total_bytes = 0
    for name, im in sprites.items():
        w, h, stride, payload = encode_i8(im)
        total_bytes += len(payload)

        c_lines.append(f"static const uint8_t {name}_map[] LV_ATTRIBUTE_MEM_ALIGN = {{")
        for i in range(0, len(payload), 16):
            chunk = ", ".join(f"0x{b:02X}" for b in payload[i : i + 16])
            c_lines.append(f"    {chunk},")
        c_lines.append("};")
        c_lines.append("")
        c_lines.append(f"const lv_image_dsc_t {name} = {{")
        c_lines.append("    .header.magic = LV_IMAGE_HEADER_MAGIC,")
        c_lines.append("    .header.cf = LV_COLOR_FORMAT_I8,")
        c_lines.append(f"    .header.w = {w},")
        c_lines.append(f"    .header.h = {h},")
        c_lines.append(f"    .header.stride = {stride},")
        c_lines.append(f"    .data_size = sizeof({name}_map),")
        c_lines.append(f"    .data = {name}_map,")
        c_lines.append("};")
        c_lines.append("")

    c_lines.extend([
        "const lv_image_dsc_t *const ecard_cat_focus_frames[4] = {",
        "    &ecard_cat_focus_0, &ecard_cat_focus_1, &ecard_cat_focus_2, &ecard_cat_focus_3,",
        "};",
        "const lv_image_dsc_t *const ecard_sprite_chairs[4] = {",
        "    &ecard_chair_0, &ecard_chair_1, &ecard_chair_2, &ecard_chair_3,",
        "};",
        "const lv_image_dsc_t *const ecard_sprite_lamps[4] = {",
        "    NULL, &ecard_lamp_1, &ecard_lamp_2, &ecard_lamp_3,",
        "};",
        "const lv_image_dsc_t *const ecard_sprite_wall_decors[4] = {",
        "    &ecard_wdecor_0, &ecard_wdecor_1, &ecard_wdecor_2, &ecard_wdecor_3,",
        "};",
        "const lv_image_dsc_t *const ecard_sprite_desk_items[4] = {",
        "    &ecard_desk_0, &ecard_desk_1, &ecard_desk_2, &ecard_desk_3,",
        "};",
        "const lv_image_dsc_t *const ecard_sprite_rugs[4] = {",
        "    NULL, &ecard_rug_1, &ecard_rug_2, &ecard_rug_3,",
        "};",
        "",
    ])

    assert total_bytes <= MAX_TOTAL_BYTES, (
        f"Sprite Flash budget exceeded: {total_bytes} > {MAX_TOTAL_BYTES}"
    )

    h_lines = [
        "#ifndef ECARD_CAT_SPRITES_H",
        "#define ECARD_CAT_SPRITES_H",
        "",
        '#include "lvgl.h"',
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "extern const lv_image_dsc_t ecard_cat_focus_0;",
        "extern const lv_image_dsc_t ecard_cat_focus_1;",
        "extern const lv_image_dsc_t ecard_cat_focus_2;",
        "extern const lv_image_dsc_t ecard_cat_focus_3;",
        "extern const lv_image_dsc_t ecard_cat_pose_idle;",
        "extern const lv_image_dsc_t ecard_cat_pose_paused;",
        "extern const lv_image_dsc_t ecard_cat_pose_break;",
        "extern const lv_image_dsc_t ecard_cat_pose_done;",
        "extern const lv_image_dsc_t ecard_desk_table;",
        "extern const lv_image_dsc_t ecard_lamp_glow;",
        "",
        "extern const lv_image_dsc_t *const ecard_cat_focus_frames[4];",
        "extern const lv_image_dsc_t *const ecard_sprite_chairs[4];",
        "extern const lv_image_dsc_t *const ecard_sprite_lamps[4];",
        "extern const lv_image_dsc_t *const ecard_sprite_wall_decors[4];",
        "extern const lv_image_dsc_t *const ecard_sprite_desk_items[4];",
        "extern const lv_image_dsc_t *const ecard_sprite_rugs[4];",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        "#endif // ECARD_CAT_SPRITES_H",
        "",
    ]

    OUT_C.write_text("\n".join(c_lines), encoding="utf-8")
    OUT_H.write_text("\n".join(h_lines), encoding="utf-8")
    elapsed = time.time() - t0
    print(
        f"Generated {len(sprites)} I8 sprites in {elapsed:.2f}s: "
        f"{total_bytes} bytes ({total_bytes / 1024:.1f} KB <= {MAX_TOTAL_BYTES / 1024:.0f} KB)"
    )


if __name__ == "__main__":
    main()
