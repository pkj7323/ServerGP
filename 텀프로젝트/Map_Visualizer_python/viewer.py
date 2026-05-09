import numpy as np
from PIL import Image
import os

WORLD_WIDTH = 2000
WORLD_HEIGHT = 2000

# 1. 시각적 타일 색상 매핑 (RGB)
TILE_COLORS = {
    0: [65, 105, 225],  # WATER (파랑)
    1: [34, 139, 34],  # GRASS (초록)
    2: [139, 69, 19],  # DIRT (흙색)
    3: [128, 128, 128],  # STONE (회색)
    4: [238, 214, 175],  # SAND (모래색)
    5: [210, 180, 140],  # SANDSTONE (진한 모래색)
    6: [255, 250, 250],  # SNOW (흰색)
    7: [175, 238, 238],  # ICE (하늘색)
    8: [101, 67, 33],  # MUD (진흙색)
    9: [47, 79, 79]  # DEEPSLATE (어두운 회색)
}


def create_image(filename, out_filename, color_dict, is_collision=False):
    if not os.path.exists(filename):
        print(f"파일을 찾을 수 없습니다: {filename}")
        return

    # 바이너리 데이터 읽기
    with open(filename, 'rb') as f:
        raw_data = f.read()

    # 1차원 바이트 배열을 2000x2000 2차원 Numpy 배열로 변환
    data_array = np.frombuffer(raw_data, dtype=np.uint8).reshape((WORLD_HEIGHT, WORLD_WIDTH))

    # 색상을 담을 빈 이미지 배열 생성 (2000x2000x3 RGB)
    img_array = np.zeros((WORLD_HEIGHT, WORLD_WIDTH, 3), dtype=np.uint8)

    # 데이터 값에 맞춰 색상 채우기
    if is_collision:
        # 충돌체: 0(통과)은 하얀색, 1(장애물)은 검은색
        img_array[data_array == 0] = [255, 255, 255]
        img_array[data_array == 1] = [0, 0, 0]
    else:
        for tile_id, color in color_dict.items():
            img_array[data_array == tile_id] = color

    # 중요: 기존 좌표계(Y-up)에 맞춰 이미지 상하 반전 (배열의 0행이 맨 아래로 가도록)
    img_array = np.flipud(img_array)

    # PNG 이미지로 저장
    img = Image.fromarray(img_array, 'RGB')
    img.save(out_filename)
    print(f"이미지 저장 완료: {out_filename}")


if __name__ == "__main__":
    print("맵 시각화 변환을 시작합니다...")
    create_image('map_visual.bin', 'map_visual.png', TILE_COLORS, is_collision=False)
    create_image('map_collision.bin', 'map_collision.png', None, is_collision=True)