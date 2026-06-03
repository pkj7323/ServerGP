import numpy as np
from PIL import Image, ImageDraw, ImageFont
import os

WORLD_WIDTH  = 2000
WORLD_HEIGHT = 2000

# ── 타일 색상 (기존) ──────────────────────────────────────────────────────────
TILE_COLORS = {
    0: [65,  105, 225],  # WATER      (파랑)
    1: [34,  139,  34],  # GRASS      (초록)
    2: [139,  69,  19],  # DIRT       (흙)
    3: [128, 128, 128],  # STONE      (회색)
    4: [238, 214, 175],  # SAND       (모래색)
    5: [210, 180, 140],  # SANDSTONE  (진한 모래)
    6: [255, 250, 250],  # SNOW       (흰색)
    7: [175, 238, 238],  # ICE        (하늘색)
    8: [101,  67,  33],  # MUD        (진흙)
    9: [ 47,  79,  79],  # DEEPSLATE  (짙은 회색)
    10: [ 10,  80,  10], # OAK TREE   (진한 초록)
    11: [ 15,  50,  30], # SPRUCE TREE(어두운 청록)
    12: [ 50, 205,  50], # CACTUS     (라임 초록)
}

# ── NPC 스폰 색상 ─────────────────────────────────────────────────────────────
NPC_COLORS = {
    0: None,               # NPC_NONE       (투명 - 표시 안 함)
    1: [255,  80,  80],    # NPC_ZOMBIE     (붉은색)
    2: [200, 200, 255],    # NPC_SKELETON   (밝은 파랑)
    3: [0,   200,   0],    # NPC_CREEPER    (밝은 초록)
    4: [160,   0, 255],    # NPC_ENDERMAN   (보라)
    5: [255, 200,   0],    # NPC_IRON_GOLEM (금색)
}

NPC_NAMES = {
    1: "Zombie",
    2: "Skeleton",
    3: "Creeper",
    4: "Enderman",
    5: "Iron Golem",
}

# ── 마을 중심 ─────────────────────────────────────────────────────────────────
CENTER_X   = WORLD_WIDTH  // 2  # 1000
CENTER_Y   = WORLD_HEIGHT // 2  # 1000
TOWN_HALF  = 15


# ── 1. 지형 이미지 생성 (기존 기능 유지) ──────────────────────────────────────
def create_visual_image(bin_path, out_path):
    if not os.path.exists(bin_path):
        print(f"[SKIP] 파일 없음: {bin_path}")
        return None

    raw = np.frombuffer(open(bin_path, 'rb').read(), dtype=np.uint8)
    data = raw.reshape((WORLD_HEIGHT, WORLD_WIDTH))

    img_arr = np.zeros((WORLD_HEIGHT, WORLD_WIDTH, 3), dtype=np.uint8)
    for tile_id, color in TILE_COLORS.items():
        img_arr[data == tile_id] = color

    img_arr = np.flipud(img_arr)
    img = Image.fromarray(img_arr, 'RGB')
    img.save(out_path)
    print(f"[OK] {out_path}")
    return img_arr  # 후속 overlay에서 재사용


# ── 2. 충돌 이미지 생성 (기존 기능 유지) ──────────────────────────────────────
def create_collision_image(bin_path, out_path):
    if not os.path.exists(bin_path):
        print(f"[SKIP] 파일 없음: {bin_path}")
        return

    raw = np.frombuffer(open(bin_path, 'rb').read(), dtype=np.uint8)
    data = raw.reshape((WORLD_HEIGHT, WORLD_WIDTH))

    img_arr = np.zeros((WORLD_HEIGHT, WORLD_WIDTH, 3), dtype=np.uint8)
    img_arr[data == 0] = [255, 255, 255]  # PASSABLE → 흰색
    img_arr[data == 1] = [0,   0,   0  ]  # BLOCKED  → 검정

    img_arr = np.flipud(img_arr)
    Image.fromarray(img_arr, 'RGB').save(out_path)
    print(f"[OK] {out_path}")


# ── 3. 스폰 맵 단독 이미지 (검정 배경에 NPC 색상 점) ─────────────────────────
def create_spawn_image(bin_path, out_path):
    if not os.path.exists(bin_path):
        print(f"[SKIP] 파일 없음: {bin_path}")
        return None

    raw = np.frombuffer(open(bin_path, 'rb').read(), dtype=np.uint8)
    data = raw.reshape((WORLD_HEIGHT, WORLD_WIDTH))

    img_arr = np.zeros((WORLD_HEIGHT, WORLD_WIDTH, 3), dtype=np.uint8)  # 검정 배경

    for npc_id, color in NPC_COLORS.items():
        if color is None:
            continue
        img_arr[data == npc_id] = color

    # 마을 중심 표시 (흰색 30×30 사각형)
    town_y0 = WORLD_HEIGHT - 1 - (CENTER_Y + TOWN_HALF)
    town_y1 = WORLD_HEIGHT - 1 - (CENTER_Y - TOWN_HALF)
    town_x0 = CENTER_X - TOWN_HALF
    town_x1 = CENTER_X + TOWN_HALF
    img_arr[town_y0:town_y1+1, town_x0:town_x1+1] = [255, 255, 255]

    img_arr = np.flipud(img_arr)
    img = Image.fromarray(img_arr, 'RGB')
    _draw_legend(img)
    img.save(out_path)
    print(f"[OK] {out_path}")
    return data  # overlay에서 재사용


# ── 4. 오버레이 이미지 (지형 위에 NPC 점 합성) ───────────────────────────────
def create_overlay_image(visual_bin, spawn_bin, out_path):
    if not os.path.exists(visual_bin) or not os.path.exists(spawn_bin):
        print(f"[SKIP] 오버레이 생성 불가 (파일 누락)")
        return

    # 지형 배열
    raw_v = np.frombuffer(open(visual_bin, 'rb').read(), dtype=np.uint8)
    vis   = raw_v.reshape((WORLD_HEIGHT, WORLD_WIDTH))

    # 스폰 배열
    raw_s = np.frombuffer(open(spawn_bin, 'rb').read(), dtype=np.uint8)
    spn   = raw_s.reshape((WORLD_HEIGHT, WORLD_WIDTH))

    # 지형을 RGB로 변환
    img_arr = np.zeros((WORLD_HEIGHT, WORLD_WIDTH, 3), dtype=np.uint8)
    for tile_id, color in TILE_COLORS.items():
        img_arr[vis == tile_id] = color

    # NPC 색상 덮어쓰기
    for npc_id, color in NPC_COLORS.items():
        if color is None:
            continue
        img_arr[spn == npc_id] = color

    # 마을 중심 흰색 표시
    img_arr[CENTER_Y - TOWN_HALF : CENTER_Y + TOWN_HALF + 1,
            CENTER_X - TOWN_HALF : CENTER_X + TOWN_HALF + 1] = [255, 255, 255]

    img_arr = np.flipud(img_arr)
    img = Image.fromarray(img_arr, 'RGB')
    _draw_legend(img)
    img.save(out_path)
    print(f"[OK] {out_path}")


# ── 5. 스폰 존별 통계 출력 ───────────────────────────────────────────────────
def print_spawn_stats(bin_path):
    if not os.path.exists(bin_path):
        print(f"[SKIP] 파일 없음: {bin_path}")
        return

    raw  = np.frombuffer(open(bin_path, 'rb').read(), dtype=np.uint8)
    data = raw.reshape((WORLD_HEIGHT, WORLD_WIDTH))

    print("\n=== Spawn Statistics ===")
    total = 0
    for npc_id, name in NPC_NAMES.items():
        count = int(np.sum(data == npc_id))
        total += count
        bar = "#" * (count // 2000)
        print(f"  [{npc_id}] {name:<12} : {count:>7,}  {bar}")
    print(f"  {'TOTAL':<15} : {total:>7,}")
    print("========================\n")


# ── 레전드 그리기 헬퍼 ────────────────────────────────────────────────────────
def _draw_legend(img: Image.Image):
    draw  = ImageDraw.Draw(img)
    items = [
        ([255, 255, 255], "Town (Spawn)"),
        ([255,  80,  80], "Zombie"),
        ([200, 200, 255], "Skeleton"),
        ([0,   200,   0], "Creeper"),
        ([160,   0, 255], "Enderman"),
        ([255, 200,   0], "Iron Golem"),
    ]
    x, y, box, gap, pad = 10, 10, 14, 4, 4
    # 배경 반투명 박스
    legend_h = len(items) * (box + gap) + pad * 2
    draw.rectangle([x - pad, y - pad, x + 130, y + legend_h], fill=(0, 0, 0, 180))
    for color, label in items:
        draw.rectangle([x, y, x + box, y + box], fill=tuple(color))
        draw.text((x + box + 4, y), label, fill=(255, 255, 255))
        y += box + gap


# ── 메인 ──────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    BIN_DIR = os.path.dirname(os.path.abspath(__file__))

    visual_bin    = os.path.join(BIN_DIR, "../Data/map_visual.bin")
    collision_bin = os.path.join(BIN_DIR, "../Data/map_collision.bin")
    spawn_bin     = os.path.join(BIN_DIR, "../Data/map_spawn.bin")

    print("=== Map Visualizer ===")

    # 기존 이미지
    create_visual_image   (visual_bin,    os.path.join(BIN_DIR, "map_visual.png"))
    create_collision_image(collision_bin, os.path.join(BIN_DIR, "map_collision.png"))

    # 신규: 스폰 이미지
    create_spawn_image    (spawn_bin,     os.path.join(BIN_DIR, "map_spawn.png"))

    # 신규: 오버레이 이미지 (지형 + 스폰 합성)
    create_overlay_image  (visual_bin, spawn_bin,
                           os.path.join(BIN_DIR, "map_overlay.png"))

    # 신규: 통계
    print_spawn_stats(spawn_bin)

    print("=== Done! ===")
    print("Generated files:")
    for f in ["map_visual.png", "map_collision.png", "map_spawn.png", "map_overlay.png"]:
        path = os.path.join(BIN_DIR, f)
        if os.path.exists(path):
            size_kb = os.path.getsize(path) // 1024
            print(f"  {f:<25} ({size_kb:,} KB)")