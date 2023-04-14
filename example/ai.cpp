#include "../include/template.hpp"

#include "../include/logger.hpp"

#include <queue>
#include <string>
#include <cassert>

constexpr bool DEBUG = true;

constexpr bool RELEASE = true;
constexpr bool LOG_SWITCH = false;
constexpr bool LOG_STDOUT = false;
constexpr int LOG_LEVEL = 0;

int pid;
const GameInfo* info;

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

const std::vector<Pos> feasible_hl[2] = {
    {
        {6, 2}, {8, 2}, {5, 3}, {6, 4},
        {8, 4}, {7, 5}, {5, 6}, {5, 7}, {6, 7}, {8, 7}, {7, 8}, {4, 9},
        {5, 9}, {6, 9}, {7, 10}, {5, 11}, {6, 11}, {8, 11}, {5, 12}, {7, 13},
        {6, 14}, {8, 14}, {5, 15}, {6, 16}, {8, 16}
    }, {
        {9, 2}, {11, 2}, {13, 3}, {9, 4},
        {11, 4}, {11, 5}, {12, 6}, {10, 7}, {12, 7}, {13, 7}, {10, 8}, {12, 9},
        {13, 9}, {14, 9}, {10, 10}, {10, 11}, {12, 11}, {13, 11}, {12, 12}, {11, 13},
        {9, 14}, {11, 14}, {13, 15}, {9, 16}, {11, 16}
    }
};

class Util {
    public:
    static int closest_tower_dis(const Pos& pos, int exclude_id = -1) {
        int ans = 999;
        for (const Tower& t : info->towers) {
            if (t.player != pid || t.id == exclude_id) continue;
            ans = std::min(ans, distance(t.x, t.y, pos.x, pos.y));
        }
        return ans;
    }

    static Operation build_op(const Pos& pos) {
        return Operation(BuildTower, pos.x, pos.y);
    }
    static Operation upgrade_op(const Tower& t, TowerType tp) {
        return Operation(UpgradeTower, t.id, tp);
    }
    static Operation lightning_op(const Pos& pos) {
        return Operation(UseLightningStorm, pos.x, pos.y);
    }
    static Operation emp_op(const Pos& pos) {
        return Operation(UseEmpBlaster, pos.x, pos.y);
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
    int succ_ant;
    int old_ant;
    int first_succ;
    int dmg_dealt;

    constexpr Enemy_sim_result() : succ_ant(99), old_ant(99), first_succ(0), dmg_dealt(0) {};

    constexpr Enemy_sim_result(int _succ, int _old, int _first, int _dmg)
    : succ_ant(_succ), old_ant(_old), first_succ(_first), dmg_dealt(_dmg) {}

};
/**
 * @brief 用于模拟ant行动的模拟器，模拟期间不会考虑两个玩家的任何操作
 * 注：仅限一次性使用
 */
class Ant_simulator {
    static constexpr int INIT_HEALTH = 49;

    public:
    Ant_simulator() : sim_info(*info) {
        for (int i = 0; i < 2; i++) sim_info.bases[i].hp = INIT_HEALTH;
    }
    void apply_op(int _player_id, const Operation& op) {
        imm_ops[_player_id].push_back(op);
    }
    Enemy_sim_result simulate(int round) {
        int base_damage[2]; // 模拟结果：基地受到的伤害[player_id]
        int first_succ = MAX_ROUND + 1; // 第一个突破防线的蚂蚁将出现在何时？
        Simulator s(sim_info);

        int _r = 0;
        for (; _r < round; ++_r) {
            if (pid == 0) {
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
            if (first_succ > MAX_ROUND && INIT_HEALTH != s.get_info().bases[pid].hp) first_succ = _r;
        }
        // assert(s.get_info().round >= 512 || _r == round);
        for (int i = 0; i < 2; i++) base_damage[i] = INIT_HEALTH - s.get_info().bases[i].hp;

        return Enemy_sim_result(base_damage[pid], 0, first_succ, base_damage[!pid]);
    }
    
    GameInfo sim_info;
    std::vector<Operation> imm_ops[2];
};

class Operation_list {
    public:
    std::vector<Operation> ops;
    int cost;

    Enemy_sim_result res;

    explicit Operation_list(const std::vector<Operation>& _ops, int eval_round = -1, int _cost = 0) : ops(_ops), cost(_cost) {
        if (eval_round >= 0) evaluate(eval_round);
    };
    void append(const Operation& op) {
        ops.push_back(op);
    }
    void append(const optional<Operation>& op) {
        if (op) ops.push_back(op.value());
    }

    const Enemy_sim_result& evaluate(int _round) {
        Ant_simulator sim;
        for (const Operation& op : ops) sim.apply_op(pid, op);
        res = sim.simulate(_round);
        return res;
    }

    std::string str() const {
        std::string ret = str_wrap("[s: %2d, f: %3d, c: %3d] [", res.succ_ant, res.first_succ, cost);
        for (const Operation& op : ops) ret += op.str() + ' ';
        if (ops.size()) ret.pop_back();
        return ret + ']';
    }
    // better than
    bool operator>(const Operation_list& other) const {
        if (res.first_succ != other.res.first_succ) return res.first_succ > other.res.first_succ;
        return cost + res.old_ant * 3 + res.succ_ant * 15 < other.cost + other.res.old_ant * 3 + other.res.succ_ant * 15;
    }
};

class AI_ {
    public:
        Logger logger;
        AI_() : logger(RELEASE, LOG_SWITCH, LOG_STDOUT, LOG_LEVEL) {}

        // 游戏过程控制及预处理
        void run_ai() {
            Controller c;
            std::vector<Operation> _opponent_op; // 对手上一次的行动，仅在保证其值正确的时候传递给ai_call_routine
            while (true) {
                if (c.self_player_id == 0) { // Game process when you are player 0
                    // AI makes decisions
                    std::vector<Operation> ops = ai_call_routine(c.self_player_id, c.get_info(), _opponent_op);
                    // Add operations to controller
                    for (auto &op : ops) c.append_self_operation(op);
                    // Send operations to judger
                    c.send_self_operations();
                    // Apply operations to game state
                    c.apply_self_operations();
                    // Read opponent operations from judger
                    c.read_opponent_operations();
                    _opponent_op = c.get_opponent_operations();
                    // Apply opponent operations to game state
                    c.apply_opponent_operations();
                    // Parallel Simulation
                    Simulator fixer(c.get_info());
                    fixer.next_round();
                    // Read round info from judger
                    c.read_round_info();
                    // Overwrite incorrect Tower::cd and Ant::evasion!
                    pre_fix_cd(fixer, c.info);
                    pre_fix_evasion(fixer, c.info);
                } else  { // Game process when you are player 1
                    // Read opponent operations from judger
                    c.read_opponent_operations();
                    _opponent_op = c.get_opponent_operations();
                    // Apply opponent operations to game state
                    c.apply_opponent_operations();
                    // AI makes decisions
                    std::vector<Operation> ops = ai_call_routine(c.self_player_id, c.get_info(), _opponent_op);
                    // Add operations to controller
                    for (auto &op : ops) c.append_self_operation(op);
                    // Send operations to judger
                    c.send_self_operations();
                    // Apply operations to game state
                    c.apply_self_operations();
                    // Parallel Simulation
                    Simulator fixer(c.get_info());
                    fixer.next_round();
                    // Read round info from judger
                    c.read_round_info();
                    // Overwrite incorrect Tower::cd and Ant::evasion!
                    pre_fix_cd(fixer, c.info);
                    pre_fix_evasion(fixer, c.info);
                }
            }
        }
        // 预处理模块：覆盖错误的Tower::cd
        void pre_fix_cd(const Simulator& fixer, GameInfo& incorrect) {
            bool id_same = true;
            const std::vector<Tower>& correct_tower = fixer.get_info().towers;
            std::vector<Tower>& editing_tower = incorrect.towers;
            assert(correct_tower.size() == editing_tower.size());
            for (int i = 0, lim = correct_tower.size(); i < lim; i++) id_same &= (correct_tower[i].id == editing_tower[i].id);
            if (!id_same) {
                std::string msg("pred:{");
                for (int i = 0, lim = correct_tower.size(); i < lim; i++) msg += str_wrap("%2d,", correct_tower[i].id);
                msg += "} real:{";
                for (int i = 0, lim = correct_tower.size(); i < lim; i++) msg += str_wrap("%2d,", editing_tower[i].id);
                logger.err("[w] Inconsistent tower id");
                logger.err(msg + "}");
            }
            for (int i = 0, lim = correct_tower.size(); i < lim; i++) editing_tower[i].cd = correct_tower[i].cd;
        }
        // 预处理模块：覆盖错误的Ant::evasion
        void pre_fix_evasion(const Simulator& fixer, GameInfo& incorrect) {
            bool id_same = true;
            const std::vector<Ant>& correct_ant = fixer.get_info().ants;
            std::vector<Ant>& editing_ant = incorrect.ants;
            assert(correct_ant.size() == editing_ant.size());
            for (int i = 0, lim = correct_ant.size(); i < lim; i++) id_same &= (correct_ant[i].id == editing_ant[i].id);
            if (!id_same) {
                std::string msg("pred:{");
                for (int i = 0, lim = correct_ant.size(); i < lim; i++) msg += str_wrap("%2d,", correct_ant[i].id);
                msg += "} real:{";
                for (int i = 0, lim = correct_ant.size(); i < lim; i++) msg += str_wrap("%2d,", editing_ant[i].id);
                logger.err("[w] Inconsistent ant id");
                logger.err(msg + "}");
                // assert(false); // 严格性有待观察
            }
            for (int i = 0, lim = correct_ant.size(); i < lim; i++) editing_ant[i].evasion = correct_ant[i].evasion;
        }

        // 决策逻辑
        const std::vector<Operation>& ai_call_routine(int player_id, const GameInfo &game_info, const std::vector<Operation>& opponent_op) {
            // 全局变量
            pid = player_id;
            info = &game_info;

            // 初始化
            ops.clear();
            logger.config(game_info.round);
            logger.err("coin: %3d vs %3d", game_info.coins[pid], game_info.coins[!pid]);
            avail_money = game_info.coins[player_id];

            // 模拟检查
            ai_simulation_checker_pre(game_info, opponent_op);

            // 输出对方操作
            std::string enemy_op = "opponent_op:";
            for (const Operation& op : opponent_op) enemy_op += ' ' + op.str(true);
            if (opponent_op.size()) logger.err(enemy_op);

            // 主决策逻辑
            ai_main(game_info, opponent_op); // 暂时维持原本的传参模式

            // 模拟检查
            ai_simulation_checker_pos(game_info);

            return ops;
        }

    private:
        std::string pred;
        // 模拟检查：检查Simulator对一回合后的预测结果是否与实测符合
        void ai_simulation_checker_pre(const GameInfo &game_info, const std::vector<Operation>& opponent_op) {
            std::string cur;
            for (const Ant& a : game_info.ants) cur += a.str(true);
            if (cur != pred && game_info.round > 0) {
                if (opponent_op.size()) logger.err("Predition and truth differ for round %d (opponent act)", game_info.round);
                else logger.err("[w] Predition and truth differ for round %d", game_info.round);
                logger.err("Pred: " + pred);
                logger.err("Truth:" + cur);

                std::string tow = "Towers: ";
                for (const Tower& t : game_info.towers) tow += t.str(true);
                logger.err(tow);
            }
        }
        // 模拟检查：检查Simulator对一回合后的预测结果是否与实测符合
        void ai_simulation_checker_pos(const GameInfo &game_info) {
            Simulator s(game_info);
            // s.verbose = 1;
            if (pid == 0) {
                // Add player0's operation
                for (auto &op : ops) s.add_operation_of_player(0, op);
                // Apply player0's operation
                s.apply_operations_of_player(0);
                // Add player1's operation
                // Apply player1's operation
                s.apply_operations_of_player(1);
                // Next round
                s.next_round();
            } else {
                // Add player1's operation
                for (auto &op : ops) s.add_operation_of_player(1, op);
                // Apply player1's operation
                s.apply_operations_of_player(1);
                // Next round
                s.next_round();
                // Add player0's operation
                // Apply player0's operation
                s.apply_operations_of_player(0);
            }
            pred = "";
            for (const Ant& a : s.get_info().ants) pred += a.str(true);
        }

        // 主决策逻辑
        void ai_main(const GameInfo &game_info, const std::vector<Operation>& opponent_op) {
            // 公共变量
            int sim_round = get_sim_round(game_info.round);
            int tower_num = game_info.tower_num_of_player(pid);

            // 初始策略
            if (game_info.round == 0) {
                ops.push_back(Util::build_op(FIRST_TOWER_POS[pid]));
                return;
            }

            // 处理计划任务
            while (schedule_queue.size() && schedule_queue.top() <= game_info.round) {
                Scheduled_task task = schedule_queue.top();
                schedule_queue.pop();
                ops.push_back(task.op);
                avail_money += game_info.get_operation_income(pid, task.op);
                assert(avail_money >= 0);
            }

            // 塔操作搜索
            if (game_info.round >= 16) {
                Operation_list raw_result({}, sim_round, 0);
                int raw_f_succ = raw_result.res.first_succ;
                Operation_list best_result(raw_result);
                logger.err("raw: " + best_result.str());

                if (best_result.res.first_succ < 20) { // 如果啥事不干基地会扣血
                    // 搜索：建塔
                    int build_cost = game_info.build_tower_cost(tower_num);
                    if ((build_cost < 120 || raw_f_succ <= 10) && build_cost <= avail_money && build_cost < 240) { // 暂时写死不允许建5号塔
                        for (const Pos& pos : feasible_hl[pid]) {
                            const Operation& build_op = Util::build_op(pos);
                            if (Util::closest_tower_dis(pos) < MIN_TOWER_DIST) continue;
                            if (!game_info.is_operation_valid(pid, build_op)) continue;

                            Operation_list opl({build_op}, sim_round, build_cost);
                            logger.err("bud: " + opl.str());

                            if (opl > best_result) best_result = opl;
                        }
                    }
                    // 搜索：（拆除）+升级
                    for (auto it = game_info.towers.begin(); ; it++) {
                        int downgrade_income = 0; // 降级收入
                        optional<Operation> down_op = nullopt;
                        if (it != game_info.towers.end()) { // 如果要降级
                            const Tower& t_down = *it;
                            if (t_down.player != pid || game_info.upgrade_tower_cost(t_down.type) != -1 || game_info.is_shielded_by_emp(t_down)) continue;

                            down_op = Operation(DowngradeTower, t_down.id);
                            if (logger.warn_if(!game_info.is_operation_valid(pid, down_op.value()), "invalid destruct attempt")) continue;

                            downgrade_income = game_info.destroy_tower_income(tower_num);
                        } // 否则不降级

                        for (const Tower& t_up : game_info.towers) {
                            if (t_up.player != pid || (down_op.has_value() && down_op.value().arg0 == t_up.id)) continue;
                            if (game_info.upgrade_tower_cost(t_up.type) == LEVEL3_TOWER_UPGRADE_PRICE || game_info.is_shielded_by_emp(t_up)) continue;

                            int tower_level = (game_info.upgrade_tower_cost(t_up.type) == -1) ? 1 : 2;
                            int total_cost = ((tower_level == 1) ? LEVEL2_TOWER_UPGRADE_PRICE : LEVEL3_TOWER_UPGRADE_PRICE) - downgrade_income;
                            if (total_cost > avail_money) continue;

                            const Operation& upgrade_op = Util::upgrade_op(t_up, (tower_level == 1) ? TowerType::Quick : TowerType::Double);
                            if (logger.warn_if(!game_info.is_operation_valid(pid, upgrade_op), "invalid upgrade attempt")) continue;

                            Operation_list opl({upgrade_op}, -1, total_cost * UPGRADE_COST_MULT); // 为升级提供优惠
                            opl.append(down_op);
                            opl.evaluate(sim_round);
                            logger.err("upd: " + opl.str());

                            if (opl > best_result) best_result = opl;
                        }
                        if (it == game_info.towers.end()) break;
                    }

                    // 搜索：搬迁
                    build_cost = game_info.build_tower_cost(tower_num - 1) / 5; // 真实消耗：0.2倍塔造价
                    if (build_cost <= avail_money) {
                        for (const Tower& t : game_info.towers) {
                            if (t.player != pid || game_info.upgrade_tower_cost(t.type) != -1 || game_info.is_shielded_by_emp(t)) continue; // 被拆塔必然是1级的
                            Operation destroy_op(DowngradeTower, t.id);
                            if (logger.warn_if(!game_info.is_operation_valid(pid, destroy_op), "invalid destruct attempt")) continue;

                            for (const Pos& pos : feasible_hl[pid]) {
                                const Operation& build_op = Util::build_op(pos);
                                if (Util::closest_tower_dis(pos, t.id) < MIN_TOWER_DIST) continue;
                                if (!game_info.is_operation_valid(pid, build_op)) continue;

                                Operation_list opl({destroy_op, build_op}, sim_round, build_cost);
                                logger.err("mov: " + opl.str());
                                if (opl > best_result) best_result = opl;
                            }
                        }
                    }
                    // 紧急处理：EMP
                    const SuperWeaponType EB = SuperWeaponType::EmpBlaster, LS = SuperWeaponType::LightningStorm;
                    bool emp_active = std::any_of(game_info.super_weapons.begin(), game_info.super_weapons.end(),
                        [&](const SuperWeapon& sup){return sup.player != pid && sup.type == EB;});
                    bool lightning_valid = (game_info.super_weapon_cd[pid][LS] <= 0) && (SUPER_WEAPON_INFO[LS][3] <= avail_money);
                    if (raw_f_succ <= EMP_HANDLE_THRESH && emp_active && lightning_valid) {
                        Operation_list opl({Util::lightning_op(LIGHTNING_POS[pid])}, sim_round, SUPER_WEAPON_INFO[LS][3]);
                        logger.err("LS: " + opl.str());
                        if (opl > best_result) best_result = opl;
                    }
                }
                if (best_result.ops.size()) { // 实施搜索结果
                    logger.err("best: " + best_result.str());
                    for (const Operation& op : best_result.ops) {
                        ops.push_back(op);
                        avail_money += game_info.get_operation_income(pid, op);
                    }
                }
            }
        }
    
        // 当前可用钱数（计及即将执行操作的钱）
        int avail_money;

        // 已确定的操作
        std::vector<Operation> ops;

        // 计划任务队列，按时间升序排列
        std::priority_queue<Scheduled_task> schedule_queue;

        // 模拟的回合数
        static int get_sim_round(int curr_round) {
            if (curr_round < 90) return 70 + curr_round / 3;
            return 100;
        }

        static constexpr Pos FIRST_TOWER_POS[2] {{4, 9}, {14, 9}};
        static constexpr Pos LIGHTNING_POS[2] {{3, 9}, {15, 9}};

        static constexpr int MIN_TOWER_DIST = 4;
        static constexpr double UPGRADE_COST_MULT = 0.75;

        static constexpr int EMP_HANDLE_THRESH = 3;
};



int main() {
    AI_ ai = AI_();

    ai.run_ai();

    return 0;
}