import struct

WORLD_WIDTH  = 2000
WORLD_HEIGHT = 2000

# 보스 스테이지 영역: 스폰(1000,1000)에서 y축으로 800칸 위 → 중심 (1000, 1800)
# 50x50 사각형: x: 975~1024, y: 1775~1824
BOSS_CENTER_X = 1000
BOSS_CENTER_Y = 1800
HALF = 25

X0 = BOSS_CENTER_X - HALF  # 975
X1 = BOSS_CENTER_X + HALF  # 1025 (exclusive)
Y0 = BOSS_CENTER_Y - HALF  # 1775
Y1 = BOSS_CENTER_Y + HALF  # 1825 (exclusive)

TILE_END_STONE = 15
TILE_STONE = 3

def get_offset(x, y):
    return y * WORLD_WIDTH + x

def patch_visual():
    path = 'Data/map_visual.bin'
    with open(path, 'r+b') as f:
        for y in range(Y0, Y1):
            for x in range(X0, X1):
                f.seek(get_offset(x, y))
                is_border = (x == X0 or x == X1 - 1 or y == Y0 or y == Y1 - 1)
                tile = TILE_STONE if is_border else TILE_END_STONE
                f.write(bytes([tile]))
    print(f"[OK] map_visual.bin patched: Boss room with stone border ({X0},{Y0}) ~ ({X1-1},{Y1-1})")

def patch_collision():
    path = 'Data/map_collision.bin'
    with open(path, 'r+b') as f:
        for y in range(Y0, Y1):
            for x in range(X0, X1):
                f.seek(get_offset(x, y))
                is_border = (x == X0 or x == X1 - 1 or y == Y0 or y == Y1 - 1)
                f.write(bytes([1 if is_border else 0]))  # 테두리 충돌, 내부 통과
    print(f"[OK] map_collision.bin patched: Border impassable, interior walkable")

patch_visual()
patch_collision()
print("Boss stage map patch complete!")
