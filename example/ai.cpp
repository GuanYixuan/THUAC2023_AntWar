#include "../include/template.hpp"

#include "../include/logger.hpp"

#include <queue>
#include <string>
#include <cassert>

constexpr bool RELEASE = true;
constexpr bool LOG_SWITCH = false;
constexpr bool LOG_STDOUT = false;
constexpr int LOG_LEVEL = 0;

struct Pos {
    int x;
    int y;
};
constexpr int highland_count = 33;
constexpr Pos highlands[2][highland_count] = {
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

// 计划任务类别
enum class Task_type {
    tower
};
// 计划任务类
struct Scheduled_task {
    int round;
    Task_type type;
    Operation op;

    bool operator<(const Scheduled_task& other) const {
        return round > other.round;
    }
    bool operator<=(int _round) const {
        return round <= _round;
    }
};

// 针对敌方的模拟结果
struct Enemy_sim_result {
    std::vector<Operation> op_list;
    int succ_ant;
    int old_ant;
    int cost;
    int first_succ;

    Enemy_sim_result(int _succ, int _old, int _cost, int _first, const std::vector<Operation>& _op = {})
    : succ_ant(_succ), old_ant(_old), cost(_cost), first_succ(_first), op_list(_op) {}

    std::string str() const {
        return str_wrap("[succ: %2d, first: %2d, cost: %d, op: %d]", succ_ant, first_succ, cost, op_list.size() ? op_list.front().type : -1);
    }

    // better than
    // 【此处写死了3】
    bool operator>(const Enemy_sim_result& other) const {
        if (succ_ant * other.succ_ant == 0 && succ_ant != other.succ_ant) return succ_ant == 0;
        if (first_succ != other.first_succ) return first_succ > other.first_succ;
        return cost + old_ant * 3 < other.cost + other.old_ant * 3;
    }
};
/**
 * @brief 用于模拟ant行动的模拟器，模拟期间不会考虑两个玩家的任何操作
 * 注：仅限一次性使用
 */
class Ant_simulator {
    static constexpr int INIT_HEALTH = 49;

    const int player_id;

    public:
        Ant_simulator(int _player_id, const GameInfo& _info) : player_id(_player_id), info(_info) {
            for (int i = 0; i < 2; i++) info.bases[i].hp = INIT_HEALTH;
        }
        void apply_op(int _player_id, const Operation& op) {
            imm_ops[_player_id].push_back(op);
        }
        Enemy_sim_result simulate(int round, int _cost = 0) {
            Simulator s(info);
            int _r = 0;
            for (; _r < round; ++_r) {
                if (player_id == 0) {
                    // Add player0's operation
                    if (_r == 0) for (auto &op : imm_ops[0]) s.add_operation_of_player(0, op);
                    // Apply player0's operation
                    s.apply_operations_of_player(0);
                    // Add player1's operation
                    if (_r == 0) for (auto &op : imm_ops[1]) s.add_operation_of_player(1, op);
                    // Apply player1's operation
                    s.apply_operations_of_player(1);
                    // Next round
                    if (s.next_round() != GameState::Running) break;
                } else {
                    // Add player1's operation
                    if (_r == 0) for (auto &op : imm_ops[1]) s.add_operation_of_player(1, op);
                    // Apply player1's operation
                    s.apply_operations_of_player(1);
                    // Next round
                    if (s.next_round() != GameState::Running) break;
                    // Add player0's operation
                    if (_r == 0) for (auto &op : imm_ops[0]) s.add_operation_of_player(0, op);
                    // Apply player0's operation
                    s.apply_operations_of_player(0);
                }
                if (first_succ > 512 && INIT_HEALTH != s.get_info().bases[player_id].hp) first_succ = _r;
            }
            // assert(s.get_info().round >= 512 || _r == round);
            for (int i = 0; i < 2; i++) base_damage[i] = INIT_HEALTH - s.get_info().bases[i].hp;

            return Enemy_sim_result(base_damage[player_id], 0, _cost, first_succ, imm_ops[player_id]);
        }
    
    GameInfo info;
    std::vector<Operation> imm_ops[2];

    // 模拟结果：基地受到的伤害[player_id]
    int base_damage[2];

    // 第一个突破防线的蚂蚁将出现在何时？
    int first_succ = 513;
};

class AI_ {
    public:
        AI_()
        : logger(RELEASE, LOG_SWITCH, LOG_STDOUT, LOG_LEVEL)
        {

        }
        std::vector<Operation> ai_main(int player_id, const GameInfo &game_info) {
            // 初始化
            ops.clear();
            logger.config(game_info.round);
            avail_money = game_info.coins[player_id];

            // 初始策略
            if (game_info.round == 0) {
                ops.emplace_back(BuildTower, player_id ? 14 : 4, 9);
                return ops;
            }

            // 处理计划任务
            while (schedule_queue.size() && schedule_queue.top() <= game_info.round) {
                Scheduled_task task = schedule_queue.top();
                ops.push_back(task.op);
                avail_money += game_info.get_operation_income(player_id, task.op);
                assert(avail_money >= 0);
            }

            // 塔操作搜索
            if (game_info.round >= 16) {
                Ant_simulator sim0(player_id, game_info);
                Enemy_sim_result best_result = sim0.simulate(get_sim_round(game_info.round));
                logger.err("raw: " + best_result.str());

                if (best_result.succ_ant > 0 && best_result.first_succ < 20) { // 如果啥事不干基地会扣血
                    // 搜索：建塔
                    int build_cost = game_info.build_tower_cost(game_info.tower_num_of_player(player_id));
                    if (build_cost < 120 && build_cost <= avail_money) { // 暂时写死不允许建4号塔
                        for (int i = 0; i < highland_count; i++) {
                            Operation temp_op(BuildTower, highlands[player_id][i].x, highlands[player_id][i].y);
                            if (!game_info.is_operation_valid(player_id, temp_op)) continue;

                            Ant_simulator sim(player_id, game_info);
                            sim.apply_op(player_id, temp_op);
                            Enemy_sim_result res = sim.simulate(get_sim_round(game_info.round), build_cost);

                            if (res > best_result) best_result = res;
                        }
                    }
                    // 搜索：升级
                    for (const Tower& t : game_info.towers) {
                        if (t.player != player_id || game_info.upgrade_tower_cost(t.type) == LEVEL3_TOWER_UPGRADE_PRICE) continue;

                        int tower_level = (game_info.upgrade_tower_cost(t.type) == -1) ? 1 : 2;
                        int upgrade_cost = (tower_level == 1) ? LEVEL2_TOWER_UPGRADE_PRICE : LEVEL3_TOWER_UPGRADE_PRICE;
                        if (upgrade_cost > avail_money) continue;

                        Operation temp_op(UpgradeTower, t.id, (tower_level == 1) ? TowerType::Quick : TowerType::Double);
                        assert(game_info.is_operation_valid(player_id, temp_op));

                        Ant_simulator sim(player_id, game_info);
                        sim.apply_op(player_id, temp_op);
                        Enemy_sim_result res = sim.simulate(get_sim_round(game_info.round), upgrade_cost * UPGRADE_COST_MULT); // 为升级提供优惠

                        logger.err("upd: " + res.str());

                        if (res > best_result) best_result = res;
                    }
                    // 搜索：搬迁
                    build_cost = game_info.build_tower_cost(game_info.tower_num_of_player(player_id) - 1) / 5; // 真实消耗：0.2倍塔造价
                    if (build_cost <= avail_money) {
                        for (const Tower& t : game_info.towers) {
                            if (t.player != player_id || game_info.upgrade_tower_cost(t.type) != -1) continue; // 被拆塔必然是1级的
                            Operation destroy_op(DowngradeTower, t.id);

                            for (int i = 0; i < highland_count; i++) {
                                Operation build_op(BuildTower, highlands[player_id][i].x, highlands[player_id][i].y);
                                if (!game_info.is_operation_valid(player_id, build_op)) continue;

                                Ant_simulator sim(player_id, game_info);
                                sim.apply_op(player_id, destroy_op);
                                sim.apply_op(player_id, build_op);
                                Enemy_sim_result res = sim.simulate(get_sim_round(game_info.round), build_cost);

                                if (res > best_result) best_result = res;
                            }
                        }
                    }
                }

                if (best_result.op_list.size()) {
                    logger.err("best: " + best_result.str());
                    for (const Operation& op : best_result.op_list) ops.push_back(op);
                    avail_money += game_info.get_operation_income(player_id, ops.back());
                }
            }

            return ops;
        }

        Logger logger;
    
    private:
        // 当前可用钱数（计及即将执行操作的钱）
        int avail_money;

        // 已确定的操作
        std::vector<Operation> ops;

        // 计划任务队列，按时间升序排列
        std::priority_queue<Scheduled_task> schedule_queue;

        // 模拟的回合数
        static int get_sim_round(int curr_round) {
            if (curr_round < 150) return 40 + curr_round / 3;
            return 90;
        }

        static constexpr double UPGRADE_COST_MULT = 0.85;
};



int main() {
    AI_ ai = AI_();

    Controller c;
    while (true) {
        if (c.self_player_id == 0) // Game process when you are player 0
        {
            // AI makes decisions
            std::vector<Operation> ops = ai.ai_main(c.self_player_id, c.get_info());
            // Add operations to controller
            for (auto &op : ops) c.append_self_operation(op);
            // Send operations to judger
            c.send_self_operations();
            // Apply operations to game state
            c.apply_self_operations();
            // Read opponent operations from judger
            c.read_opponent_operations();
            // Apply opponent operations to game state
            c.apply_opponent_operations();
            // Read round info from judger
            c.read_round_info();
        } else // Game process when you are player 1
        {
            // Read opponent operations from judger
            c.read_opponent_operations();
            // Apply opponent operations to game state
            c.apply_opponent_operations();
            // AI makes decisions
            std::vector<Operation> ops = ai.ai_main(c.self_player_id, c.get_info());
            // Add operations to controller
            for (auto &op : ops) c.append_self_operation(op);
            // Send operations to judger
            c.send_self_operations();
            // Apply operations to game state
            c.apply_self_operations();
            // Read round info from judger
            c.read_round_info();
        }
    }

    return 0;
}