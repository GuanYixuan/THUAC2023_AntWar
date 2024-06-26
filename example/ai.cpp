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
     * @brief 计算在给定点释放EMP后，被屏蔽的塔数量
     * @param pos 释放EMP的坐标
     * @param player_id 被EMP攻击的玩家编号
     * @return int 被屏蔽的塔数量
     */
    static int EMP_tower_count(const Pos& pos, int player_id) {
        return std::count_if(info->towers.begin(), info->towers.end(), [&](const Tower& t){ return t.player == player_id && distance(t.x, t.y, pos.x, pos.y) <= EMP_RANGE; });
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

            // 初始化距离数组
            init_dist_array(); 
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

            // raw results
            Operation_list raw_result({}, sim_round);
            Operation_list best_result(raw_result);
            int raw_f_succ = raw_result.res.first_succ;

            // status flags
            bool EMP_active = std::any_of(game_info.super_weapons.begin(), game_info.super_weapons.end(),
                    [&](const SuperWeapon& sup){return sup.player != pid && sup.type == SuperWeaponType::EmpBlaster;});
            bool DFL_active = std::any_of(game_info.super_weapons.begin(), game_info.super_weapons.end(),
                [&](const SuperWeapon& sup){return sup.player != pid && sup.type == SuperWeaponType::Deflector;});
            bool aware_status = (raw_f_succ < 20) && (reflecting_EMP_countdown <= 0);
            bool warning_status = EMP_active || DFL_active || EVA_emergency > 0 || raw_f_succ <= 10;
            bool peace_check = (avail_money >= 130 || avail_value[pid] >= 250) && (raw_f_succ >= 40);
            peace_check &= (raw_result.res.next_old <= 20 && game_info.bases[pid].hp >= game_info.bases[!pid].hp) || (raw_result.res.first_enc <= 20);

            bool hp_draw = (game_info.bases[pid].hp == game_info.bases[!pid].hp); // 血量是否打平

            int enemy_base_level = game_info.bases[!pid].ant_level;
            peace_check &= (enemy_base_level < 2);
            peace_check &= (peace_check_cd <= 0) || (game_info.round >= 500);
            peace_check_cd--;

            // situation log
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

            int atk_start_time = Simulator::round_count;
            if (game_info.round >= 12) {
                if (aware_status) { // 如果啥事不干基地会扣血
                    // 搜索：（拆除+）建塔/升级
                    Op_generator build_gen(game_info, pid, avail_money);
                    if (warning_status) build_gen << Sell_cfg{3, 3};
                    build_gen.generate_operations();

                    for (const Defense_operation& op_list : build_gen.ops) {
                        if (game_info.round + op_list.round_needed >= MAX_ROUND) continue;

                        std::optional<Pos> build_pos;
                        for (const Task& t : op_list.ops) if (t.op.type == OperationType::BuildTower) build_pos = {t.op.arg0, t.op.arg1};
                        assert(!build_pos || is_highland(pid, build_pos.value().x, build_pos.value().y));

                        Operation_list opl({}, -1, op_list.loss, op_list.cost, !pid);
                        opl.ops = op_list.ops;
                        opl.evaluate(sim_round, best_result.res.first_succ);

                        // 判定修建后是否“在任何时刻都能放出LS”
                        if (opl.res.first_succ > EMP_COVER_PENALTY) {
                            int min_avail = min_avail_money_under_EMP(game_info, op_list);
                            if (min_avail < 150) opl.max_f_succ = EMP_COVER_PENALTY + 5 * (double(min_avail) / 150);
                        }

                        if (opl > best_result) {
                            logger.err((build_pos ? "bud: " : "upd: ") + opl.defence_str());
                            best_result = opl;
                        }
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
                    // 是否需要承接末期LS的使命
                    bool no_ls = (avail_value[pid] < 130) || (game_info.round + game_info.super_weapon_cd[pid][SuperWeaponType::LightningStorm] >= MAX_ROUND);

                    // 搜索：（拆除+）建塔/升级
                    Op_generator build_gen(game_info, pid, avail_money);
                    build_gen.sell.tweaking = true;
                    if (game_info.round <= 493 || !no_ls) {
                        build_gen.build.lv3_options.clear();
                        build_gen.upgrade.max_count = 1;
                    }
                    build_gen.generate_operations();

                    for (const Defense_operation& op_list : build_gen.ops) {
                        if (game_info.round + op_list.round_needed >= MAX_ROUND) continue;

                        std::optional<Pos> build_pos;
                        for (const Task& t : op_list.ops) if (t.op.type == OperationType::BuildTower) build_pos = {t.op.arg0, t.op.arg1};
                        assert(!build_pos || is_highland(pid, build_pos.value().x, build_pos.value().y));

                        if (game_info.round <= 493 && op_list.cost > 60) continue;
                        if (game_info.round > 493 && op_list.cost > 120 && !no_ls) continue;

                        Operation_list opl({}, -1, op_list.loss, op_list.cost, !pid);
                        opl.ops = op_list.ops;
                        opl.evaluate(sim_round, best_result.res.first_succ);

                        // 判定修建后是否“在任何时刻都能放出LS”
                        if (opl.res.first_succ > EMP_COVER_PENALTY) {
                            int min_avail = min_avail_money_under_EMP(game_info, op_list);
                            if (min_avail < 150) opl.max_f_succ = EMP_COVER_PENALTY + 5 * (double(min_avail) / 150);
                        }

                        if (!opl.res.early_stop && opl > raw_result) logger.err((build_pos ? "p_bud: " : "p_upd: ") + opl.defence_str());
                        if (opl > best_result) best_result = opl;
                    }
                }
                // reflect
                if (best_result.res.first_succ == 0 && EMP_active && avail_value[!pid] <= 100) reflect_limit = 50;
                else reflect_limit--;
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

            // 实施搜索结果
            if (best_result.ops.size()) {
                logger.err("best: " + best_result.defence_str());
                if (peace_check) {
                    if (enemy_base_level) peace_check_cd = 30;
                    else peace_check_cd = 20;
                }

                bool occupying_LS = EMP_active && best_result.res.first_succ <= raw_f_succ + 20
                    && (avail_money + tower_value[pid] > 150 && avail_money + tower_value[pid] - best_result.cost <= 150)
                    && !std::any_of(best_result.ops.begin(), best_result.ops.end(), [](const Task& sc){return sc.op.type == UseLightningStorm;});
                if (occupying_LS) logger.err("[Occupying LS, result not taken]");
                else append_task_list(game_info, best_result.ops);
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
            if (game_info.super_weapon_cd[pid][EVA] <= 0 && last_atk > 5 && avail_value[pid] >= 150) if (raw_f_succ >= 30) {
                Op_generator EVA_gen(game_info, pid, avail_money);
                EVA_gen << Sell_cfg{3, 3} << Build_cfg{false} << Upgrade_cfg{0} << EVA_cfg{true};
                EVA_gen.generate_operations();

                // “模拟对方防守”的结果与我方如何sell塔无关，所以可以解耦出来
                Pos last_EVA_pos = {-1, -1};
                bool defended = false;
                bool old_defended = false;

                for (const Defense_operation& EVA_list : EVA_gen.ops) {
                    if (Simulator::round_count - atk_start_time > 160000) {// 硬卡时间
                        logger.err("[w] EVA search time out");
                        break;
                    }
                    if (game_info.round + EVA_list.round_needed >= MAX_ROUND) continue;

                    Operation_list opl({}, -1, EVA_list.loss, EVA_list.cost);
                    opl.ops = EVA_list.ops;
                    opl.evaluate(20); // 用于判定拆完塔之后是不是安全的

                    // 进攻效果判据
                    bool old_cond = hp_draw && (opl.res.old_opp > EVA_raw.res.old_opp);
                    if (opl.res.dmg_dealt <= EVA_raw.res.dmg_dealt && !old_cond) continue;

                    // 防守要求判据
                    if (opl.res.first_succ < MAX_ROUND) continue;

                    // 部分经济要求判据
                    int min_avail = min_avail_money_under_EMP(game_info, EVA_list) * (game_info.super_weapon_cd[pid][SuperWeaponType::LightningStorm] <= 0);
                    if (!(EVA_economy_crit || min_avail >= 160)) continue;

                    // 假如该位置的EVA还没模拟过，则模拟对方防守
                    Pos curr_EVA_pos = {EVA_list.ops.back().op.arg0, EVA_list.ops.back().op.arg1};
                    if (last_EVA_pos != curr_EVA_pos) {
                        last_EVA_pos = curr_EVA_pos;

                        Simulator raw_sim(game_info, pid, pid);
                        raw_sim.step_simulation(EVA_list.round_needed);
                        raw_sim.task_list[pid].emplace_back(EVA_list.ops.back()); // 对对方而言，我方是否Sell塔并不是很重要
                        raw_sim.info.coins[pid] = 999; // 所以作点弊也没关系...
                        raw_sim.step_to_next_player();

                        Op_generator generator(raw_sim.info, !pid);
                        generator.generate_operations();

                        defended = false;
                        old_defended = false;
                        for (const Defense_operation& op_list : generator.ops) {
                            Simulator atk_sim(raw_sim.info, !pid, pid);
                            atk_sim.task_list[!pid] = op_list.ops;
                            Sim_result res = atk_sim.simulate(EVA_SIM_ROUND, EVA_SIM_ROUND);

                            if (res.old_opp < opl.res.old_opp) old_defended = true;
                            if (res.first_succ > EVA_SIM_ROUND || res.succ_ant < EVA_raw.res.dmg_dealt) {
                                defended = true;
                                // logger.err("%s solved by %s", opl.attack_str().c_str(), op_list.str().c_str());
                                break;
                            }
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
                logger.err("raw best_EVA: %s (scanned %d)", best_EVA.attack_str().c_str(), EVA_gen.ops.size());

                bool fast_EVA_trigger = (best_EVA.res.dmg_time <= 5);
                if (best_EVA.res.dmg_dealt > EVA_raw.res.dmg_dealt && fast_EVA_trigger) {
                    // refine一下
                    logger.err("EVA refining:");
                    bool local_best = true;
                    Operation_list best_refine(best_EVA);
                    for (int i = 1; i <= EVA_REFINE_ROUND && local_best; i++) {
                        best_refine.ops.back().round = i;
                        best_refine.evaluate(EVA_SIM_ROUND);
                        best_refine.res.dmg_time -= i; // 对模拟i回合的补偿
                        logger.err("EVA refine: %s", best_refine.attack_str().c_str());
                        if (best_refine.attack_better_than(best_EVA, consider_old)) local_best = false;
                    }

                    if (local_best) {
                        logger.err("Conduct EVA attack " + best_EVA.attack_str());
                        append_task_list(game_info, best_EVA.ops);
                        avail_money -= best_EVA.cost;
                        last_attack_round = game_info.round;
                        last_atk = 0;
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
            if (game_info.super_weapon_cd[pid][EB] <= 0 && last_atk > 5 && avail_value[pid] >= 210) if (raw_f_succ >= 40 || reflect_tag) {
                Op_generator EMP_gen(game_info, pid, avail_money);
                EMP_gen << Sell_cfg{2, 3} << Build_cfg{false} << Upgrade_cfg{0} << EMP_cfg{true};
                EMP_gen.generate_operations();

                // “模拟对方防守”的结果与我方如何sell塔无关，所以可以解耦出来
                Pos last_EMP_pos = {-1, -1};
                bool ls_defended = false;
                bool build_defended = false;
                bool old_defended = false;

                for (const Defense_operation& EMP_list : EMP_gen.ops) {
                    if (Simulator::round_count - atk_start_time > 170000) {// 硬卡时间
                        logger.err("[w] EMP search time out");
                        break;
                    }
                    if (game_info.round + EMP_list.round_needed >= MAX_ROUND) continue;

                    Operation_list opl({}, -1, EMP_list.loss, EMP_list.cost);
                    opl.ops = EMP_list.ops;
                    opl.evaluate(20); // 用于判定拆完塔之后是不是安全的

                    // 进攻效果判据
                    bool dmg_cond = opl.res.dmg_dealt > EMP_raw.res.dmg_dealt && opl.res.dmg_dealt > 2;
                    bool old_cond = opl.res.old_opp > EMP_raw.res.old_opp;
                    if (!reflect_tag && !dmg_cond && !old_cond) continue;

                    // 防守要求判据
                    if (opl.res.first_succ < MAX_ROUND) continue;

                    // 部分经济要求判据
                    int min_avail = min_avail_money_under_EMP(game_info, {opl.ops}) * (game_info.super_weapon_cd[pid][SuperWeaponType::LightningStorm] <= 0);
                    if (!(EMP_economy_crit || min_avail >= 160)) continue;

                    // 假如该位置的EMP还没模拟过，则模拟对方防守
                    Pos curr_EMP_pos = {EMP_list.ops.back().op.arg0, EMP_list.ops.back().op.arg1};
                    if (last_EMP_pos != curr_EMP_pos) {
                        last_EMP_pos = curr_EMP_pos;

                        Simulator raw_sim(game_info, pid, pid);
                        raw_sim.step_simulation(EMP_list.round_needed);
                        raw_sim.task_list[pid].emplace_back(EMP_list.ops.back()); // 对对方而言，我方是否Sell塔并不是很重要
                        raw_sim.info.coins[pid] = 999; // 所以作点弊也没关系...
                        raw_sim.step_to_next_player();

                        Op_generator generator(raw_sim.info, !pid);
                        generator << LS_cfg{true};
                        generator.generate_operations();

                        ls_defended = false;
                        build_defended = false;
                        old_defended = false;
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
                logger.err("raw best_EMP: " + best_EMP.attack_str());

                bool unsolved_trigger = (best_EMP.res.dmg_dealt > 100);
                bool force_ls_trigger = (avail_value[pid] - avail_value[!pid] >= 150);
                force_ls_trigger |= (avail_money >= 250 && avail_value[pid] >= 300);
                force_ls_trigger |= game_info.round > 450;
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
                        last_attack_round = game_info.round;

                        if (reflect_tag) reflecting_EMP_countdown = 0;
                    }
                }
            }


            // 随缘升基地
            // 事实证明，打开这个功能则打eve/MoebiusMeow战绩很好，关闭这个功能则打omegafantasy战绩很好
            int base_level = game_info.bases[pid].ant_level;
            bool draw_cond = (game_info.bases[pid].hp <= game_info.bases[!pid].hp) && (ants_killed[pid] <= ants_killed[!pid]);
            bool money_cond = (avail_money >= 200 + 50 * base_level) && (avail_value[pid] >= 300 + 50 * base_level) && (base_level < 2);
            if (money_cond) money_cond &= (min_avail_money_under_EMP(game_info, {{Task(Operation(UpgradeGeneratedAnt))}}) >= 150);
            if (game_info.round < 480 && draw_cond && money_cond && !ops.size()) {
                logger.err("[Upgrading base]");
                ops.emplace_back(UpgradeGeneratedAnt);
                avail_money -= LEVEL2_BASE_UPGRADE_PRICE;
            }


            // 末回合进行LS
            Operation_list final_LS_raw({}, sim_round), best_final_LS(final_LS_raw);
            constexpr SuperWeaponType LS(SuperWeaponType::LightningStorm);
            constexpr int LS_cost = SUPER_WEAPON_INFO[LS][3];
            if (hp_draw && game_info.round >= 505 && game_info.super_weapon_cd[pid][LS] <= 0) {
                Op_generator gen(game_info, pid, avail_money);
                gen << Sell_cfg{3, 3} << Build_cfg{false} << Upgrade_cfg{0} << LS_cfg{true};
                gen.generate_operations();

                for (const Defense_operation& op_list : gen.ops) {
                    if (game_info.round + op_list.round_needed >= MAX_ROUND) continue;

                    Operation_list opl({}, -1, op_list.loss, op_list.cost, !pid);
                    opl.ops = op_list.ops;
                    opl.evaluate(sim_round);

                    if (opl > final_LS_raw) logger.err("Terminal LS:   " + opl.defence_str());
                    if (opl > best_final_LS) best_final_LS = opl;
                }

                bool better_cond = !best_final_LS.res.succ_ant && (best_final_LS > final_LS_raw);
                bool solved_cond = better_cond && !best_final_LS.res.old_ant;
                bool round_cond = better_cond && (game_info.round >= 508);
                if (solved_cond || round_cond) {
                    logger.err("Conduct terminal LS %s %s", best_final_LS.defence_str().c_str(), best_final_LS.attack_str().c_str());
                    append_task_list(game_info, best_final_LS.ops);
                }
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
        void append_task_list(const GameInfo& game_info, const std::vector<Task>& task_list) {
            for (const Task& task : task_list) {
                if (task.round == 0) {
                    if (!game_info.is_operation_valid(pid, task.op)) logger.err("[w] Discard invalid operation %s", task.op.str(true).c_str());
                    else {
                        ops.push_back(task.op);
                        avail_money += game_info.get_operation_income(pid, task.op);
                    }
                } else {
                    Task temp = task;
                    temp.round += game_info.round;
                    schedule_queue.push(temp);
                    logger.err("Operation scheduled at round %3d: %s", temp.round, task.op.str(true).c_str());
                }
            }
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