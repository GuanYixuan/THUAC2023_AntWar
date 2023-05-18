#include "../include/control.hpp"

#include "../include/simulate.hpp"
#include "../include/logger.hpp"
#include "../include/operation.hpp"

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
int EVA_emergency; // 因对手释放EVA而进入紧急情况的剩余回合数

int reflect_limit; // 等待EMP结束，自身有足够资金进行“反射EMP攻击”的剩余回合数
int reflecting_EMP_countdown; // 卖出全部塔后, 等待进攻模块放EMP的剩余回合数

int peace_check_cd; // 距离下一次peace_check的最小回合数
int last_attack_round = -100; // 上一次发动攻击的回合数（绝对时间）

const GameInfo* info; // info的一份拷贝，用于Util等地


class Util {
    public:
    /**
     * @brief 计算到给定点的最近塔的距离，可以选择排除一个塔
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
     * @brief 计算给定玩家在给定局面下可调动的资金总额
     * @param info 给定的游戏局面
     * @param player_id 给定的玩家编号
     * @return int 可调动的资金总额
     */
    static int calc_total_value(const GameInfo& info, int player_id) {
        int banned_tower_count = 0;
        int tower_value = ACCU_REFUND[info.tower_num_of_player(player_id)];

        for (const Tower& t : info.towers) {
            if (t.player != player_id) continue;
            if (info.is_shielded_by_emp(t)) {
                banned_tower_count++;
                tower_value -= TOWER_REFUND[banned_tower_count];
            } else tower_value += LEVEL_REFUND[t.level()];
        }
        return tower_value + info.coins[player_id];
    }

    /**
     * @brief 获取在给定点释放EVA后，添加护盾的蚂蚁编号
     * @param pos 释放EVA的坐标
     * @param player_id 【释放EVA】的玩家编号
     * @return std::vector<int> 添加护盾的蚂蚁编号列表
     */
    static std::vector<int> EVA_ant(const Pos& pos, int player_id) {
        std::vector<int> ans;
        for (const Ant& a : info->ants) if (a.player == player_id && distance(a.x, a.y, pos.x, pos.y) <= EVA_RANGE) ans.push_back(a.id);
        return ans;
    }
    /**
     * @brief 计算在给定点释放EMP后，被屏蔽的塔数量
     * @param pos 释放EMP的坐标
     * @param player_id 被EMP攻击的玩家编号
     * @return int 被屏蔽的塔数量
     */
    static int EMP_tower_count(const Pos& pos, int player_id) {
        return std::count_if(info->towers.begin(), info->towers.end(), [&](const Tower& t){ return t.player == player_id && distance(t.x, t.y, pos.x, pos.y) <= EMP_RANGE; });
    }
    /**
     * @brief 判断是否存在一个EMP释放点能够覆盖所有塔，可以选择添加一个塔坐标
     * @param player_id 被EMP攻击的玩家编号
     * @param pos 新建塔的坐标
     * @return int 被屏蔽的高地面积
     */
    static bool EMP_cover_all(int player_id, std::optional<Pos> pos = {}) {
        std::vector<Pos> tower_pos;
        if (pos) tower_pos.push_back(pos.value());
        for (const Tower& t : info->towers) if (t.player == player_id) tower_pos.push_back({t.x, t.y});
        for (int x = 0; x < MAP_SIZE; x++) for (int y = 0; y < MAP_SIZE; y++)
            if (is_valid_pos(x, y) && std::all_of(tower_pos.begin(), tower_pos.end(), [&](const Pos& p){return p.dist_to(x, y) <= EMP_RANGE;})) return true;
        return false;
    }
    /**
     * @brief 计算在给定点释放EMP后，被屏蔽的钱数
     * @param pos 释放EMP的坐标
     * @param player_id 被EMP攻击的玩家编号
     * @return int 被屏蔽的钱数
     */
    static int EMP_banned_money(const Pos& pos, int player_id) { // 【可能需要debug，见#3942719】
        int ans = 0;
        int banned_count = 0;
        for (const Tower& t : info->towers) {
            if (t.player != player_id || distance(t.x, t.y, pos.x, pos.y) > EMP_RANGE) continue;
            banned_count++;
            ans += TOWER_REFUND[banned_count] + LEVEL_REFUND[t.level()];
        }
        return ans;
    }
    static int EMP_banned_money(const GameInfo& info, const Pos& pos, int player_id) {
        int ans = 0;
        int banned_count = 0;
        for (const Tower& t : info.towers) {
            if (t.player != player_id || distance(t.x, t.y, pos.x, pos.y) > EMP_RANGE) continue;
            banned_count++;
            ans += TOWER_REFUND[banned_count] + LEVEL_REFUND[t.level()];
        }
        return ans;
    }
    /**
     * @brief 检查新的塔是否有可能与其它塔同时被EMP覆盖，可以选择排除一个塔
     * @param new_tower 新塔的坐标
     * @param exclude_id 要排除的塔id，默认不排除
     * @return bool 判定的结果 
     */
    static bool EMP_can_cover(const Pos& new_tower, int exclude_id = -1) {
        for (int x = 0; x < MAP_SIZE; x++) for (int y = 0; y < MAP_SIZE; y++) {
            if (distance(x, y, new_tower.x, new_tower.y) > EMP_RANGE) continue;
            for (const Tower& t : info->towers) if (t.player == pid && distance(x, y, t.x, t.y) <= EMP_RANGE && t.id != exclude_id) return true;
        }
        return false;
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
                    std::vector<Operation> ops = ai_call_routine(c.self_player_id, c.info, _opponent_op);
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
                    Simulator fixer(c.info, pid);
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
                    std::vector<Operation> ops = ai_call_routine(c.self_player_id, c.info, _opponent_op);
                    // Add operations to controller
                    for (auto &op : ops) c.append_self_operation(op);
                    // Send operations to judger
                    c.send_self_operations();
                    // Apply operations to game state
                    c.apply_self_operations();
                    // Parallel Simulation
                    Simulator fixer(c.info, pid);
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
            const std::vector<Tower>& correct_tower = fixer.info.towers;
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
            const std::vector<Ant>& correct_ant = fixer.info.ants;
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
            if (!std::any_of(opponent_op.begin(), opponent_op.end(), [](const Operation& op){return op.type == UseEmergencyEvasion;})) EVA_emergency--;
            else EVA_emergency = std::max(EVA_emergency, EVA_EMERGENCY_ROUND);

            // 经济信息统计
            for (int i = 0; i < 2; i++) {
                banned_tower_count[i] = banned_tower_value[i] = 0;
                tower_value[i] = ACCU_REFUND[game_info.tower_num_of_player(i)];
            }
            for (const Tower& t : game_info.towers) {
                if (game_info.is_shielded_by_emp(t)) {
                    banned_tower_count[t.player]++;
                    banned_tower_value[t.player] += TOWER_REFUND[banned_tower_count[t.player]] + LEVEL_REFUND[t.level()];
                    tower_value[t.player] -= TOWER_REFUND[banned_tower_count[t.player]];
                } else tower_value[t.player] += LEVEL_REFUND[t.level()];
            }
            for (int i = 0; i < 2; i++) avail_value[i] = tower_value[i] + game_info.coins[i];

            // 例行Log
            std::string disp = str_wrap("HP:%2d/%2d   ", game_info.bases[pid].hp, game_info.bases[!pid].hp);
            disp += str_wrap("Kill:%2d/%2d   ", ants_killed[pid], ants_killed[!pid]);
            disp += str_wrap("Money: %3d (%3dC + %3dT", tower_value[pid] + game_info.coins[pid], game_info.coins[pid], tower_value[pid]);
            if (banned_tower_value[pid]) disp += str_wrap(" + %dU", banned_tower_value[pid]);
            disp += str_wrap(") vs %3d (%3dC + %3dT", tower_value[!pid] + game_info.coins[!pid], game_info.coins[!pid], tower_value[!pid]);
            if (banned_tower_value[!pid]) disp += str_wrap(" + %dU", banned_tower_value[!pid]);
            logger.err(disp + ')');

            // SuperWeapon log
            if (game_info.super_weapons.size()) {
                std::string sup_disp("Active super weapon:");
                for (const SuperWeapon& s : game_info.super_weapons)
                    sup_disp += str_wrap(" [id:%d, remain:%d, player%d at (%d, %d)]", s.type, s.left_time, s.player, s.x, s.y);
                logger.err(sup_disp);
            }


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

            // Simulator/Ant log
            int max_age = -1;
            const Ant* oldest = NULL;
            for (const Ant& a : game_info.ants) if (a.player == pid && a.age > max_age) {
                oldest = &a;
                max_age = a.age;
            }

            logger.err("Sim:%d Round:%d,  Max age %d %s",
                Simulator::sim_count - last_sim_count, Simulator::round_count - last_round_count, max_age, oldest ? oldest->str(true).c_str() : "");
            last_sim_count = Simulator::sim_count;
            last_round_count = Simulator::round_count;

            return ops;
        }

    private:
        std::string pred;
        int last_sim_count = 0;
        int last_round_count = 0;
        // 模拟检查：检查Simulator对一回合后的预测结果是否与实测符合
        void ai_simulation_checker_pre(const GameInfo &game_info, const std::vector<Operation>& opponent_op) {
            std::string cur;
            for (const Ant& a : game_info.ants) cur += a.str(true);
            if (cur != pred && game_info.round > 0) {
                if (opponent_op.size()) {
                    logger.err("Predition and truth differ for round %d (opponent act)", game_info.round);
                    return;
                } else logger.err("[w] Predition and truth differ for round %d", game_info.round);
                logger.err("Pred: " + pred);
                logger.err("Truth:" + cur);

                std::string tow = "Towers: ";
                for (const Tower& t : game_info.towers) tow += t.str(true);
                logger.err(tow);
            }
        }
        // 模拟检查：检查Simulator对一回合后的预测结果是否与实测符合，同时预测Ants_killed
        void ai_simulation_checker_pos(const GameInfo &game_info) {
            Simulator s(game_info, pid);
            // s.verbose = 1;
            if (pid == 0) {
                // Add player0's operation
                for (const Operation& op : ops) s.operations[0].push_back(op);
                // Apply player0's operation
                s.apply_operations_of_player(0);
                // Add player1's operation
                // Apply player1's operation
                s.apply_operations_of_player(1);
                // Next round
                s.next_round();
            } else {
                // Add player1's operation
                for (const Operation& op : ops) s.operations[1].push_back(op);
                // Apply player1's operation
                s.apply_operations_of_player(1);
                // Next round
                s.next_round();
                // Add player0's operation
                // Apply player0's operation
                s.apply_operations_of_player(0);
            }
            pred = "";
            for (const Ant& a : s.info.ants) pred += a.str(true);

            // 更新ants_killed的预测值
            for (int i = 0; i < 2; i++) ants_killed[i] += s.ants_killed[i];
        }

        // 主决策逻辑
        void ai_main(const GameInfo &game_info, const std::vector<Operation>& opponent_op) {
            // 公共变量
            int sim_round = get_sim_round(game_info.round);
            int tower_num = game_info.tower_num_of_player(pid);

            // 处理计划任务
            bool conducted = false;
            while (schedule_queue.size() && schedule_queue.top() <= game_info.round) {
                Task task = schedule_queue.top();
                schedule_queue.pop();

                if (!game_info.is_operation_valid(pid, task.op)) {
                    logger.err("[w] Discard invalid operation %s", task.op.str(true).c_str());
                    continue;
                }
                int cost = -game_info.get_operation_income(pid, task.op);
                if (avail_money - cost < 0) {
                    logger.err("[w] Discard operation %s, cost %d > %d", task.op.str(true).c_str(), cost, avail_money);
                    continue;
                }

                conducted = true;
                ops.push_back(task.op);
                avail_money -= cost;
                logger.err("Conduct scheduled task: %s", task.op.str(true).c_str());
            }
            if (conducted) return;

            // 塔操作搜索
            Operation_list raw_result({}, sim_round);
            Operation_list best_result(raw_result);
            int raw_f_succ = raw_result.res.first_succ;

            bool EMP_active = std::any_of(game_info.super_weapons.begin(), game_info.super_weapons.end(),
                    [&](const SuperWeapon& sup){return sup.player != pid && sup.type == SuperWeaponType::EmpBlaster;});
            bool DFL_active = std::any_of(game_info.super_weapons.begin(), game_info.super_weapons.end(),
                [&](const SuperWeapon& sup){return sup.player != pid && sup.type == SuperWeaponType::Deflector;});
            bool aware_status = raw_f_succ < 20;
            bool warning_status = EMP_active || DFL_active || EVA_emergency > 0 || raw_f_succ <= 10;
            bool peace_check = (avail_money >= 130 || avail_value[pid] >= 250) && (raw_f_succ >= 40) && (raw_result.res.next_old <= 20 || raw_result.res.first_enc <= 20);

            int enemy_base_level = game_info.bases[!pid].ant_level;
            peace_check &= (enemy_base_level < 2) && (peace_check_cd <= 0);
            peace_check_cd--;

            aware_status &= (reflecting_EMP_countdown <= 0);

            if (game_info.round >= 12 && game_info.round < 511) { // round 511 bugfix
                std::string situation_log("raw: " + best_result.defence_str());
                if (aware_status) {
                    warn_streak++;
                    warning_status |= (warn_streak > 5);
                    situation_log += str_wrap(", streak: %d", warn_streak);
                } else warn_streak = 0;
                if (EVA_emergency > 0) situation_log += str_wrap(", EVA_emer: %d", EVA_emergency);
                if (reflect_limit > 0) situation_log += str_wrap(", wait for reflect: %d", reflect_limit);
                if (reflecting_EMP_countdown > 0) situation_log += str_wrap(", try to EMP: %d", reflecting_EMP_countdown);
                logger.err(situation_log);

                if (raw_f_succ == 0 && EMP_active && avail_value[!pid] <= 100) reflect_limit = 50;
                else reflect_limit--;

                if (aware_status) { // 如果啥事不干基地会扣血
                    // 搜索：（拆除+）建塔/升级
                    Op_generator build_gen(game_info, pid, avail_money);
                    if (warning_status) build_gen << Sell_cfg{3, 3};
                    build_gen.generate_operations();

                    for (const Defense_operation& op_list : build_gen.ops) {
                        std::optional<Pos> build_pos;
                        for (const Task& t : op_list.ops) if (t.op.type == OperationType::BuildTower) build_pos = {t.op.arg0, t.op.arg1};
                        assert(!build_pos || is_highland(pid, build_pos.value().x, build_pos.value().y));

                        Operation_list opl({}, -1, op_list.loss, op_list.cost, !pid);
                        opl.ops = op_list.ops;
                        opl.evaluate(sim_round);

                        // 判定修建后是否“在任何时刻都能放出LS”
                        if (opl.res.first_succ > EMP_COVER_PENALTY) {
                            int min_avail = min_avail_money_under_EMP(game_info, op_list);
                            if (min_avail < 150) opl.max_f_succ = EMP_COVER_PENALTY + 5 * (double(min_avail) / 150);
                        }

                        if (!opl.res.early_stop && opl > raw_result) logger.err((build_pos ? "bud: " : "upd: ") + opl.defence_str());
                        if (opl > best_result) best_result = opl;
                    }

                    // 紧急处理：EMP
                    constexpr SuperWeaponType LS(SuperWeaponType::LightningStorm);
                    constexpr int LS_cost = SUPER_WEAPON_INFO[LS][3];
                    if ((raw_f_succ <= EMP_HANDLE_THRESH || warn_streak > 4) && EMP_active && game_info.super_weapon_cd[pid][LS] <= 0) {
                        Op_generator gen(game_info, pid, avail_money);
                        gen << Sell_cfg{3, 3} << Build_cfg{false} << Upgrade_cfg{0} << LS_cfg{true};
                        gen.generate_operations();

                        for (const Defense_operation& op_list : gen.ops) {
                            Operation_list opl({}, -1, op_list.loss, op_list.cost, !pid);
                            opl.ops = op_list.ops;
                            opl.evaluate(sim_round);

                            if (opl > raw_result) logger.err("LS:   " + opl.defence_str());
                            if (opl > best_result) best_result = opl;
                        }
                    }
                } else if (peace_check) { // 和平时期检查
                    // 搜索：（拆除+）建塔/升级
                    Op_generator build_gen(game_info, pid, avail_money);
                    build_gen.sell.tweaking = true;
                    build_gen.build.lv3_options.clear();
                    build_gen.upgrade.max_count = 1;
                    build_gen.generate_operations();

                    for (const Defense_operation& op_list : build_gen.ops) {
                        std::optional<Pos> build_pos;
                        for (const Task& t : op_list.ops) if (t.op.type == OperationType::BuildTower) build_pos = {t.op.arg0, t.op.arg1};
                        assert(!build_pos || is_highland(pid, build_pos.value().x, build_pos.value().y));

                        if (op_list.cost > 60) continue;

                        Operation_list opl({}, -1, op_list.loss, op_list.cost, !pid);
                        opl.ops = op_list.ops;
                        opl.evaluate(sim_round);

                        // 判定修建后是否“在任何时刻都能放出LS”
                        if (opl.res.first_succ > EMP_COVER_PENALTY) {
                            int min_avail = min_avail_money_under_EMP(game_info, op_list);
                            if (min_avail < 150) opl.max_f_succ = EMP_COVER_PENALTY + 5 * (double(min_avail) / 150);
                        }

                        if (!opl.res.early_stop && opl > raw_result) logger.err((build_pos ? "p_bud: " : "p_upd: ") + opl.defence_str());
                        if (opl > best_result) best_result = opl;
                    }
                }
            }

            // Reflect EMP
            reflecting_EMP_countdown--;
            if (reflect_limit > 0 && avail_value[pid] >= 170) {
                Op_generator sell_gen(game_info, pid, avail_money);
                sell_gen << Sell_cfg{10, 3};
                sell_gen.generate_sell_list();
                best_result.ops = sell_gen.sell_list.back().ops;

                reflect_limit = 0;
                reflecting_EMP_countdown = 2;
            }

            if (best_result.ops.size()) { // 实施搜索结果
                logger.err("best: " + best_result.defence_str());

                if (peace_check) {
                    if (enemy_base_level) peace_check_cd = 30;
                    else peace_check_cd = 20;
                }

                bool occupying_LS = EMP_active && best_result.res.first_succ <= raw_f_succ + 20
                    && (avail_money + tower_value[pid] > 150 && avail_money + tower_value[pid] - best_result.cost <= 150)
                    && !std::any_of(best_result.ops.begin(), best_result.ops.end(), [](const Task& sc){return sc.op.type == UseLightningStorm;});
                if (occupying_LS) logger.err("[Occupying LS, result not taken]");

                else for (const Task& task : best_result.ops) {
                    if (task.round == 0) {
                        ops.push_back(task.op);
                        avail_money += game_info.get_operation_income(pid, task.op);
                    } else {
                        Task temp = task;
                        temp.round += game_info.round;
                        schedule_queue.push(temp);
                        logger.err("Operation scheduled at round %3d: %s", temp.round, task.op.str(true).c_str());
                    }
                }
                return;
            }

            // 是否进入到“卷击杀数”的状态
            bool consider_old = (game_info.bases[pid].hp == game_info.bases[!pid].hp) && (ants_killed[pid] < ants_killed[!pid] + 3);

            // 进攻搜索：EVA
            // 总体思想：尽量2~3回合内打到对面基地，减少对方反应时间
            int last_atk = game_info.round - last_attack_round;
            Operation_list EVA_raw({}, EVA_SIM_ROUND, -1), best_EVA(EVA_raw);
            constexpr SuperWeaponType EVA(SuperWeaponType::EmergencyEvasion);

            bool EVA_economy_crit = (avail_money >= 210) || (game_info.coins[!pid] <= 130 && avail_money >= 160 + 50 * game_info.bases[!pid].ant_level);
            if (game_info.super_weapon_cd[pid][EVA] <= 0 && last_atk > 5 && avail_money >= 100) if (raw_f_succ >= 30) {
                std::vector<std::vector<int>> scaned;
                if (game_info.super_weapon_cd[pid][EVA] <= 0) for (int x = 0; x < MAP_SIZE; x++) for (int y = 0; y < MAP_SIZE; y++) {
                    Pos p{x, y};
                    if (MAP_PROPERTY[x][y] == -1) continue;

                    std::vector<int> curr(Util::EVA_ant(p, pid));
                    if (!curr.size() || std::count(scaned.begin(), scaned.end(), curr)) continue;
                    scaned.push_back(curr);

                    Operation_list opl({EVA_op(p)}, EVA_SIM_ROUND, -curr.size());
                    opl.atk_side = pid;

                    // 进攻效果判据
                    bool old_cond = opl.res.old_opp > EVA_raw.res.old_opp;
                    if (opl.res.dmg_dealt <= EVA_raw.res.dmg_dealt || old_cond) continue;

                    // 部分经济要求判据
                    int min_avail = min_avail_money_under_EMP(game_info, {opl.ops}) * (game_info.super_weapon_cd[pid][SuperWeaponType::LightningStorm] <= 0);
                    if (!(EVA_economy_crit || min_avail >= 160)) continue;

                    // 模拟对方防守
                    Simulator raw_sim(game_info, pid, pid);
                    raw_sim.task_list[pid].emplace_back(EVA_op(p));
                    raw_sim.step_to_next_player();

                    Op_generator generator(raw_sim.info, !pid);
                    generator.generate_operations();

                    bool defended = false;
                    bool old_defended = false;
                    for (const Defense_operation& op_list : generator.ops) {
                        Simulator atk_sim(raw_sim.info, !pid, pid);
                        atk_sim.task_list[!pid] = op_list.ops;
                        Sim_result res = atk_sim.simulate(EVA_SIM_ROUND, EVA_SIM_ROUND);

                        if (res.old_opp < opl.res.old_opp) old_defended = true;
                        if (res.first_succ > EVA_SIM_ROUND || res.succ_ant < EVA_raw.res.dmg_dealt) {
                            defended = true;
                            logger.err("%s solved by %s", opl.attack_str().c_str(), op_list.str().c_str());
                            break;
                        }
                    }
                    // 如果（对方）未找到解，则更新答案
                    if (!defended) {
                        logger.err("Not solved EVA %s", opl.attack_str().c_str());
                        if (opl.attack_better_than(best_EVA, consider_old)) best_EVA = opl;
                    } else if (consider_old && old_cond && !old_defended) {
                        logger.err("EVA attack for old %s", opl.attack_str().c_str());
                        if (opl.attack_better_than(best_EVA, consider_old)) best_EVA = opl;
                    }
                }

                bool fast_EVA_trigger = (best_EVA.res.dmg_time <= 5);
                if (best_EVA.res.dmg_dealt > EVA_raw.res.dmg_dealt && fast_EVA_trigger) {
                    // refine一下
                    logger.err("EVA refining:");
                    bool local_best = true;
                    Operation_list best_refine(best_EVA);
                    for (int i = 1; i <= EVA_REFINE_ROUND && local_best; i++) {
                        best_refine.ops.front().round = i;
                        best_refine.evaluate(EVA_SIM_ROUND);
                        best_refine.res.dmg_time -= i; // 对模拟i回合的补偿
                        logger.err("EVA refine: %s", best_refine.attack_str().c_str());
                        if (best_refine.attack_better_than(best_EVA, consider_old)) local_best = false;
                    }

                    if (local_best) {
                        logger.err("Conduct EVA attack " + best_EVA.attack_str());
                        ops.push_back(best_EVA.ops.front().op);
                        avail_money -= SUPER_WEAPON_INFO[EVA][3];
                        last_atk = game_info.round;
                    }
                }
            }

            // 进攻搜索：EMP
            // 总体思想：我不着急，钱足够多，对面放不出LS（钱不够多或冷却中）
            Operation_list EMP_raw({}, EMP_SIM_ROUND), best_EMP(EMP_raw);
            constexpr SuperWeaponType EB = SuperWeaponType::EmpBlaster;

            bool reflect_tag = reflecting_EMP_countdown >= 0;
            bool op_ls_ready = game_info.super_weapon_cd[!pid][SuperWeaponType::LightningStorm] <= 0;
            bool EMP_economy_crit = !op_ls_ready || reflect_tag || (avail_money >= 200);
            // bool EMP_economy_crit = !op_ls_ready || reflect_tag || (avail_money >= 250) || (game_info.coins[!pid] < 150 && avail_money >= 210);
            if (game_info.super_weapon_cd[pid][EB] <= 0 && last_atk > 5 && avail_money >= 150) if (raw_f_succ >= 40 || reflect_tag) {
                for (int x = 0; x < MAP_SIZE; x++) for (int y = 0; y < MAP_SIZE; y++) {
                    Pos p{x, y};
                    if (MAP_PROPERTY[x][y] == -1 || !Util::EMP_tower_count(p, !pid)) continue;

                    Operation_list opl({EMP_op(p)}, EMP_SIM_ROUND, -Util::EMP_banned_money(p, !pid));
                    opl.atk_side = pid;

                    int min_avail = min_avail_money_under_EMP(game_info, {opl.ops}) * (game_info.super_weapon_cd[pid][SuperWeaponType::LightningStorm] <= 0);
                    bool dmg_cond = opl.res.dmg_dealt > EMP_raw.res.dmg_dealt && opl.res.dmg_dealt > 2;
                    bool old_cond = opl.res.old_opp > EMP_raw.res.old_opp;
                    if (reflect_tag || dmg_cond || old_cond) if (EMP_economy_crit || min_avail >= 160) { // 部分经济要求下放到这里
                        // 模拟对方防守
                        Simulator raw_sim(game_info, pid, pid);
                        raw_sim.task_list[pid].emplace_back(EMP_op(p));
                        raw_sim.step_to_next_player();

                        Op_generator generator(raw_sim.info, !pid);
                        generator << LS_cfg{true};
                        generator.generate_operations();

                        bool ls_defended = false;
                        bool build_defended = false;
                        bool old_defended = false;
                        for (const Defense_operation& op_list : generator.ops) {
                            if (ls_defended && op_list.has_ls()) continue;

                            Simulator atk_sim(raw_sim.info, !pid, pid);
                            atk_sim.task_list[!pid] = op_list.ops;
                            Sim_result res = atk_sim.simulate(EMP_SIM_ROUND, EMP_SIM_ROUND);

                            if (res.old_opp < opl.res.old_opp) old_defended = true;
                            if (res.first_succ > EMP_SIM_ROUND || res.succ_ant < EMP_raw.res.dmg_dealt) {
                                if (!op_list.has_ls()) build_defended = true;
                                else ls_defended = true;

                                if (build_defended) break;
                            }
                        }

                        if (reflect_tag && opl.attack_better_than(best_EMP, consider_old)) {
                            logger.err("Possible reflect EMP %s", opl.attack_str().c_str());
                            best_EMP = opl;
                        } else if (ls_defended && !build_defended) { // （对方）只找到LS解
                            logger.err("%s solved by LS", opl.attack_str().c_str());
                            if (opl.attack_better_than(best_EMP, consider_old)) best_EMP = opl;
                        } else if (!ls_defended && !build_defended) { // （对方）未找到解
                            logger.err("Not solved EMP %s", opl.attack_str().c_str());
                            opl.res.dmg_dealt += 100; // 标记为“不可解”
                            if (opl.attack_better_than(best_EMP, consider_old)) best_EMP = opl;
                        } else if (consider_old && old_cond && !old_defended) { // 未找到“防止老死”的解
                            logger.err("EMP Attack for old %s", opl.attack_str().c_str());
                            if (opl.attack_better_than(best_EMP, consider_old)) best_EMP = opl;
                        }
                    }
                }
                logger.err("raw best_EMP: " + best_EMP.attack_str());

                bool unsolved_trigger = (best_EMP.res.dmg_dealt > 100);
                bool force_ls_trigger = (avail_value[pid] - avail_value[!pid] >= 150);
                force_ls_trigger |= (avail_money >= 250 && avail_value[pid] >= 300);
                if (best_EMP.res.dmg_dealt > EMP_raw.res.dmg_dealt && (unsolved_trigger || force_ls_trigger || reflect_tag)) {
                    // 不可解，或己方经济有优势时挤压对方
                    std::string pr;
                    if (reflect_limit > 0) pr += "(Reflect)";
                    else if (unsolved_trigger) pr += "(Unsolved)";
                    else pr += "(Force LS)";

                    // refine一下
                    logger.err("EMP refining:");
                    bool local_best = true;
                    Operation_list best_refine(best_EMP);
                    for (int i = 1; i <= EMP_REFINE_ROUND && local_best; i++) {
                        best_refine.ops.front().round = i;
                        best_refine.evaluate(EMP_SIM_ROUND);
                        best_refine.res.dmg_time -= i; // 对模拟i回合的补偿
                        if (unsolved_trigger) best_refine.res.dmg_dealt += 100;
                        logger.err("EMP refine: %s", best_refine.attack_str().c_str());
                        if (best_refine.attack_better_than(best_EMP, consider_old)) local_best = false;
                    }

                    if (local_best || reflect_tag) {
                        logger.err(pr + " Conduct EMP attack " + best_EMP.attack_str());
                        ops.push_back(best_EMP.ops.front().op);
                        avail_money -= SUPER_WEAPON_INFO[EB][3];
                        last_atk = game_info.round;

                        if (reflect_tag) reflecting_EMP_countdown = 0;
                    }
                }
            }

            bool draw_cond = (game_info.bases[pid].hp <= game_info.bases[!pid].hp) && (ants_killed[pid] <= ants_killed[!pid]);
            bool money_cond = (avail_money >= 300) && (min_avail_money_under_EMP(game_info, {{Task(Operation(UpgradeGeneratedAnt))}}) >= 150);
            if (money_cond && draw_cond && !game_info.bases[pid].ant_level) {
                logger.err("[Upgrading base]");
                ops.emplace_back(UpgradeGeneratedAnt);
                avail_money -= LEVEL2_BASE_UPGRADE_PRICE;
            }
        }

        int min_avail_money_under_EMP(const GameInfo& game_info, const Defense_operation& my_op) {
            Simulator op_done{game_info, pid, !pid};
            op_done.task_list[pid] = my_op.ops;
            op_done.simulate(my_op.round_needed+1, -1);

            int ans = 1e7;
            int full = Util::calc_total_value(op_done.info, pid);
            for (int x = 0; x < MAP_SIZE; x++) for (int y = 0; y < MAP_SIZE; y++) {
                Pos p{x, y};
                if (MAP_PROPERTY[x][y] == -1) continue;
                int residual = full - Util::EMP_banned_money(op_done.info, p, pid);
                ans = std::min(ans, residual);
            }
            return ans;
        }

        // 当前可用钱数（计及即将执行操作的钱）
        int avail_money;

        // 已确定的操作
        std::vector<Operation> ops;

        // 计划任务队列，按时间升序排列
        std::priority_queue<Task> schedule_queue;

        // 模拟的回合数
        static int get_sim_round(int curr_round) {
            if (curr_round < 150) return 70 + curr_round / 3;
            return 120;
        }

        static constexpr int EMP_COVER_PENALTY = 40;

        static constexpr int EMP_HANDLE_THRESH = 5;
        static constexpr int EVA_EMERGENCY_ROUND = 10;

        static constexpr int EMP_SIM_ROUND = 20;
        static constexpr int EMP_REFINE_ROUND = 7;

        static constexpr int EVA_SIM_ROUND = 10;
        static constexpr int EVA_REFINE_ROUND = 7;

        static constexpr int DFL_SIM_ROUND = 10;
};



int main() {
    AI_ ai = AI_();

    ai.run_ai();

    return 0;
}