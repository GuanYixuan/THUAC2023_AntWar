#include "../include/template.hpp"

// #include "../include/logger.hpp"

#include <queue>
#include <string>
#include <cassert>

constexpr bool RELEASE = true;
constexpr bool LOG_SWITCH = false;
constexpr bool LOG_STDOUT = false;
constexpr int LOG_LEVEL = 0;

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

// 模拟阈值（仅对敌方生效）
// 尚未生效
struct Simulation_thresh {
    int succ_ant;
    int old_ant;

    bool eq_worse_than(int succ, int old) const {
        if (succ_ant != succ) return succ_ant > succ;
        return old_ant >= old;
    }
};
/**
 * @brief 用于模拟ant行动的模拟器，模拟期间不会考虑两个玩家的任何操作
 * 注：仅限一次性使用
 */
class Ant_simulator {
    static constexpr int INIT_HEALTH = 512;

    int player_id;

    public:
        Ant_simulator(int _player_id, const GameInfo& _info) : player_id(_player_id), info(_info) {
            for (int i = 0; i < 2; i++) info.bases[i].hp = INIT_HEALTH;
        }
        void apply_op(int _player_id, const Operation& op) {
            imm_ops[_player_id].push_back(op);
        }
        void simulate(int round) {
            Simulator s(info);
            for (int i = 0; i < round; ++i) {
                if (player_id == 0) {
                    // Add player0's operation
                    if (i == 0) for (auto &op : imm_ops[0]) s.add_operation_of_player(0, op);
                    // Apply player0's operation
                    s.apply_operations_of_player(0);
                    // Add player1's operation
                    if (i == 0) for (auto &op : imm_ops[1]) s.add_operation_of_player(1, op);
                    // Apply player1's operation
                    s.apply_operations_of_player(1);
                    // Next round
                    if (s.next_round() != GameState::Running) break;
                } else {
                    // Add player1's operation
                    if (i == 0) for (auto &op : imm_ops[1]) s.add_operation_of_player(1, op);
                    // Apply player1's operation
                    s.apply_operations_of_player(1);
                    // Next round
                    if (s.next_round() != GameState::Running) break;
                    // Add player0's operation
                    if (i == 0) for (auto &op : imm_ops[0]) s.add_operation_of_player(0, op);
                    // Apply player0's operation
                    s.apply_operations_of_player(0);
                }
            }
            for (int i = 0; i < 2; i++) succ_ants[i] = INIT_HEALTH - s.get_info().bases[i].hp;
        }
    
    GameInfo info;
    std::vector<Operation> imm_ops[2];

    int succ_ants[2];
};

class AI_ {
    public:
        AI_()
        // : logger(RELEASE, LOG_SWITCH, LOG_STDOUT, LOG_LEVEL)
        {

        }
        std::vector<Operation> ai_main(int player_id, const GameInfo &game_info) {
            // 初始化
            ops.clear();
            avail_money = game_info.coins[player_id];

            // 处理计划任务
            while (schedule_queue.size() && schedule_queue.top() <= game_info.round) {
                Scheduled_task task = schedule_queue.top();
                ops.push_back(task.op);
                avail_money += game_info.get_operation_income(player_id, task.op);
                assert(avail_money >= 0);
            }



            return ops;
        }

        // Logger logger;
    
    private:
        // 当前可用钱数（计及即将执行操作的钱）
        int avail_money;

        // 已确定的操作
        std::vector<Operation> ops;

        // 计划任务队列，按时间升序排列
        std::priority_queue<Scheduled_task> schedule_queue;

        static constexpr int SIM_ROUND = 120;
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