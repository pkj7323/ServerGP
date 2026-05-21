#include <algorithm>
#include <iostream>
#include <vector>
#include <fstream>
#include <cmath>
#include <random>
#include "FastNoiseLite.h" // https://github.com/Auburn/FastNoiseLite
#include "Protocol.h"


// ===== 바이옴 ID (10종) =====
enum BiomeType {
    BIOME_PLAINS  = 0,
    BIOME_DESERT  = 1,
    BIOME_SNOW    = 2,
    BIOME_FOREST  = 3,
    BIOME_JUNGLE  = 4,
    BIOME_SWAMP   = 5,
    BIOME_TAIGA   = 6,
    BIOME_SAVANNA = 7,
    BIOME_BADLANDS = 8,
    BIOME_OCEAN   = 9
};

// ===== 타일 시각 ID =====
enum TileType : uint8_t {
    TILE_WATER     = 0,
    TILE_GRASS     = 1,
    TILE_DIRT      = 2,
    TILE_STONE     = 3,
    TILE_SAND      = 4,
    TILE_SANDSTONE = 5,
    TILE_SNOW      = 6,
    TILE_ICE       = 7,
    TILE_MUD       = 8,
    TILE_DEEPSLATE = 9
};

// ===== NPC 스폰 타입 (map_spawn.bin에 저장되는 값) =====
enum NpcType : uint8_t {
    NPC_NONE       = 0,  // 스폰 없음
    NPC_ZOMBIE     = 1,
    NPC_SKELETON   = 2,
    NPC_CREEPER    = 3,
    NPC_ENDERMAN   = 4,
    NPC_IRON_GOLEM = 5,
};

// ===== 충돌 플래그 =====
constexpr uint8_t COLLISION_PASSABLE = 0;
constexpr uint8_t COLLISION_BLOCKED  = 1;

// ===== 맵 중심 및 마을 크기 =====
constexpr int CENTER_X   = WORLD_WIDTH  / 2;  // 1000
constexpr int CENTER_Y   = WORLD_HEIGHT / 2;  // 1000
constexpr int TOWN_HALF  = 15;                // 마을: (CENTER+-15) -> 31x31

// ===== 스폰 존 반경 =====
constexpr float GOLEM_R_MIN    = 50.f;
constexpr float GOLEM_R_MAX    = 150.f;
constexpr float ZOMBIE_R_MIN   = 150.f;
constexpr float ZOMBIE_R_MAX   = 400.f;
constexpr float CREEPER_R_MIN  = 400.f;
constexpr float CREEPER_R_MAX  = 700.f;
constexpr float ENDERMAN_R_MIN = 700.f;

// ===== 목표 NPC 마리 수 (합계 = 정확히 200,000) =====
constexpr int COUNT_IRON_GOLEM = 5000;
constexpr int COUNT_ZOMBIE     = 40000;
constexpr int COUNT_SKELETON   = 25000;
constexpr int COUNT_CREEPER    = 70000;
constexpr int COUNT_ENDERMAN   = 60000;
constexpr int COUNT_TOTAL      = COUNT_IRON_GOLEM + COUNT_ZOMBIE + COUNT_SKELETON
                               + COUNT_CREEPER + COUNT_ENDERMAN; // = 200,000

int main() {
    static_assert(COUNT_TOTAL == 200000, "NPC 합계가 200,000이 아닙니다!");

    std::cout << "=== Map Generator ===" << std::endl;
    std::cout << "World: " << WORLD_WIDTH << " x " << WORLD_HEIGHT << std::endl;
    std::cout << "Target NPCs: " << COUNT_TOTAL << std::endl;

    // -- 1. 노이즈 설정 ---------------------------------------------------
    FastNoiseLite biomeNoise;
    biomeNoise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
    biomeNoise.SetFrequency(0.005f);
    biomeNoise.SetCellularReturnType(FastNoiseLite::CellularReturnType_CellValue);

    FastNoiseLite terrainNoise;
    terrainNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    terrainNoise.SetFrequency(0.01f);

    // -- 2. 데이터 버퍼 초기화 --------------------------------------------
    const int TOTAL_CELLS = WORLD_WIDTH * WORLD_HEIGHT;
    std::vector<uint8_t> visualData   (TOTAL_CELLS, TILE_GRASS);
    std::vector<uint8_t> collisionData(TOTAL_CELLS, COLLISION_PASSABLE);
    std::vector<uint8_t> spawnData    (TOTAL_CELLS, NPC_NONE);

    // -- 3. 존별 후보 셀 인덱스 수집 (Pass 1) -----------------------------
    std::vector<int> golem_zone;
    std::vector<int> zombie_skeleton_zone;
    std::vector<int> creeper_zone;
    std::vector<int> enderman_zone;

    golem_zone.reserve(80000);
    zombie_skeleton_zone.reserve(500000);
    creeper_zone.reserve(1100000);
    enderman_zone.reserve(2500000);

    std::cout << "Generating terrain..." << std::endl;

    for (int y = 0; y < WORLD_HEIGHT; y++) {
        for (int x = 0; x < WORLD_WIDTH; x++) {
            const int index = y * WORLD_WIDTH + x;

            float bNoise = biomeNoise.GetNoise((float)x, (float)y);
            float tNoise = terrainNoise.GetNoise((float)x, (float)y);

            int biome = static_cast<int>((bNoise + 1.0f) * 0.5f * 10.0f);
            biome = std::min(biome, 9);

            uint8_t visualID      = TILE_GRASS;
            uint8_t collisionFlag = COLLISION_PASSABLE;

            if (biome == BIOME_DESERT) {
                if      (tNoise < -0.2f) { visualID = TILE_WATER;     collisionFlag = COLLISION_BLOCKED; }
                else if (tNoise >  0.5f) { visualID = TILE_SANDSTONE; collisionFlag = COLLISION_BLOCKED; }
                else                     { visualID = TILE_SAND; }
            }
            else if (biome == BIOME_SNOW) {
                if      (tNoise < -0.2f) { visualID = TILE_ICE; }
                else if (tNoise >  0.6f) { visualID = TILE_STONE; collisionFlag = COLLISION_BLOCKED; }
                else                     { visualID = TILE_SNOW; }
            }
            else {
                if      (tNoise < -0.3f) { visualID = TILE_WATER; }
                else if (tNoise >  0.5f) { visualID = TILE_STONE; collisionFlag = COLLISION_BLOCKED; }
                else                     { visualID = TILE_GRASS; }
            }

            visualData   [index] = visualID;
            collisionData[index] = collisionFlag;

            // 마을 및 막힌 셀은 스폰 후보 제외
            const float dx   = (float)(x - CENTER_X);
            const float dy   = (float)(y - CENTER_Y);
            const float dist = std::sqrtf(dx * dx + dy * dy);

            if (dist < (float)TOWN_HALF)           continue;
            if (collisionFlag == COLLISION_BLOCKED) continue;

            if      (dist >= GOLEM_R_MIN   && dist < GOLEM_R_MAX)  golem_zone.push_back(index);
            else if (dist >= ZOMBIE_R_MIN  && dist < ZOMBIE_R_MAX)  zombie_skeleton_zone.push_back(index);
            else if (dist >= CREEPER_R_MIN && dist < CREEPER_R_MAX) creeper_zone.push_back(index);
            else if (dist >= ENDERMAN_R_MIN)                         enderman_zone.push_back(index);
        }
    }

    std::cout << "Candidate cells:"
              << " Golem="    << golem_zone.size()
              << " Zombie/Skeleton=" << zombie_skeleton_zone.size()
              << " Creeper=" << creeper_zone.size()
              << " Enderman=" << enderman_zone.size() << std::endl;

    // -- 4. 마을 30x30 강제 안전 구역 (Pass 2) ---------------------------
    for (int y = CENTER_Y - TOWN_HALF; y <= CENTER_Y + TOWN_HALF; y++) 
    {
        for (int x = CENTER_X - TOWN_HALF; x <= CENTER_X + TOWN_HALF; x++) 
        {
            const int idx = y * WORLD_WIDTH + x;
            visualData   [idx] = TILE_GRASS;
            collisionData[idx] = COLLISION_PASSABLE;
            spawnData    [idx] = NPC_NONE;
        }
    }

    // -- 5. Shuffle -> 정확히 N개 배치 (Pass 3) --------------------------
    std::mt19937 rng(42); // 고정 시드: 항상 동일한 맵 재현

    auto place_npcs = [&](std::vector<int>& candidates, NpcType type, int count,
                          const char* type_name) {
        if ((int)candidates.size() < count) {
            std::cerr << "[WARNING] " << type_name
                      << ": 후보 셀 부족! 필요=" << count
                      << " 가용=" << candidates.size()
                      << " -> 가용 최대치로 배치" << std::endl;
            count = (int)candidates.size();
        }
        std::shuffle(candidates.begin(), candidates.end(), rng);
        for (int i = 0; i < count; i++) {
            spawnData[candidates[i]] = type;
        }
        std::cout << "  Placed " << count << " " << type_name << std::endl;
    };

    std::cout << "Placing NPCs..." << std::endl;

    // 골렘
    place_npcs(golem_zone, NPC_IRON_GOLEM, COUNT_IRON_GOLEM, "Iron Golem");

    // 좀비 + 스켈레톤: 같은 존 공유 -> shuffle 후 앞/뒤 분리
    {
        const int need_total = COUNT_ZOMBIE + COUNT_SKELETON;
        if ((int)zombie_skeleton_zone.size() < need_total) {
            std::cerr << "[WARNING] Zombie/Skeleton 존 후보 셀 부족! "
                      << "필요=" << need_total
                      << " 가용=" << zombie_skeleton_zone.size() << std::endl;
        }
        std::shuffle(zombie_skeleton_zone.begin(), zombie_skeleton_zone.end(), rng);
        int zombie_placed   = 0;
        int skeleton_placed = 0;
        for (int i = 0; i < (int)zombie_skeleton_zone.size()
                     && (zombie_placed + skeleton_placed) < need_total; i++) {
            if (zombie_placed < COUNT_ZOMBIE) {
                spawnData[zombie_skeleton_zone[i]] = NPC_ZOMBIE;
                zombie_placed++;
            } else if (skeleton_placed < COUNT_SKELETON) {
                spawnData[zombie_skeleton_zone[i]] = NPC_SKELETON;
                skeleton_placed++;
            }
        }
        std::cout << "  Placed " << zombie_placed   << " Zombie" << std::endl;
        std::cout << "  Placed " << skeleton_placed << " Skeleton" << std::endl;
    }

    // 크리퍼
    place_npcs(creeper_zone,  NPC_CREEPER,  COUNT_CREEPER,  "Creeper");

    // 엔더맨
    place_npcs(enderman_zone, NPC_ENDERMAN, COUNT_ENDERMAN, "Enderman");

    // -- 6. 배치 검증 -----------------------------------------------------
    int total_placed = 0;
    for (auto v : spawnData) if (v != NPC_NONE) total_placed++;
    std::cout << "Total NPCs placed: " << total_placed
              << " / " << COUNT_TOTAL
              << (total_placed == COUNT_TOTAL ? " [OK]" : " [MISMATCH!]")
              << std::endl;

    // -- 7. 바이너리 파일 출력 --------------------------------------------
    std::cout << "Writing binary files..." << std::endl;
    {
        std::ofstream f("map_visual.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(visualData.data()), visualData.size());
    }
    {
        std::ofstream f("map_collision.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(collisionData.data()), collisionData.size());
    }
    {
        std::ofstream f("map_spawn.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(spawnData.data()), spawnData.size());
    }

    std::cout << "=== Done! ===" << std::endl;
    std::cout << "  map_visual.bin    (~" << TOTAL_CELLS / 1024 / 1024 << " MB)" << std::endl;
    std::cout << "  map_collision.bin (~" << TOTAL_CELLS / 1024 / 1024 << " MB)" << std::endl;
    std::cout << "  map_spawn.bin     (~" << TOTAL_CELLS / 1024 / 1024 << " MB)" << std::endl;

    return 0;
}