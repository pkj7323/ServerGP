#include <iostream>
#include <vector>
#include <fstream>
#include "FastNoiseLite.h" // https://github.com/Auburn/FastNoiseLite

constexpr int WORLD_WIDTH = 2000;
constexpr int WORLD_HEIGHT = 2000;

// 바이옴 ID (10개)
enum BiomeType {
    BIOME_PLAINS = 0,
    BIOME_DESERT = 1, 
	BIOME_SNOW = 2, 
	BIOME_FOREST = 3, 
	BIOME_JUNGLE = 4,
    BIOME_SWAMP = 5, 
	BIOME_TAIGA = 6, 
	BIOME_SAVANNA = 7, 
	BIOME_BADLANDS = 8, 
	BIOME_OCEAN = 9
};

// 시각적 타일 ID (렌더링용)
enum TileType : uint8_t {
    TILE_WATER = 0,
	TILE_GRASS = 1, 
	TILE_DIRT = 2,
	TILE_STONE = 3,
	TILE_SAND = 4,
    TILE_SANDSTONE = 5,
	TILE_SNOW = 6,
	TILE_ICE = 7, 
	TILE_MUD = 8,
	TILE_DEEPSLATE = 9
};

// 충돌체 데이터 (Bitmask 용도)
constexpr uint8_t COLLISION_PASSABLE = 0;
constexpr uint8_t COLLISION_BLOCKED = 1;

int main() {
    std::cout << "Generating Map Data..." << std::endl;

    // 1. 바이옴 생성을 위한 Cellular (Voronoi) 노이즈 세팅
    FastNoiseLite biomeNoise;
    biomeNoise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
    biomeNoise.SetFrequency(0.002f); // 값이 작을수록 바이옴 구역이 넓어짐
    biomeNoise.SetCellularReturnType(FastNoiseLite::CellularReturnType_CellValue);

    // 2. 지형 디테일(물, 땅, 산)을 위한 Perlin 노이즈 세팅
    FastNoiseLite terrainNoise;
    terrainNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    terrainNoise.SetFrequency(0.01f);

    // 3. 데이터 컨테이너 할당 (2000x2000 = 4,000,000 bytes = 약 3.8MB)
    std::vector<uint8_t> visualData(WORLD_WIDTH * WORLD_HEIGHT);
    std::vector<uint8_t> collisionData(WORLD_WIDTH * WORLD_HEIGHT);

    for (int y = 0; y < WORLD_HEIGHT; y++) {
        for (int x = 0; x < WORLD_WIDTH; x++) {
            int index = y * WORLD_WIDTH + x;

            // -1.0 ~ 1.0 사이의 노이즈 값 추출
            float bNoise = biomeNoise.GetNoise((float)x, (float)y);
            float tNoise = terrainNoise.GetNoise((float)x, (float)y);

            // 노이즈 값을 0 ~ 9 사이의 바이옴 ID로 매핑
            int biome = static_cast<int>((bNoise + 1.0f) * 0.5f * 10.0f);
            if (biome > 9) biome = 9; // Clamp

            uint8_t visualID = TILE_GRASS;
            uint8_t collisionFlag = COLLISION_PASSABLE;

            // 바이옴과 지형 높낮이에 따른 타일 1:1 대응 및 충돌 세팅
            if (biome == BIOME_DESERT) {
                if (tNoise < -0.2f) { visualID = TILE_WATER; collisionFlag = COLLISION_BLOCKED; } // 오아시스
                else if (tNoise > 0.5f) { visualID = TILE_SANDSTONE; collisionFlag = COLLISION_BLOCKED; } // 사암 절벽
                else { visualID = TILE_SAND; collisionFlag = COLLISION_PASSABLE; }
            }
            else if (biome == BIOME_SNOW) {
                if (tNoise < -0.2f) { visualID = TILE_ICE; collisionFlag = COLLISION_BLOCKED; } // 얼어붙은 강
                else if (tNoise > 0.6f) { visualID = TILE_STONE; collisionFlag = COLLISION_BLOCKED; } // 돌산
                else { visualID = TILE_SNOW; collisionFlag = COLLISION_PASSABLE; }
            }
            else { // 기본 평원 및 기타
                if (tNoise < -0.3f) { visualID = TILE_WATER; collisionFlag = COLLISION_BLOCKED; } // 호수/강
                else if (tNoise > 0.5f) { visualID = TILE_STONE; collisionFlag = COLLISION_BLOCKED; } // 산맥
                else { visualID = TILE_GRASS; collisionFlag = COLLISION_PASSABLE; }
            }

            visualData[index] = visualID;
            collisionData[index] = collisionFlag;
        }
    }

    // 4. 바이너리 파일로 저장 (서버 로딩 속도 최적화)
    std::ofstream visualOut("map_visual.bin", std::ios::binary);
    visualOut.write(reinterpret_cast<const char*>(visualData.data()), visualData.size());
    visualOut.close();

    std::ofstream collisionOut("map_collision.bin", std::ios::binary);
    collisionOut.write(reinterpret_cast<const char*>(collisionData.data()), collisionData.size());
    collisionOut.close();

    std::cout << "Map generated successfully! (map_visual.bin, map_collision.bin)" << std::endl;
    return 0;
}