/**
 * @file common.hpp
 * @author Yufei Li, Jingxuan Liu
 * @brief Models and constants.
 * @date 2023-04-01
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#pragma once

#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <optional>
#include <cassert>

#include "logger.hpp"

/**
 * @brief Max number of rounds.
 */
static constexpr int MAX_ROUND = 512;

/* Map */
/**
 * @brief Length of one edge.
 * @note EDGE must be even.
 */
static constexpr int EDGE = 10;
/**
 * @brief Size of the map.
 * @note Point (x, y) with x < MAP_SIZE and y < MAP_SIZE may not be a valid position on the map.
 */
static constexpr int MAP_SIZE = 2 * EDGE - 1;
/**
 * @brief Tag indicating property of points.
 */
enum PointType {
    Void = -1,   ///< Out of the map
    Path = 0,    ///< Ants can pass through here
    Barrier = 1, ///< No passing or building here
    Player0Highland = 2, ///< Player0 can have buildings here
    Player1Highland = 3, ///< Player1 can have buildings here
};
/**
 * @brief Point types of the map.
 */
static constexpr int MAP_PROPERTY[MAP_SIZE][MAP_SIZE] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 0, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, 0, 0, 1, 0, 1, 0, 0, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, -1, -1, -1, -1},
    {-1, -1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, -1, -1},
    {0, 0, 2, 2, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1, 0, 2, 2, 0, 0},
    {0, 0, 0, 2, 0, 0, 2, 2, 0, 2, 0, 2, 2, 0, 0, 2, 0, 0, 0},
    {0, 2, 2, 0, 2, 0, 0, 2, 0, 2, 0, 2, 0, 0, 2, 0, 2, 2, 0},
    {0, 2, 0, 0, 0, 2, 0, 0, 2, 0, 2, 0, 0, 2, 0, 0, 0, 2, 0},
    {0, 0, 2, 0, 2, 0, 0, 2, 0, 0, 0, 2, 0, 0, 2, 0, 2, 0, 0},
    {0, 1, 3, 0, 3, 1, 0, 1, 0, 1, 0, 1, 0, 1, 3, 0, 3, 1, 0},
    {0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0},
    {0, 3, 3, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 3, 3, 0},
    {0, 3, 0, 0, 0, 0, 3, 3, 0, 3, 0, 3, 3, 0, 0, 0, 0, 3, 0},
    {0, 0, 3, 3, 0, 0, 0, 3, 0, 3, 0, 3, 0, 0, 0, 3, 3, 0, 0},
    {-1, 0, 0, 3, 0, 1, 1, 0, 0, 3, 0, 0, 1, 1, 0, 3, 0, 0, -1},
    {-1, -1, -1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, -1, -1, -1},
    {-1, -1, -1, -1, -1, 0, 0, 1, 1, 0, 1, 1, 0, 0, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
};
/**
 * @brief The offsets between the coordinates of the current point and its surrounding 6 points.
 *
@verbatim 
When y is even:
                         {x - 1, y}
     {x, y - 1}                          {x, y + 1}
                         {x, y} 
     {x + 1, y - 1}                      {x + 1, y + 1}
                         {x + 1, y}

When y is odd:           
                         {x - 1, y}
     {x - 1, y - 1}                          {x - 1, y + 1}
                         {x, y} 
     {x, y - 1}                              {x, y + 1}
                         {x + 1, y}
@endverbatim
 */
static constexpr int OFFSET[2][6][2] = {{{0, 1}, {-1, 0}, {0, -1}, {1, -1}, {1, 0}, {1, 1}},
                                {{-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, 0}, {0, 1}}};

static int dist_array[MAP_SIZE][MAP_SIZE][MAP_SIZE][MAP_SIZE];
/**
 * @brief Get the distance between two points on the map (NOT Euclidean distance). 
 * @param x0 The x-coordinate of the first point.
 * @param y0 The y-coordinate of the first point.
 * @param x1 The x-coordinate of the second point.
 * @param y1 The y-coordinate of the second point.
 * @return The distance between the given points.
 */
inline int distance_raw(int x0, int y0, int x1, int y1)
{
    int dy = abs(y0 - y1);
    int dx;
    if (abs(y0 - y1) % 2)
    {
        if (x0 > x1)
            dx = std::max(0, abs(x0 - x1) - abs(y0 - y1) / 2 - (y0 % 2));
        else
            dx = std::max(0, abs(x0 - x1) - abs(y0 - y1) / 2 - (1 - (y0 % 2)));
    }
    else
        dx = std::max(0, abs(x0 - x1) - abs(y0 - y1) / 2);

    return dx + dy;
}
void init_dist_array() {
    for (int x0 = 0; x0 < MAP_SIZE; x0++) for (int x1 = 0; x1 < MAP_SIZE; x1++)
        for (int y0 = 0; y0 < MAP_SIZE; y0++) for (int y1 = 0; y1 < MAP_SIZE; y1++) dist_array[x0][y0][x1][y1] = distance_raw(x0, y0, x1, y1);
}

inline int distance(int x0, int y0, int x1, int y1)
{
    return dist_array[x0][y0][x1][y1];
}

/**
 * @brief Check if the given coordinates refers to a valid point on the map.
 * @param x The x-coordinate of the point.
 * @param y The y-coordinate of the point.
 * @return Whether the given coordinates refers to a valid point on the map.
 */
inline bool is_valid_pos(int x, int y)
{
    if (x < 0 || x >= MAP_SIZE || y < 0 || y >= MAP_SIZE)
        return false;
    return MAP_PROPERTY[x][y] != PointType::Void;
}

/**
 * @brief Check if the given position is reachable for ants.
 * @param x The x-coordinate of the point.
 * @param y The y-coordinate of the point.
 * @return Whether it is reachable.
 */
inline bool is_path(int x, int y)
{
    if (x < 0 || x >= MAP_SIZE || y < 0 || y >= MAP_SIZE)
        return false;
    return MAP_PROPERTY[x][y] == PointType::Path;
}

/**
 * @brief Check if a player can build towers at given position.
 * @param x The x-coordinate of the point.
 * @param y The y-coordinate of the point.
 * @return Whether building is allowed.
 */
inline bool is_highland(int player, int x, int y)
{
    if (x < 0 || x >= MAP_SIZE || y < 0 || y >= MAP_SIZE)
        return false;
    return MAP_PROPERTY[x][y] == (player == 0 ? PointType::Player0Highland : PointType::Player1Highland);
}

/**
 * @brief Get the direction of two adjacent points, starting from the first and pointing to the second.
 * @param x0 The x-coordinate of the first point.
 * @param y0 The y-coordinate of the first point.
 * @param x1 The x-coordinate of the second point.
 * @param y1 The y-coordinate of the second point.
 * @return The index of the direction if given adjacent points, or -1 in other cases.
 */
inline int get_direction(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    for (int i = 0; i < 6; ++i)
    {
        if (OFFSET[y0 % 2][i][0] == dx && OFFSET[y0 % 2][i][1] == dy)
            return i;
    }
    return -1;
}


struct Pos {
    int x;
    int y;

    bool operator==(const Pos& other) const {
        return x == other.x && y == other.y;
    }
    bool operator!=(const Pos& other) const {
        return x != other.x || y != other.y;
    }

    int dist_to(int x1, int x2) const {
        return distance(x, y, x1, x2);
    }
    int dist_to(const Pos& other) const {
        return distance(x, y, other.x, other.y);
    }
};
const std::vector<Pos> highlands[2] = {
    {
        {6, 1}, {7, 1}, {4, 2}, {6, 2}, {8, 2}, {4, 3}, {5, 3}, {6, 4},
        {8, 4}, {7, 5}, {5, 6}, {5, 7}, {6, 7}, {8, 7}, {7, 8}, {4, 9},
        {5, 9}, {6, 9}, {7, 10}, {5, 11}, {6, 11}, {8, 11}, {5, 12}, {7, 13},
        {6, 14}, {8, 14}, {4, 15}, {5, 15}, {4, 16}, {6, 16}, {8, 16}, {6, 17}, {7, 17}
    }, {
        {11, 1}, {12, 1}, {9, 2}, {11, 2}, {13, 2}, {13, 3}, {14, 3}, {9, 4},
        {11, 4}, {11, 5}, {12, 6}, {10, 7}, {12, 7}, {13, 7}, {10, 8}, {12, 9},
        {13, 9}, {14, 9}, {10, 10}, {10, 11}, {12, 11}, {13, 11}, {12, 12}, {11, 13},
        {9, 14}, {11, 14}, {13, 15}, {14, 15}, {9, 16}, {11, 16}, {13, 16}, {11, 17}, {12, 17}
    }
};


/* Coin */
static constexpr int COIN_INIT = 50,
                     BASIC_INCOME = 1;
static constexpr int TOWER_BUILD_PRICE_BASE = 15,
                     TOWER_BUILD_PRICE_RATIO = 2;
static constexpr int LEVEL2_TOWER_UPGRADE_PRICE = 60,
                     LEVEL3_TOWER_UPGRADE_PRICE = 200;
static constexpr double TOWER_DOWNGRADE_REFUND_RATIO = 0.8;
static constexpr int LEVEL2_BASE_UPGRADE_PRICE = 200,
                     LEVEL3_BASE_UPGRADE_PRICE = 250;
static constexpr int BUILD_COST[8] = {0, 15, 30, 60, 120, 240, 480, 960}; // “修建”第i个塔需要的钱数
static constexpr int ACCU_COST[8] = {0, 15, 45, 105, 225, 465, 945, 1905}; // “修完”第i个塔需要的钱数
static constexpr int TOWER_REFUND[8] = {0, 12, 24, 48, 96, 192, 384, 768}; // 拆除第i个塔获得的返还 / 第i个塔所拥有的“数量价值”
static constexpr int ACCU_REFUND[8] = {0, 12, 36, 84, 180, 372, 756, 1524}; // 拥有n个塔的累计固定资产
static constexpr int MOVE_COST[8] = {0, 3, 6, 12, 24, 48, 96, 192}; // “搬迁”第i个塔需要的钱数（不包括等级开销）

static constexpr int UPGRADE_COST[3] = {0, LEVEL2_TOWER_UPGRADE_PRICE, LEVEL3_TOWER_UPGRADE_PRICE}; // 升级i级塔的开销
static constexpr int LEVEL_REFUND[4] = {0, 0, UPGRADE_COST[1]*4/5, (UPGRADE_COST[1]+UPGRADE_COST[2])*4/5}; // i级塔的“等级价值”
static constexpr int DOWNGRADE_REFUND[4] = {0, 0, UPGRADE_COST[1]*4/5, UPGRADE_COST[2]*4/5}; // 降级i级塔获得的钱

/* Pheromone */
static constexpr double PHEROMONE_INIT = 10,
                        PHEROMONE_MIN = 0,
                        PHEROMONE_ATTENUATING_RATIO = 0.97;


/* Entity */

/**
 * @brief State of an ant, indicating its life cycle stages.
 * @note Terminology: An ant is called "alive" iff it has positive health points (HP) and hasn't reached
 * the opponent's base. Therefore "alive" ants have state AntState::Alive or AntState::Frozen, while
 * "dead" ants have other states. It's irreversible to change from "alive" states to "dead" states.
 */
enum AntState
{
    Alive   = 0, ///< Normal case
    Success = 1, ///< Reach the opponent's camp
    Fail    = 2, ///< Non-positive health points (HP)
    TooOld  = 3, ///< Reach age limit
    Frozen  = 4  ///< Frozen, cannot move
};

/**
 * @brief Basic attacking unit.
 */
struct Ant
{
    // Attributes
    int id, player;
    int x, y;
    int hp, level, age;
    AntState state;
    std::vector<int> path;
    int evasion; // tag for emergency evasion
    bool deflector;  // tag for deflector
    // Static info
    static constexpr int AGE_LIMIT = 32;
    static constexpr int MAX_HP_INFO[] = {10, 25, 50}; // Max HP of an ant of certain level
    static constexpr int REWARD_INFO[] = {3, 5, 7};    // Reward for killing an ant of certain level

    /**
     * @brief Construct a new ant with given information.
     */
    Ant(int id, int player, int x, int y, int hp, int level, int age, AntState state)
        : id(id), player(player), x(x), y(y), hp(hp), level(level), age(age), state(state), evasion(0), deflector(false) {}

    std::string str(bool bracket = false) const {
        std::string ans;
        if (bracket) ans += '[';
        ans += str_wrap("id%d p%d (%2d,%2d) hp%d", id, player, x, y, hp);
        if (evasion) ans += " E";
        if (evasion == 2) ans += 'E';
        if (bracket) ans += ']';
        return ans;
    }

    /**
     * @brief Move the ant in a specified direction.
     * @param direction Index of the direction.
     */
    void move(int direction)
    {
        path.push_back(direction);
        x += OFFSET[y % 2][direction][0];
        y += OFFSET[y % 2][direction][1];
    }

    /**
     * @brief HP limit of this ant.
     */
    int max_hp() const
    {
        return MAX_HP_INFO[level];
    }

    /**
     * @brief Reward for killing this ant.
     */
    int reward() const
    {
        return REWARD_INFO[level];
    }

    /**
     * @brief Check if the ant is alive, including states AntState::Alive and AntState::Frozen.
     * @return Whether the ant is alive.
     */
    bool is_alive() const
    {
        return state == AntState::Alive || state == AntState::Frozen;
    }

    /**
     * @brief Check if the ant stays in a circle with given point as the center.
     * @param x The x-coordinate of the center point.
     * @param y The y-coordinate of the center point.
     * @param range The radius of the circle.
     * @return Whether the ant stays in the area.
     */
    bool is_in_range(int x, int y, int range) const
    {
        return distance(this->x, this->y, x, y) <= range;
    }

    /**
     * @brief Check if the ant is attackable by a player from given position and range.
     * @param x The x-coordinate of the center point.
     * @param y The y-coordinate of the center point.
     * @param range The radius of the 
     * @return Whether the ant is attackable.
     */
    bool is_attackable_from(int player, int x, int y, int range) const
    {
        return this->player != player && is_alive() && is_in_range(x, y, range);
    }
};

/**
 * @brief Tag for the type of a tower. The integer values of these enumeration items
 * are also their indexes.
 */
enum TowerType {
    // Basic
    Basic = 0,
    // Heavy class
    Heavy     = 1,
    HeavyPlus = 11,
    Ice       = 12,
    Cannon    = 13,
    // Quick class
    Quick     = 2,
    QuickPlus = 21,
    Double    = 22,
    Sniper    = 23,
    // Mortar class
    Mortar     = 3,
    MortarPlus = 31,
    Pulse      = 32,
    Missile    = 33
};
inline const char* tower_type_name(TowerType tp) {
    switch (tp) {
        case TowerType::Basic:
            return "Basic";
        case TowerType::Heavy:
            return "Heavy";
        case TowerType::HeavyPlus:
            return "Heavy+";
        case TowerType::Ice:
            return "Ice";
        case TowerType::Cannon:
            return "Cannon";
        case TowerType::Quick:
            return "Quick";
        case TowerType::QuickPlus:
            return "Quick+";
        case TowerType::Double:
            return "Double";
        case TowerType::Sniper:
            return "Sniper";
        case TowerType::Mortar:
            return "Mortar";
        case TowerType::MortarPlus:
            return "Mortar+";
        case TowerType::Pulse:
            return "Pulse";
        case TowerType::Missile:
            return "Missile";
        default:
            return "?";
    }
}
/**
 * @brief Structure of static information of a type of towers.
 */
struct TowerInfo
{
    int attack;   
    double speed; ///< Number of rounds required for an attack
    int range;    ///< Radius of searching range
};
/**
 * @brief Static information of all types of tower.
 */
constexpr TowerInfo TOWER_INFO[] = {
    {5, 2, 2},  // ID = 0
    {20, 2, 2}, // ID = 1
    {6, 1, 3},  // ID = 2
    {16, 4, 3}, // ID = 3
    {}, {}, {}, {}, {}, {}, {}, // Padding
    {35, 2, 3}, // ID = 11
    {15, 2, 2}, // ID = 12
    {50, 3, 3}, // ID = 13
    {}, {}, {}, {}, {}, {}, {}, // Padding
    {8, 0.5, 3}, // ID = 21
    {7, 1, 4},  // ID = 22
    {15, 2, 6},   // ID = 23
    {}, {}, {}, {}, {}, {}, {}, // Padding
    {35, 4, 4}, // ID = 31
    {30, 3, 2}, // ID = 32 
    {45, 6, 5}  // ID = 33
};
struct Tower
{
    int id, player;
    int x, y;
    TowerType type;
    int damage, range;
    int cd;       ///< Time remaining until next attack (Possibly negative)
    double speed; ///< Number of rounds required for an attack

    /**
     * @brief Construct a new tower with given information.
     * @param type (Optional) Type of the tower, with TowerType::Basic as default.
     * @param cd (Optional) CD time of the tower, with 0 (already cooled down) as default.
     */
    Tower(int id, int player, int x, int y, TowerType type = TowerType::Basic, int cd = 0)
        : id(id), player(player), x(x), y(y), type(type), cd(cd)
    {
        upgrade(type);
    }

    /**
     * @brief 计算给定塔的等级
     * @return int 塔的等级(1~3)
     */
    int level() const {
        if (type == TowerType::Basic) return 1;
        if (type <= 10) return 2;
        return 3;
    }

    std::string str(bool bracket = false) const {
        std::string ans;
        if (bracket) ans += '[';
        ans += str_wrap("p%d id%d %s (%2d,%2d) cd%d", player, id, tower_type_name(type), x, y, cd);
        if (bracket) ans += ']';
        return ans;
    }

    /**
     * @brief Try to attack ants around, and update CD time.
     * @param ants Reference to all ants on the map, holding in a vector.
     * @return The indexes of attacked ants without repeat (i.e. an ant that is attacked multiple
     * times only appears once when returned).
     * @see Tower::find_targets for target searching process.
     */
    std::vector<int> attack(std::vector<Ant>& ants, bool verbose = false)
    {
        std::vector<int> attacked_idxs;
        // Count down CD
        cd = std::max(cd - 1, 0);
        if (verbose) fprintf(stderr, "Tower %2d: cd%d", id, cd);
        if (cd <= 0) // Ready to attack
        {
            // How many times the tower will try to find targets in this turn
            int time = speed >= 1 ? 1 : (1 / speed);
            // How many targets the tower should find each time (maybe less than required number)
            int target_num = type == Double ? 2 : 1;
            // Find and action
            if (verbose) fprintf(stderr, " time%d", time);
            while (time--)
            {
                std::vector<int> target_idxs = find_targets(ants, target_num);
                std::vector<int> attackable_idxs = find_attackable(ants, target_idxs);
                if (verbose && target_idxs.size()) fprintf(stderr, " targ%d", ants[target_idxs[0]].id);
                if (verbose && attackable_idxs.size()) fprintf(stderr, " atk%d", ants[attackable_idxs[0]].id);
                for (int idx: attackable_idxs)
                    action(ants[idx]);
                attacked_idxs.insert(attacked_idxs.end(), attackable_idxs.begin(), attackable_idxs.end());
            }
            // Uniquify to prevent multiple occurances of the same ant
            std::sort(attacked_idxs.begin(), attacked_idxs.end());
            attacked_idxs.erase(std::unique(attacked_idxs.begin(), attacked_idxs.end()), attacked_idxs.end());
            // Reset CD if really attacks
            if (!attacked_idxs.empty())
                reset_cd();
        }
        if (verbose) fprintf(stderr, "\n");
        return attacked_idxs;
    }

    /**
     * @brief Find certain amount of targets and return its reference by index in order.
     * @param ants Reference to all ants on the map, holding in a vector.
     * @param target_num How many targets to find.
     * @return The indexes of targets.
     * @note Terminology: "targets" refers to all the ants discovered by the tower when searching enemy,
     * which is only a SUBSET of all the ants affected by this tower. For example, towers with range attack
     * ability will find some targets and fire directly at them, which may cause damage to ants around the targets.
     */
    std::vector<int> find_targets(const std::vector<Ant>& ants, int target_num) const
    {
        // Initialize index array for reference
        std::vector<int> idxs = get_attackable_ants(ants, x, y, range);
        // Partial sort to get first n elements
        auto bound = target_num <= idxs.size() ? (idxs.begin() + target_num) : idxs.end();
        std::partial_sort(idxs.begin(), bound, idxs.end(), [&] (int i, int j) {
            int dist1 = distance(ants[i].x, ants[i].y, x, y),
                dist2 = distance(ants[j].x, ants[j].y, x, y);
            if (dist1 != dist2)
                return dist1 < dist2;
            else
                return i < j;
        });
        // Get first n elements
        if (idxs.size() > target_num)
            idxs.resize(target_num);
        return idxs;
    }

    /**
     * @brief Find all ants affected by this attack based on given targets.
     * @param ants Reference to all ants on the map, holding in a vector.
     * @param target_idxs Indexes of all targets.
     * @return Indexes of all ants involved, with possible duplication (i.e. an ant that is attacked multiple times
     * appears a corresponding number of times when returned).
     * @see Tower::find_targets for more information on the term "targets".
     */
    std::vector<int> find_attackable(const std::vector<Ant>& ants, const std::vector<int>& target_idxs) const
    {
        std::vector<int> attackable_idxs;
        for (int idx: target_idxs)
        {
            std::vector<int> tmp;
            switch (type)
            {
                case Mortar:
                    tmp = get_attackable_ants(ants, ants[idx].x, ants[idx].y, 1);
                    break;
                case MortarPlus:
                    tmp = get_attackable_ants(ants, ants[idx].x, ants[idx].y, 1);
                    break;
                case Pulse:
                    tmp = get_attackable_ants(ants, x, y, range);
                    break;
                case Missile:
                    tmp = get_attackable_ants(ants, ants[idx].x, ants[idx].y, 2);
                    break;
                default:
                    tmp = {idx};
            }
            attackable_idxs.insert(attackable_idxs.end(), tmp.begin(), tmp.end());
        }
        return attackable_idxs;
    }

    /**
     * @brief Cause real damage and other effects on the target.
     * @param ant Reference to the attacked ant.
     */
    void action(Ant& ant) const
    {
        if (ant.evasion > 0)  // evasion effect
            ant.evasion--;  // count down times
        else if (ant.deflector && damage < ant.max_hp() / 2) // deflector effect
            return; // get no damage
        else // normal condition
        {
            ant.hp -= damage;
            if (type == Ice)
                ant.state = AntState::Frozen;
            if (ant.hp <= 0)
                ant.state = AntState::Fail;
        }
    }

    /**
     * @brief Find all attackable ants based on given position and range.
     * @param ants Reference to all ants on the map, holding in a vector.
     * @param x The x-coordinate of the position.
     * @param y The y-coordinate of the position.
     * @param range Radius of the area to search.
     * @return Indexes of all ants involved without repeat.
     */
    std::vector<int> get_attackable_ants(const std::vector<Ant>& ants, int x, int y, int range) const
    {
        std::vector<int> idxs;
        for (int i = 0; i < ants.size(); ++i)
            if (ants[i].is_attackable_from(player, x, y, range))
                idxs.push_back(i);
        return idxs;
    }

    /**
     * @brief Check if the tower is ready to attack.
     * @return Whether the tower is ready.
     */
    bool is_ready() const
    {
        return cd <= 0;
    }

    /**
     * @brief Reset CD value.
    */
    void reset_cd()
    {
        cd = speed > 1 ? speed : 1;
    }

    /**
     * @brief Upgrade tower to new type and reset CD, without checking validness.
     * @param new_type Type to be upgraded into.
     */
    void upgrade(TowerType new_type)
    {
        type = new_type;
        damage = TOWER_INFO[new_type].attack;
        speed = TOWER_INFO[new_type].speed;
        range = TOWER_INFO[new_type].range;
        reset_cd(); // Reset when `speed` has changed
    }

    /**
     * @brief Check if the tower can be upgraded to a certain type.
     * @param type The target type of upgrading.
     * @return Whether the tower can be upgraded to target type. 
     */
    bool is_upgrade_type_valid(int type) const
    {
        if (type < TowerType::Basic || type > TowerType::Missile)
            return false;
        switch (this->type)
        {
            case TowerType::Basic:
                return type == TowerType::Heavy || type == TowerType::Quick || type == TowerType::Mortar;
            case TowerType::Heavy:
                return type == TowerType::HeavyPlus || type == TowerType::Ice || type == TowerType::Cannon;
            case TowerType::Quick:
                return type == TowerType::QuickPlus || type == TowerType::Double || type == TowerType::Sniper;
            case TowerType::Mortar:
                return type == TowerType::MortarPlus || type == TowerType::Pulse || type == TowerType::Missile;
        }
        return false;
    }

    /**
     * @brief Downgrade tower to new type and reset CD, without checking validness.
     */
    void downgrade()
    {
        type = static_cast<TowerType>(type / 10);
        damage = TOWER_INFO[type].attack;
        speed = TOWER_INFO[type].speed;
        range = TOWER_INFO[type].range;
        reset_cd(); // Reset when `speed` has changed
    }

    /**
     * @brief Check if the tower can be downgraded.
     * @return Whether the tower can be downgraded.
     */
    bool is_downgrade_valid() const
    {
        return type != TowerType::Basic;
    }
};

/**
 * @brief Target to protect or to destroy.
 */
struct Base
{
    // Attributes
    const int player, x, y;
    int hp;
    int gen_speed_level; ///< Level of production speed
    int ant_level;       ///< Level of produced ants
    // Static info
    static constexpr int MAX_HP = 50;
    static constexpr int POSITION[2][2] = {{2, EDGE - 1},  {(MAP_SIZE - 1) - 2, EDGE - 1}};  ///< Positions for both players
    static constexpr int GENERATION_CYCLE_INFO[] = {4, 2, 1}; ///< Ants will be generated when round index can be divided by this value
    
    Base(int player)
        : player(player), x(POSITION[player][0]), y(POSITION[player][1]), hp(MAX_HP),
          gen_speed_level(0), ant_level(0) {}

    /**
     * @brief Try to generate a new ant.
     * @param id ID for the ant to be generated.
     * @param round Birth round for the ant to be generated.
     * @return The ant if successfully generated, or nothing.
     */
    std::optional<Ant> generate_ant(int id, int round)
    {
        return round % GENERATION_CYCLE_INFO[gen_speed_level] == 0
            ? std::make_optional(Ant(
                id, player, x, y,
                Ant::MAX_HP_INFO[ant_level], ant_level,
                0, AntState::Alive
            ))
            : std::nullopt;
    }

    /**
     * @brief Upgrade ant generation speed.
     */
    void upgrade_generation_speed()
    {
        gen_speed_level++;
    }

    /**
     * @brief Upgrade ant generation speed.
     */
    void upgrade_generated_ant()
    {
        ant_level++;
    }
};


/**
 * @brief Tag for the type of a super weapon. The integer values of these enumeration items
 * are also their indexes.
 */
enum SuperWeaponType
{
    LightningStorm = 1,
    EmpBlaster = 2,
    Deflector = 3,
    EmergencyEvasion = 4,
    SuperWeaponCount
};
/**
 * @brief Static information of all types of super weapons.
 * @note [type]{duration, range, cd, price}
 */
static constexpr int SUPER_WEAPON_INFO[5][4] = {
    {}, // Padding
    {20, 3, 100, 150}, // LightningStorm
    {20, 3, 100, 150}, // EmpBlaster
    {10, 3, 50, 100}, // Deflector
    {1, 3, 50, 100}   // Evasion
};
static constexpr int EVA_RANGE = SUPER_WEAPON_INFO[SuperWeaponType::EmergencyEvasion][1];
static constexpr int EMP_RANGE = SUPER_WEAPON_INFO[SuperWeaponType::EmpBlaster][1];
/**
 * @brief Great choice to knockout your opponent.
 */
struct SuperWeapon
{
    SuperWeaponType type;
    int player;
    int x, y;
    int left_time;
    int range;

    SuperWeapon(SuperWeaponType type, int player, int x, int y) : 
        type(type), player(player), x(x), y(y), left_time(SUPER_WEAPON_INFO[type][0]), range(SUPER_WEAPON_INFO[type][1]) {}

    /**
     * @brief Check whether given position is in the range of effect.
     * @param x The x-coordinate of the position.
     * @param y The y-coordinate of the position.
     * @return In the range or not. 
     */
    bool is_in_range(int x, int y) const
    {
        return distance(x, y, this->x, this->y) <= range;
    }
};

/* Operation */

/**
 * @brief Tag for the type of an operation. The integer values of these enumeration items
 * are also their indexes.
 */
enum OperationType
{
    // Towers
    BuildTower = 11,     ///< Build a tower
    UpgradeTower = 12,   ///< Upgrade a tower
    DowngradeTower = 13, ///< Downgrade/Destroy a tower
    // Super weapons
    UseLightningStorm = 21,   ///< Use a lightning storm
    UseEmpBlaster = 22,       ///< Use an EMP blaster
    UseDeflector = 23,        ///< Use a deflector
    UseEmergencyEvasion = 24, ///< Use an emergency evasion
    // Base
    UpgradeGenerationSpeed = 31, ///< Increase ant producing speed
    UpgradeGeneratedAnt = 32     ///< Increase HP of newly generated ants
};

/**
 * @brief Player's operations. It is able to be applied to the map.
 */
struct Operation
{
    OperationType type;
    int arg0, arg1;
    static constexpr int INVALID_ARG = -1; ///< Placeholder for the second argument

    /**
     * @brief Construct a new Operation object with specified type and arguments.
     * @param type Type of operation.
     * @param arg0 (optional) The first argument.
     * @param arg1 (optional) The second argument.
     */
    constexpr Operation(OperationType type, int arg0 = INVALID_ARG, int arg1 = INVALID_ARG)
        : type(type), arg0(arg0), arg1(arg1) {}

    std::string wrap_pos() const {
        return str_wrap("(%2d,%2d)", arg0, arg1);
    }
    std::string str(bool bracket = false) const {
        std::string ans;
        if (bracket) ans += '[';
        switch (type) {
        case BuildTower:
            ans += "b" + wrap_pos();
            break;
        case UpgradeTower:
            ans += str_wrap("%d^=%s", arg0, tower_type_name((TowerType)arg1));
            break;
        case DowngradeTower:
            ans += str_wrap("!%d", arg0);
            break;
        case UseLightningStorm:
            ans += "LS" + wrap_pos();
            break;
        case UseEmpBlaster:
            ans += "EMP" + wrap_pos();
            break;
        case UseDeflector:
            ans += "DFL" + wrap_pos();
            break;
        case UseEmergencyEvasion:
            ans += "EVA" + wrap_pos();
            break;
        case UpgradeGenerationSpeed:
            ans += "Base:Speed";
            break;
        case UpgradeGeneratedAnt:
            ans += "Base:Shield";
            break;
        default:
            assert(false);
        }
        if (bracket) ans += ']';
        return ans;
    }

    friend std::ostream& operator<<(std::ostream& out, const Operation& op) 
    {
        out << op.type;
        if (op.arg0 != INVALID_ARG)
            out << ' ' << op.arg0;
        if (op.arg1 != INVALID_ARG)
            out << ' ' << op.arg1;
        out << std::endl;
        return out;
    }
};
Operation build_op(const Pos& pos) {
    return Operation(BuildTower, pos.x, pos.y);
}
Operation upgrade_op(const Tower& t, TowerType tp) {
    return Operation(UpgradeTower, t.id, tp);
}
Operation upgrade_op(int tower_id, TowerType tp) {
    return Operation(UpgradeTower, tower_id, tp);
}
Operation lightning_op(const Pos& pos) {
    return Operation(UseLightningStorm, pos.x, pos.y);
}
Operation EMP_op(const Pos& pos) {
    return Operation(UseEmpBlaster, pos.x, pos.y);
}
Operation DFL_op(const Pos& pos) {
    return Operation(UseDeflector, pos.x, pos.y);
}
Operation EVA_op(const Pos& pos) {
    return Operation(UseEmergencyEvasion, pos.x, pos.y);
}

// 计划任务类
struct Task {
    Operation op; // 该任务的动作
    int round; // 该任务的预定时间（可以是相对时间也可以是绝对时间）

    constexpr explicit Task(const Operation& _op, int _round = 0) : round(_round), op(_op) {}

    // 比较此任务是否应该先于other执行（仅用于优先级队列）
    bool operator<(const Task& other) const {
        return round > other.round;
    }
    // 给定当前回合数，判定此任务是否应当在此时执行
    bool operator<=(int _round) const {
        return round <= _round;
    }
};

/**
 * @brief Random noise generator.
 */
struct Random
{
    unsigned long long seed; ///< Seed for pheromone random initialization.

    Random(unsigned long long seed): seed(seed) {}

    unsigned long long get()
    {
        seed = (25214903917 * seed) & ((1ll << 48) - 1);
        return seed;
    }
};