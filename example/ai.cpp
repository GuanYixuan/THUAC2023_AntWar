#include "../include/simulate.hpp"
#include "../include/logger.hpp"

#include <queue>
#include <string>
#include <cassert>

constexpr bool DEBUG = true;

constexpr bool RELEASE = true;
constexpr bool LOG_SWITCH = false;
constexpr bool LOG_STDOUT = false;
constexpr int LOG_LEVEL = 0;

int pid; // 玩家(我的)编号
int ants_killed[2]; // 双方击杀蚂蚁数
int tower_value[2]; // 双方的固定资产（不包含被EMP的）
int avail_value[2]; // 双方的可用资产（不包含被EMP的）
int banned_tower_count[2]; // 双方因EMP被冻结的塔数
int banned_tower_value[2]; // 双方因EMP而不可用的固定资产

int warn_streak; // 连续处于“响应状态”的回合数

const GameInfo* info; // info的一份拷贝，用于Util等地

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
    static constexpr int BUILD_COST[8] = {0, 15, 30, 60, 120, 240, 480, 960}; // “修建”第i个塔需要的钱数
    static constexpr int ACCU_COST[8] = {0, 15, 45, 105, 225, 465, 945, 1905}; // “修完”第i个塔需要的钱数
    static constexpr int TOWER_REFUND[8] = {0, 12, 24, 48, 96, 192, 384, 768}; // 拆除第i个塔获得的返还 / 第i个塔所拥有的“数量价值”
    static constexpr int ACCU_REFUND[8] = {0, 12, 36, 84, 180, 372, 756, 1524}; // 拥有n个塔的累计固定资产
    static constexpr int MOVE_COST[8] = {0, 3, 6, 12, 24, 48, 96, 192}; // “搬迁”第i个塔需要的钱数（不包括等级开销）

    static constexpr int UPGRADE_COST[3] = {0, LEVEL2_TOWER_UPGRADE_PRICE, LEVEL3_TOWER_UPGRADE_PRICE}; // 升级i级塔的开销
    static constexpr int LEVEL_REFUND[4] = {0, 0, UPGRADE_COST[1]*4/5, (UPGRADE_COST[1]+UPGRADE_COST[2])*4/5}; // i级塔的“等级价值”

    static constexpr int EMP_RANGE = 3;

    /**
     * @brief 计算到给定点的最近塔的距离，可以选择排除一个塔
     * 
     * @param pos 给定的点（坐标）
     * @param exclude_id 要排除的塔id，默认不排除
     * @return int 排除exclude_id后的最近塔距离，没有塔时返回999
     */
    static int closest_tower_dis(const Pos& pos, int exclude_id = -1) {
        int ans = 999;
        for (const Tower& t : info->towers) {
            if (t.player != pid || t.id == exclude_id) continue;
            ans = std::min(ans, distance(t.x, t.y, pos.x, pos.y));
        }
        return ans;
    }
    /**
     * @brief 计算在给定点释放EMP后，被屏蔽的塔数量
     * 
     * @param pos 释放EMP的坐标
     * @param player_id 被EMP攻击的玩家编号
     * @return int 被屏蔽的塔数量
     */
    static int EMP_tower_count(const Pos& pos, int player_id) {
        return std::count_if(info->towers.begin(), info->towers.end(), [&](const Tower& t){ return t.player == player_id && distance(t.x, t.y, pos.x, pos.y) <= EMP_RANGE; });
    }
    /**
     * @brief 计算在给定点释放EMP后，被屏蔽的钱数
     * 
     * @param pos 释放EMP的坐标
     * @param player_id 被EMP攻击的玩家编号
     * @return int 被屏蔽的钱数
     */
    static int EMP_banned_money(const Pos& pos, int player_id) {
        int ans = 0;
        int banned_count = 0;
        for (const Tower& t : info->towers) {
            if (t.player != player_id || distance(t.x, t.y, pos.x, pos.y) > EMP_RANGE) continue;
            banned_count++;
            ans += TOWER_REFUND[banned_count] + LEVEL_REFUND[tower_level(t)];
        }
        return ans;
    }
    /**
     * @brief 计算给定塔的等级
     * 
     * @param t 参与计算的塔
     * @return int 塔的等级(1~3)
     */
    static int tower_level(const Tower& t) {
        if (t.type == TowerType::Basic) return 1;
        if (t.type <= 10) return 2;
        return 3;
    }
    static int update_cost(const Tower& t) {
        return UPGRADE_COST[tower_level(t)];
    }

    static Operation build_op(const Pos& pos) {
        return Operation(BuildTower, pos.x, pos.y);
    }
    static Operation upgrade_op(const Tower& t, TowerType tp) {
        return Operation(UpgradeTower, t.id, tp);
    }
    static Operation upgrade_op(int tower_id, TowerType tp) {
        return Operation(UpgradeTower, tower_id, tp);
    }
    static Operation lightning_op(const Pos& pos) {
        return Operation(UseLightningStorm, pos.x, pos.y);
    }
    static Operation EMP_op(const Pos& pos) {
        return Operation(UseEmpBlaster, pos.x, pos.y);
    }
    static Operation DFL_op(const Pos& pos) {
        return Operation(UseDeflector, pos.x, pos.y);
    }
    static Operation EVA_op(const Pos& pos) {
        return Operation(UseEmergencyEvasion, pos.x, pos.y);
    }
    static Operation EVA_op(const Ant& a) {
        return Operation(UseEmergencyEvasion, a.x, a.y);
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

    constexpr explicit Scheduled_task(const Operation& _op, int _round = 0) : round(_round), type(Task_type::tower), op(_op) {}

    bool operator<(const Scheduled_task& other) const {
        return round > other.round;
    }
    bool operator<=(int _round) const {
        return round <= _round;
    }
};

// 模拟结果类
struct Sim_result {
    int succ_ant; // 模拟过程中我方掉的血
    int ant_killed; // 被我方击杀的蚂蚁数
    int first_succ; // 模拟过程中我方第一次掉血的回合数（相对时间）
    int danger_encounter; // “危险抵近”（即容易被套盾发起攻击）的蚂蚁数

    int dmg_dealt; // 模拟过程中对方掉的血
    int dmg_time; // 模拟过程中对方第一次掉血的回合数（相对时间）

    constexpr Sim_result() : succ_ant(99), ant_killed(99), first_succ(0), danger_encounter(99), dmg_dealt(0), dmg_time(MAX_ROUND + 1) {}

    constexpr Sim_result(int _succ, int _kill, int _first, int _enc, int _dmg, int _dmg_time)
    : succ_ant(_succ), ant_killed(_kill), first_succ(_first), danger_encounter(_enc), dmg_dealt(_dmg), dmg_time(_dmg_time) {}

};
/**
 * @brief 用于模拟ant行动的模拟器，模拟期间不会考虑两个玩家的任何操作
 * @note 仅限一次性使用
 */
class Ant_simulator {
    static constexpr int INIT_HEALTH = 49;

    public:
        Ant_simulator(const std::vector<Scheduled_task>& my_task = {}) : sim_info(*info) {
            ops[pid] = my_task;
            for (int i = 0; i < 2; i++) sim_info.bases[i].hp = INIT_HEALTH;
        }
        Sim_result simulate(int round) {
            int base_damage[2]; // 模拟结果：基地受到的伤害[player_id]
            int first_succ = MAX_ROUND + 1;
            int dmg_time = MAX_ROUND + 1;
            std::vector<int> enc_ant_id;

            Simulator s(sim_info);

            for (int i = 0; i < 2; i++) std::sort(ops[i].begin(), ops[i].end(), Ant_simulator::__cmp_downgrade_last); // 将降级操作排到最后(因为操作从最后开始加)

            int _r = 0;
            for (; _r < round; ++_r) {
                if (pid == 0) {
                    // Add player0's operation
                    __add_op(s, _r, 0);
                    // Apply player0's operation
                    s.apply_operations_of_player(0);
                    // Add player1's operation
                    __add_op(s, _r, 1);
                    // Apply player1's operation
                    s.apply_operations_of_player(1);
                    // Next round
                    if (s.next_round() != GameState::Running) break;
                } else {
                    // Add player1's operation
                    __add_op(s, _r, 1);
                    // Apply player1's operation
                    s.apply_operations_of_player(1);
                    // Next round
                    if (s.next_round() != GameState::Running) break;
                    // Add player0's operation
                    __add_op(s, _r, 0);
                    // Apply player0's operation
                    s.apply_operations_of_player(0);
                }
                if (first_succ > MAX_ROUND) for (const Ant& a : s.get_info().ants) {
                    if (a.player == pid || distance(a.x, a.y, Base::POSITION[pid][0], Base::POSITION[pid][1]) > DANGER_RANGE) continue;
                    if (!std::count(enc_ant_id.begin(), enc_ant_id.end(), a.id)) enc_ant_id.push_back(a.id);
                }
                if (first_succ > MAX_ROUND && INIT_HEALTH != s.get_info().bases[pid].hp) first_succ = _r;
                if (dmg_time > MAX_ROUND && INIT_HEALTH != s.get_info().bases[!pid].hp) dmg_time = _r;
            }
            // assert(s.get_info().round >= 512 || _r == round);
            for (int i = 0; i < 2; i++) base_damage[i] = INIT_HEALTH - s.get_info().bases[i].hp;

            return Sim_result(base_damage[pid], s.ants_killed[pid], first_succ, enc_ant_id.size(), base_damage[!pid], dmg_time);
        }

        GameInfo sim_info;
        std::vector<Scheduled_task> ops[2];

    private:

        static constexpr int DANGER_RANGE = 4;
        // 将降级操作排到最后(因为操作从最后开始加)
        static bool __cmp_downgrade_last(const Scheduled_task& a, const Scheduled_task& b) {
            return a.op.type != DowngradeTower && b.op.type == DowngradeTower;
        }
        void __add_op(Simulator& s, int _r, int player) {
            std::vector<Scheduled_task>& tasks = ops[player];
            for (int i = tasks.size()-1; i >= 0; i--) {
                const Scheduled_task& curr_task = tasks[i];
                if (curr_task.round == _r) {
                    if (!s.add_operation_of_player(player, curr_task.op)) {
                        fprintf(stderr, "[w] Adding invalid operation for player %d at sim round %d: %s\n", player, _r, curr_task.op.str(true).c_str());
                    }
                    tasks.erase(tasks.begin() + i);
                }
            }
        }
};

/**
 * @brief 行动序列，同时包含模拟及比较功能
 * 
 */
class Operation_list {
    public:
    std::vector<Scheduled_task> ops;
    int cost; // 行动序列的“损失函数”，注意这往往不是真实花费

    Sim_result res;

    explicit Operation_list(const std::vector<Operation>& _ops, int eval_round = -1, int _cost = 0) : cost(_cost) {
        for (const Operation& op : _ops) append(op);
        if (eval_round >= 0) evaluate(eval_round);
    };
    void append(const Operation& op, int _round = 0) {
        ops.emplace_back(op, _round);
    }
    void append(const optional<Operation>& op, int _round = 0) {
        if (op) append(op.value(), _round);
    }

    const Sim_result& evaluate(int _round) {
        Ant_simulator sim(ops);
        res = sim.simulate(_round);
        return res;
    }

    std::string defence_str() const {
        std::string ret;
        int real_first_time = Operation_list::real_f_succ(res);

        if (res.succ_ant) ret += str_wrap("[s: %2d, f: %3d", res.succ_ant, res.first_succ);
        else ret += str_wrap("[f: %3d", res.first_succ);
        if (real_first_time != res.first_succ) ret += str_wrap("(%d)", real_first_time);
        ret += str_wrap(", c: %3d] [", cost);

        for (const Scheduled_task& task : ops) ret += task.op.str() + ' ';
        if (ops.size()) ret.pop_back();
        return ret + ']';
    }
    std::string attack_str() const {
        std::string ret = str_wrap("[dmg: %d, f: %2d, c: %3d] [", res.dmg_dealt, res.dmg_time, cost);
        for (const Scheduled_task& task : ops) ret += task.op.str() + ' ';
        if (ops.size()) ret.pop_back();
        return ret + ']';
    }

    static int real_f_succ(const Sim_result& res) {
        if (res.first_succ <= 40) return res.first_succ;
        return 40 + (res.first_succ - 40) / (res.danger_encounter + 1);
    }
    /**
     * @brief 比较当前行动序列与另一行动序列在“防守”方面的表现
     * 
     * @param other 要比较的另一行动序列
     * @return bool 当前行动序列在“防守”方面是否优于other
     */
    bool operator>(const Operation_list& other) const {
        if (Operation_list::real_f_succ(res) != Operation_list::real_f_succ(other.res)) return Operation_list::real_f_succ(res) > Operation_list::real_f_succ(other.res);
        return cost - res.ant_killed * 5 + res.succ_ant * 20 < other.cost - other.res.ant_killed * 5 + other.res.succ_ant * 20;
    }
    /**
     * @brief 比较当前行动序列与另一行动序列在“进攻”方面的表现
     * @note 比较优先级：对敌方造成的伤害、行动开销
     * @param other 要比较的另一行动序列
     * @return bool 当前行动序列在“进攻”方面是否优于other
     */
    bool attack_better_than(const Operation_list& other) const {
        if (res.dmg_dealt != other.res.dmg_dealt) return res.dmg_dealt > other.res.dmg_dealt;
        return cost < other.cost;
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
            avail_money = game_info.coins[player_id];

            // 经济信息统计
            for (int i = 0; i < 2; i++) {
                banned_tower_count[i] = banned_tower_value[i] = 0;
                tower_value[i] = Util::ACCU_REFUND[game_info.tower_num_of_player(i)];
            }
            for (const Tower& t : game_info.towers) {
                int u_cost = game_info.upgrade_tower_cost(t.type);
                if (game_info.is_shielded_by_emp(t)) {
                    banned_tower_count[t.player]++;
                    banned_tower_value[t.player] += Util::TOWER_REFUND[banned_tower_count[t.player]] + Util::LEVEL_REFUND[Util::tower_level(t)];
                    tower_value[t.player] -= Util::TOWER_REFUND[banned_tower_count[t.player]];
                } else tower_value[t.player] += Util::LEVEL_REFUND[Util::tower_level(t)];
            }
            for (int i = 0; i < 2; i++) avail_value[i] = tower_value[i] + game_info.coins[i];

            // 例行Log
            std::string disp = str_wrap("HP:%2d/%2d   ", game_info.bases[pid].hp, game_info.bases[!pid].hp);
            disp += str_wrap("Kill:%2d/%2d   ", ants_killed[pid], ants_killed[!pid]);
            disp += str_wrap("Coin: %3d (%3dC + %3dT", tower_value[pid] + game_info.coins[pid], game_info.coins[pid], tower_value[pid]);
            if (banned_tower_value[pid]) disp += str_wrap(" + %dU", banned_tower_value[pid]);
            disp += str_wrap(") vs %3d (%3dC + %3dT", tower_value[!pid] + game_info.coins[!pid], game_info.coins[!pid], tower_value[!pid]);
            if (banned_tower_value[!pid]) disp += str_wrap(" + %dU", banned_tower_value[!pid]);
            logger.err(disp + ')');

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

            // 操作重排序
            std::sort(ops.begin(), ops.end(), [](const Operation& a, const Operation& b){return a.type == DowngradeTower && b.type != DowngradeTower;});

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
        // 模拟检查：检查Simulator对一回合后的预测结果是否与实测符合，同时预测Ants_killed
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

            // 更新ants_killed的预测值
            for (int i = 0; i < 2; i++) ants_killed[i] += s.ants_killed[i];
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
            bool conducted = false;
            while (schedule_queue.size() && schedule_queue.top() <= game_info.round) {
                Scheduled_task task = schedule_queue.top();
                schedule_queue.pop();

                if (!game_info.is_operation_valid(pid, task.op)) {
                    logger.err("[w] Discard invalid operation %s", task.op.str(true).c_str());
                    continue;
                }
                int cost = -game_info.get_operation_income(pid, task.op);
                if (avail_money - cost < 0) {
                    logger.err("[w] Discarded operation %s, cost %d > %d", task.op.str(true).c_str(), cost, avail_money);
                    continue;
                }

                conducted = true;
                ops.push_back(task.op);
                avail_money -= cost;
                logger.err("Conduct scheduled task: %s", task.op.str(true).c_str());
            }
            if (conducted) return;

            // 塔操作搜索
            Operation_list raw_result({}, sim_round, 0);
            int raw_f_succ = raw_result.res.first_succ;
            if (game_info.round >= 12) {
                Operation_list best_result(raw_result);
                logger.err("raw: " + best_result.defence_str());

                bool emp_active = std::any_of(game_info.super_weapons.begin(), game_info.super_weapons.end(),
                    [&](const SuperWeapon& sup){return sup.player != pid && sup.type == SuperWeaponType::EmpBlaster;});
                bool dfl_active = std::any_of(game_info.super_weapons.begin(), game_info.super_weapons.end(),
                    [&](const SuperWeapon& sup){return sup.player != pid && sup.type == SuperWeaponType::Deflector;});
                bool warning_status = emp_active || dfl_active || raw_f_succ <= 10;
                if (best_result.res.first_succ >= 20) warn_streak = 0;
                else { // 如果啥事不干基地会扣血
                    warn_streak++;
                    warning_status |= (warn_streak > 5);

                    // 搜索：（拆除+）建塔（+升级）
                    for (auto it = game_info.towers.begin(); ; it++) {
                        int downgrade_income = 0; // 降级收入
                        optional<Operation> down_op = nullopt;
                        if (it != game_info.towers.end()) { // 如果要降级
                            const Tower& t_down = *it;
                            if (t_down.player != pid || game_info.is_shielded_by_emp(t_down)) continue; // 游戏规则

                            int down_level = Util::tower_level(t_down);
                            if ((down_level == 2 && tower_num < 3) && !warning_status) continue; // 非紧急情况下，仅在有4塔时允许降级2级塔
                            if (down_level == 3 && !emp_active) continue; // 非EMP情况下，不允许降级3级塔

                            down_op = Operation(DowngradeTower, t_down.id);
                            if (logger.warn_if(!game_info.is_operation_valid(pid, down_op.value()), "invalid destruct attempt")) continue;

                            downgrade_income = game_info.get_operation_income(pid, down_op.value());
                        } // 否则不降级

                        // 新建
                        int build_cost = game_info.build_tower_cost(tower_num) - downgrade_income;
                        if (build_cost <= avail_money && build_cost < 240) { // 暂时写死不允许建5号塔
                            for (const Pos& pos : feasible_hl[pid]) {
                                const Operation& build_op = Util::build_op(pos);
                                if (Util::closest_tower_dis(pos) < MIN_TOWER_DIST && !emp_active) continue;
                                if (!game_info.is_operation_valid(pid, build_op)) continue;

                                Operation_list opl_lv1({build_op}, -1, game_info.build_tower_cost(tower_num));
                                opl_lv1.append(down_op);

                                opl_lv1.evaluate(sim_round);
                                logger.err("bud:  " + opl_lv1.defence_str());
                                if (opl_lv1 > best_result) best_result = opl_lv1;

                                // 升级
                                if (avail_money < build_cost + Util::UPGRADE_COST[1]) continue; // 【其实很难讲塔的id会不会被别人抢先占用了】
                                for (const TowerType* const upd_path : BUILD_SERIES) {
                                    if (upd_path[0] == TowerType::Heavy && !warning_status) continue; // HEAVY->CANNON路线仅限紧急情况
                                    if (upd_path[0] == TowerType::Quick && upd_path[1] == TowerType::QuickPlus && !warning_status) continue; // QUICK+路线仅限紧急情况

                                    Operation_list opl_lv2(opl_lv1);
                                    opl_lv2.append(Util::upgrade_op(game_info.next_tower_id, upd_path[0]), 1);
                                    opl_lv2.cost += LEVEL2_TOWER_UPGRADE_PRICE * UPGRADE_COST_MULT;
                                    opl_lv2.evaluate(sim_round);
                                    logger.err("bud+: " + opl_lv2.defence_str());
                                    if (opl_lv2 > best_result) best_result = opl_lv2;

                                    if (avail_money < build_cost + Util::UPGRADE_COST[1] + Util::UPGRADE_COST[2] || !warning_status) continue; // 跳级到3级仅限紧急情况
                                    Operation_list opl_lv3(opl_lv2);
                                    opl_lv3.append(Util::upgrade_op(game_info.next_tower_id, upd_path[1]), 2);
                                    opl_lv3.cost += LEVEL3_TOWER_UPGRADE_PRICE * UPGRADE_COST_MULT;
                                    opl_lv3.evaluate(sim_round);
                                    logger.err("bud++: " + opl_lv3.defence_str());
                                    if (opl_lv3 > best_result) best_result = opl_lv3;
                                }
                            }
                        }
                        if (it == game_info.towers.end()) break;
                    }

                    // 搜索：（拆除）+升级
                    for (auto it = game_info.towers.begin(); ; it++) {
                        int downgrade_income = 0; // 降级收入
                        optional<Operation> down_op = nullopt;
                        if (it != game_info.towers.end()) { // 如果要降级
                            const Tower& t_down = *it;
                            if (t_down.player != pid || game_info.is_shielded_by_emp(t_down)) continue; // 游戏规则

                            int down_level = Util::tower_level(t_down);
                            bool banned_lv3 = (down_level == 3) && (t_down.type == TowerType::Double || t_down.type == TowerType::MortarPlus);
                            if ((down_level == 2 && tower_num < 3) && !warning_status) continue; // 非紧急情况下，仅在有4塔时允许降级2级塔
                            if (banned_lv3 && !emp_active) continue; // 非EMP情况下，不允许降级3级塔

                            down_op = Operation(DowngradeTower, t_down.id);
                            if (logger.warn_if(!game_info.is_operation_valid(pid, down_op.value()), "invalid destruct attempt")) continue;

                            downgrade_income = game_info.get_operation_income(pid, down_op.value());
                        } // 否则不降级

                        for (const Tower& t_up : game_info.towers) {
                            if (t_up.player != pid || (down_op.has_value() && down_op.value().arg0 == t_up.id) || game_info.is_shielded_by_emp(t_up)) continue; // 游戏规则

                            int tower_level = Util::tower_level(t_up);
                            int total_cost = Util::UPGRADE_COST[tower_level] - downgrade_income;
                            if (tower_level == 3 || total_cost > avail_money) continue;

                            for (const TowerType* const upd_path : UPDATE_SERIES) {
                                const Operation& upgrade_op = Util::upgrade_op(t_up, upd_path[tower_level-1]);
                                if (upd_path[tower_level-1] / 10 != t_up.type) continue; // 升级路线不匹配
                                if (logger.warn_if(!game_info.is_operation_valid(pid, upgrade_op), "invalid upgrade attempt")) continue;

                                Operation_list opl({upgrade_op}, -1, total_cost * UPGRADE_COST_MULT); // 为升级提供优惠
                                opl.append(down_op);
                                opl.evaluate(sim_round);
                                logger.err("upd: " + opl.defence_str());
                                if (opl > best_result) best_result = opl;
                            }
                        }
                        if (it == game_info.towers.end()) break;
                    }

                    // 搜索：搬迁
                    for (const Tower& t : game_info.towers) {
                        int tower_level = Util::tower_level(t);
                        if (t.player != pid || tower_level == 3 || game_info.is_shielded_by_emp(t)) continue; // 被拆塔必然是1或2级的

                        // 检验钱数
                        int build_cost = Util::MOVE_COST[tower_num] + (tower_level == 2 ? 12 : 0);
                        if (build_cost > avail_money) continue;

                        // 构造拆除动作序列
                        Operation_list opl_destroy({Operation(DowngradeTower, t.id)});
                        if (tower_level == 2) opl_destroy.append(Operation(DowngradeTower, t.id), 1);

                        for (const Pos& pos : feasible_hl[pid]) {
                            const Operation& build_op = Util::build_op(pos);
                            if (Util::closest_tower_dis(pos, t.id) < MIN_TOWER_DIST && !emp_active) continue;
                            if (!game_info.is_operation_valid(pid, build_op)) continue;

                            Operation_list opl(opl_destroy);
                            opl.append(build_op, 1);
                            if (tower_level == 2) opl.append(Util::upgrade_op(game_info.next_tower_id, t.type), 2); // 这里也有id的抢占问题
                            opl.cost = build_cost;

                            opl.evaluate(sim_round);
                            logger.err("mov: " + opl.defence_str());
                            if (opl > best_result) best_result = opl;
                        }
                    }
                    // 紧急处理：EMP
                    constexpr SuperWeaponType LS = SuperWeaponType::LightningStorm;
                    if ((raw_f_succ <= EMP_HANDLE_THRESH || warn_streak > 4) && emp_active && game_info.super_weapon_cd[pid][LS] <= 0) {
                        if (SUPER_WEAPON_INFO[LS][3] <= avail_money) {
                            Operation_list opl({Util::lightning_op(LIGHTNING_POS[pid])}, sim_round, SUPER_WEAPON_INFO[LS][3] * LIGHTNING_MULT);
                            logger.err("LS:    " + opl.defence_str());
                            if (opl > best_result) best_result = opl;
                        } else {
                            for (const Tower& t_down : game_info.towers) {
                                if (t_down.player != pid || game_info.is_shielded_by_emp(t_down)) continue; // 游戏规则
                                Operation down_op(DowngradeTower, t_down.id);
                                if (logger.warn_if(!game_info.is_operation_valid(pid, down_op), "invalid destruct(LS) attempt")) continue;

                                int downgrade_income = game_info.get_operation_income(pid, down_op);
                                if (downgrade_income + avail_money < SUPER_WEAPON_INFO[LS][3]) continue; // 钱不够
                                Operation_list opl({down_op, Util::lightning_op(LIGHTNING_POS[pid])}, sim_round, SUPER_WEAPON_INFO[LS][3] * LIGHTNING_MULT + downgrade_income);
                                logger.err("LS(-): " + opl.defence_str());
                                if (opl > best_result) best_result = opl;
                            }
                        }
                    }
                }
                if (best_result.ops.size()) { // 实施搜索结果
                    logger.err("best: " + best_result.defence_str());
                    // bool occupying_EMP = emp_active && best_result.res.first_succ <= raw_f_succ + 20 && 
                    // if (best_result.res.first_succ <= raw_f_succ + 10 && best_result.cost >= 10 * (best_result.res.first_succ - raw_f_succ)) logger.err("[Result not taken]");
                    // else
                    for (const Scheduled_task& task : best_result.ops) {
                        if (task.round == 0) {
                            ops.push_back(task.op);
                            avail_money += game_info.get_operation_income(pid, task.op);
                        } else {
                            Scheduled_task temp = task;
                            temp.round += game_info.round;
                            schedule_queue.push(temp);
                            logger.err("Operation scheduled at round %3d: %s", temp.round, task.op.str(true).c_str());
                        }
                    }
                }
            }

            // 进攻搜索：EVA和DFL
            // 总体思想：我不着急，钱尚可，要求2~3回合内打到对面基地
            bool attacking = false;
            Operation_list EVA_raw({}, EVA_SIM_ROUND, -1), best_EVA(EVA_raw);
            Operation_list DFL_raw({}, DFL_SIM_ROUND, -1), best_DFL(DFL_raw);
            constexpr SuperWeaponType EVA(SuperWeaponType::EmergencyEvasion), DFL(SuperWeaponType::Deflector);
            if (raw_f_succ >= 30 && avail_money >= 230) {
                if (game_info.super_weapon_cd[pid][EVA] <= 0) for (const Ant& a : game_info.ants) {
                    if (a.player != pid) continue;

                    Operation_list opl({Util::EVA_op(a)}, EVA_SIM_ROUND); // 暂时不知设什么cost好
                    opl.cost = opl.res.dmg_time;

                    if (opl.attack_better_than(best_EVA)) best_EVA = opl;
                    if (opl.res.dmg_dealt > EVA_raw.res.dmg_dealt && (opl.res.dmg_time <= 3 || (opl.res.dmg_dealt >= opl.res.dmg_time - 2)))
                        logger.err("Potential EVA %s", opl.attack_str().c_str());
                }
                if (game_info.super_weapon_cd[pid][DFL] <= 0) for (int x = 0; x < MAP_SIZE; x++) for (int y = 0; y < MAP_SIZE; y++) {
                    Pos p{x, y};
                    if (MAP_PROPERTY[x][y] == -1) continue;

                    Operation_list opl({Util::DFL_op(p)}, DFL_SIM_ROUND); // 暂时不知设什么cost好
                    opl.cost = opl.res.dmg_time;

                    if (opl.attack_better_than(best_DFL)) best_DFL = opl;
                    if (opl.res.dmg_dealt > DFL_raw.res.dmg_dealt) logger.err("Potential DFL %s", opl.attack_str().c_str());
                }

                // if (game_info.super_weapon_cd[pid][EVA] <= 0) if (best_EVA.res.dmg_dealt && best_EVA.res.dmg_time == 0 && !attacking) {
                //     logger.err("Conduct EVA attack " + best_EVA.attack_str());
                //     ops.push_back(best_EVA.ops.front().op);
                //     avail_money -= SUPER_WEAPON_INFO[EVA][3];
                //     attacking = true;
                // }
            }

            // 进攻搜索：EMP
            // 总体思想：我不着急，钱足够多，对面放不出LS（钱不够多或冷却中）
            Operation_list EMP_raw({}, EMP_SIM_ROUND), best_EMP(EMP_raw);
            constexpr SuperWeaponType EB = SuperWeaponType::EmpBlaster;
            bool op_ls_ready = game_info.super_weapon_cd[!pid][SuperWeaponType::LightningStorm] <= 0;
            bool economy_advantage = avail_money + tower_value[pid] - avail_value[!pid] >= 200;
            if (raw_f_succ >= 40 && avail_money >= 300 && game_info.super_weapon_cd[pid][EB] <= 0) {
                for (int x = 0; x < MAP_SIZE; x++) for (int y = 0; y < MAP_SIZE; y++) {
                    Pos p{x, y};
                    if (MAP_PROPERTY[x][y] == -1 || !Util::EMP_tower_count(p, !pid)) continue;

                    int banned_money = Util::EMP_banned_money(p, !pid);
                    // bool op_enough_cash = game_info.coins[!pid] + std::max(tower_value[!pid] - banned_money - 84, 0) / 2 >= 180;
                    // if (op_enough_cash && op_ls_ready) continue;
                    Operation_list opl({Util::EMP_op(p)}, EMP_SIM_ROUND, -banned_money+distance(x, y, Base::POSITION[!pid][0], Base::POSITION[!pid][1]));

                    if (opl.attack_better_than(best_EMP)) best_EMP = opl;
                    if (opl.res.dmg_dealt > EMP_raw.res.dmg_dealt && opl.res.dmg_dealt > 2 && opl.res.dmg_time <= 10)
                        logger.err("atk: Ban:%d/%2d %s", Util::EMP_tower_count(p, !pid), banned_money, opl.attack_str().c_str());
                }
                logger.err("best_atk: " + best_EMP.attack_str());

                // if (best_EMP.res.dmg_dealt >= 4 && !attacking) {
                //     logger.err("Conduct EMP attack " + best_EMP.attack_str());
                //     ops.push_back(best_EMP.ops.front().op);
                //     avail_money -= SUPER_WEAPON_INFO[EB][3];
                //     attacking = true;
                // }
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
            if (curr_round < 150) return 70 + curr_round / 3;
            return 120;
        }

        static constexpr Pos FIRST_TOWER_POS[2] {{4, 9}, {14, 9}};
        static constexpr Pos LIGHTNING_POS[2] {{3, 9}, {15, 9}};

        static constexpr int MIN_TOWER_DIST = 4;
        static constexpr double UPGRADE_COST_MULT = 0.85;
        static constexpr double LIGHTNING_MULT = 3;

        static constexpr TowerType BUILD_SERIES[][2] = {
            {TowerType::Quick, TowerType::Sniper}, {TowerType::Quick, TowerType::Double}, {TowerType::Quick, TowerType::QuickPlus},
            {TowerType::Mortar, TowerType::MortarPlus}, {TowerType::Heavy, TowerType::Cannon}};
        static constexpr TowerType UPDATE_SERIES[][2] = {{TowerType::Quick, TowerType::Double}, {TowerType::Mortar, TowerType::MortarPlus}};

        static constexpr int EMP_HANDLE_THRESH = 5;

        static constexpr int EMP_SIM_ROUND = 30;
        static constexpr int EVA_SIM_ROUND = 7;
        static constexpr int DFL_SIM_ROUND = 10;
};



int main() {
    AI_ ai = AI_();

    ai.run_ai();

    return 0;
}