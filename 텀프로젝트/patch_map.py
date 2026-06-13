import os

width = 2000
height = 2000

def get_offset(x, y):
    return y * width + x

frames = [
    (999, 1010), (1000, 1010), (1001, 1010), # Top
    (999, 1014), (1000, 1014), (1001, 1014), # Bottom
    (998, 1011), (998, 1012), (998, 1013),   # Left
    (1002, 1011), (1002, 1012), (1002, 1013) # Right
]

portals = []
for x in range(999, 1002):
    for y in range(1011, 1014):
        portals.append((x, y))

def patch_map():
    # Visual Map Patching
    with open('Data/map_visual.bin', 'r+b') as f:
        for x, y in frames:
            f.seek(get_offset(x, y))
            f.write(bytes([13])) # 13 is end_portal_frame
        for x, y in portals:
            f.seek(get_offset(x, y))
            f.write(bytes([14])) # 14 is end_portal

    # Collision Map Patching
    with open('Data/map_collision.bin', 'r+b') as f:
        for x, y in frames:
            f.seek(get_offset(x, y))
            f.write(bytes([0])) # Collision off for frames
        for x, y in portals:
            f.seek(get_offset(x, y))
            f.write(bytes([0])) # Collision off for portals

patch_map()
print("Map patched successfully!")
