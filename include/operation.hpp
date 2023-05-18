#pragma once

#include "game_info.hpp"
#include "simulate.hpp"

extern const GameInfo* info;
extern int pid;

// 动作序列类，模拟及比较功能将于日后分离出去
class Operation_list {
    public:
    std::vector<Task> ops;
    int loss; // 行动序列的“损失函数”，注意这往往不是真实花费
    int cost; // 行动序列的开销（指coin）

    int atk_side;
    Sim_result res;
    double max_f_succ = 1e7;

    explicit Operation_list(const std::vector<Operation>& _ops, int eval_round = -1, int _loss = 0, int _cost = 0, int _atk_side = -1)
    : loss(_loss), cost(_cost), atk_side(_atk_side) {
        for (const Operation& op : _ops) append(op);
        if (eval_round >= 0) evaluate(eval_round);
    };
    void append(const Operation& op, int _round = 0) {
        ops.emplace_back(op, _round);
    }
    void append(const std::optional<Operation>& op, int _round = 0) {
        if (op) append(op.value(), _round);
    }

    const Sim_result& evaluate(int _round, int stopping_f_succ = -1) {
        Simulator sim(*info, pid, atk_side);
        sim.task_list[pid] = ops;
        res = sim.simulate(_round, stopping_f_succ);
        return res;
    }

    std::string defence_str() const {
        std::string ret;
        double real_first_time = real_f_succ();

        ret += str_wrap("[S/E/O: %d/%d/%d, S/E/O: %3d/%d/%d", res.succ_ant, res.danger_encounter, res.old_ant,
            user_friendly_int(res.first_succ), user_friendly_int(res.first_enc), user_friendly_int(res.next_old));
        if (real_first_time != res.first_succ) ret += str_wrap("(r%.1lf)", real_first_time);
        ret += str_wrap(", c/l: %d/%d] [", cost, loss);

        for (const Task& task : ops) ret += task.op.str() + ' ';
        if (ops.size()) ret.pop_back();
        return ret + ']';
    }
    std::string attack_str() const {
        std::string ret = str_wrap("[dmg/old: %d/%d, f/o: %2d/%d, l: %3d] [", res.dmg_dealt, res.old_opp, res.dmg_time, res.next_old_opp, loss);
        for (const Task& task : ops) {
            ret += task.op.str();
            if (task.round) ret += str_wrap("(+%d)", task.round);
            ret.push_back(' ');
        }
        if (ops.size()) ret.pop_back();
        return ret + ']';
    }

    double real_f_succ() const {
        double ans = std::min((double)res.first_succ, max_f_succ);
        if (ans <= 40) return ans;
        return 40 + (ans - 40) / (res.danger_encounter + res.old_ant + 1);
    }
    /**
     * @brief 比较当前行动序列与另一行动序列在“防守”方面的表现
     * @param other 要比较的另一行动序列
     * @return bool 当前行动序列在“防守”方面是否优于other
     */
    bool operator>(const Operation_list& other) const {
        if (real_f_succ() != other.real_f_succ()) return real_f_succ() > other.real_f_succ();
        return loss + res.old_ant * 10 + res.succ_ant * 50 < other.loss + other.res.old_ant * 10 + other.res.succ_ant * 50;
    }
    /**
     * @brief 比较当前行动序列与另一行动序列在“进攻”方面的表现
     * @note 比较优先级：对敌方造成的伤害、行动开销
     * @param other 要比较的另一行动序列
     * @param is_old_significant 是否考虑对击杀数的影响
     * @return bool 当前行动序列在“进攻”方面是否优于other
     */
    bool attack_better_than(const Operation_list& other, bool is_old_significant) const {
        int s_score = res.dmg_dealt - res.dmg_time + (res.dmg_time <= 4);
        int o_score = other.res.dmg_dealt - other.res.dmg_time + (res.dmg_time <= 4);
        if (is_old_significant) {
            s_score += res.old_opp - res.next_old_opp + (res.next_old_opp <= 4);
            o_score += other.res.old_opp - other.res.next_old_opp + (other.res.next_old_opp <= 4);
        }
        if (s_score != o_score) return s_score > o_score;
        return loss < other.loss;
    }

    private:
    static int user_friendly_int(int a) {
        if (a > MAX_ROUND) return -1;
        return a;
    }
};

// “卖出操作”的配置类
struct Sell_cfg {
    int max_step; // “卖出序列”中的最大动作数。若希望关闭卖出操作，可将此值设为0
    int max_level; // 允许降级的最高塔等级
    bool tweaking = false; // 是否进入“微调模式”，此模式下会尝试各种卖出操作
};
// “新建操作”的配置类
struct Build_cfg {
    bool available; // 是否允许新建
    std::vector<TowerType> lv2_options; // 允许新建的二级塔
    std::vector<TowerType> lv3_options; // 允许新建的三级塔
};
// “升级操作”的配置类
struct Upgrade_cfg {
    int max_count; // 最大升级次数。若希望关闭升级操作，可将此值设为0
    std::vector<TowerType> lv2_options; // 允许升级到的二级塔
    std::vector<TowerType> lv3_options; // 允许升级到的三级塔
};
// 闪电风暴配置类
struct LS_cfg {
    bool available; // 是否允许使用闪电风暴
};

// “卖出动作序列”
class Sell_operation {
    public:
        std::vector<Task> ops; // 该序列中的一系列downgrade操作
        int round_needed = 0; // 完成所有操作需要的回合数，至多为2
        int earn = 0; // 最终获得的钱数
        int destroy = 0; // 彻底拆除的塔数

        std::string str() const {
            std::string ans = str_wrap("c:%3d r:%d d:%d [", earn, round_needed, destroy);
            for (const Task& task : ops) ans += task.op.str() + ' ';
            if (ops.size()) ans.pop_back();
            return ans + ']';
        }

        // 把“动静最小”的操作排到前面
        bool operator<(const Sell_operation& other) const {
            if (earn != other.earn) return earn < other.earn;
            if (round_needed != other.round_needed) return round_needed < other.round_needed;
            return ops.size() < other.ops.size();
        }
        // 用于给定金额需求时的二分查找
        bool operator<(int need) const {
            return earn < need;
        }
};
// “防御动作序列”，日后将会代替Operation_list
class Defense_operation {
    public:
        std::vector<Task> ops; // 该序列中的系列操作
        int round_needed = 0; // 完成所有操作需要的回合数
        int loss = 0;
        int cost = 0; // 花费的钱数

        // 清空整个序列并重置开销信息
        void clear() {
            ops.clear();
            round_needed = loss = cost = 0;
        }
        // 将整个动作序列后移round回合
        void suspend(int round) {
            round_needed += round;
            for (Task& t : ops) t.round += round;
        }
        // 获取此序列中是否含有LS动作
        bool has_ls() const {
            return std::count_if(ops.begin(), ops.end(), [](const Task& t){return t.op.type == OperationType::UseLightningStorm;});
        }
        std::string str() const {
            std::string ans = str_wrap("[c:%3d l:%3d r:%d] [", cost, loss, round_needed);
            for (const Task& task : ops) ans += task.op.str() + ' ';
            if (ops.size()) ans.pop_back();
            return ans + ']';
        }

        // 简单地拼接两个Defense_operation
        Defense_operation& operator+=(const Defense_operation& other) {
            loss += other.loss;
            cost += other.cost;
            ops.insert(ops.end(), other.ops.begin(), other.ops.end());
            round_needed = std::max(round_needed, other.round_needed);
            return *this;
        }
        // 简单地拼接两个Defense_operation，返回新的Defense_operation对象
        Defense_operation operator+(const Defense_operation& other) const {
            Defense_operation ret(*this);
            return ret += other;
        }
        // （在前面）拼接一个Sell_operation，理论上仅应用于generate_operations()函数
        void concat_sell(const Sell_operation& other, double loss_mult = 1) {
            loss += other.earn * loss_mult;
            cost -= other.earn;
            ops.insert(ops.begin(), other.ops.begin(), other.ops.end());
            round_needed = std::max(round_needed, other.round_needed);
        }
};

// （专门用于防御的）“动作序列生成器”
class Op_generator {
    public:
        int pid;
        int cash;
        GameInfo info;

        Sell_cfg sell = {2, 3};
        Build_cfg build = {true, {TowerType::Quick, TowerType::Mortar, TowerType::Heavy}, {TowerType::Double, TowerType::Sniper, TowerType::MortarPlus, TowerType::Cannon}};
        Upgrade_cfg upgrade = {2, {TowerType::Quick, TowerType::Mortar, TowerType::Heavy}, {TowerType::Double, TowerType::MortarPlus}};
        LS_cfg ls_cfg = {false};

        explicit Op_generator(const GameInfo& info, int pid, int _cash = -1) : info(info), pid(pid), cash(_cash) {
            if (cash == -1) cash = info.coins[pid];
        }

        bool sell_list_generated = false;
        std::vector<Sell_operation> sell_list;
        // 生成“卖出列表”
        void generate_sell_list() {
            if (sell_list_generated) return;
            sell_list_generated = true;

            temp_sell = {};
            tower_count = info.tower_num_of_player(pid);

            sell_list.clear();
            sell_list.push_back(temp_sell); // 放一个空的，表示不需要拆除的情形
            for (int i = 0; i < info.towers.size(); i++) if (info.towers[i].player == pid && !info.is_shielded_by_emp(info.towers[i])) sell_recur(i, sell.max_step, 0);

            std::sort(sell_list.begin(), sell_list.end());
        }

        std::vector<Defense_operation> ops;
        std::vector<Defense_operation> build_list;
        std::vector<Defense_operation> upgrade_list;
        void generate_operations() {
            // 初始化
            ops.clear();
            generate_sell_list();
            tower_count = info.tower_num_of_player(pid);

            // 解决build子问题
            build_list.clear();
            if (build.available) for (const Pos& p : highlands[pid]) {
                if (info.tower_at(p.x, p.y) || info.is_shielded_by_emp(pid, p.x, p.y)) continue; // 已经建了塔的地方就不必再建了
                // 1级
                temp_build.clear();
                temp_build.ops.emplace_back(build_op(p));
                temp_build.loss = BUILD_COST[tower_count+1] / BUILD_LOSS_DIV;
                temp_build.cost = BUILD_COST[tower_count+1];
                build_list.push_back(temp_build);
                // 2级
                for (const TowerType& target : build.lv2_options) {
                    build_list.push_back(temp_build);
                    build_list.back().ops.emplace_back(upgrade_op(info.next_tower_id, target), 1);
                    build_list.back().loss += UPGRADE_COST[1] / BUILD_LOSS_DIV;
                    build_list.back().cost += UPGRADE_COST[1];
                    build_list.back().round_needed = 1;
                }
                // 3级
                for (const TowerType& target : build.lv3_options) {
                    TowerType lv2_target = TowerType((int)target / 10);
                    build_list.push_back(temp_build);
                    build_list.back().ops.emplace_back(upgrade_op(info.next_tower_id, lv2_target), 1);
                    build_list.back().ops.emplace_back(upgrade_op(info.next_tower_id, target), 2);
                    build_list.back().loss += (UPGRADE_COST[1] + UPGRADE_COST[2]) / BUILD_LOSS_DIV;
                    build_list.back().cost += UPGRADE_COST[1] + UPGRADE_COST[2];
                    build_list.back().round_needed = 2;
                }
            }
            // 与Sell部分进行合并
            if (sell.tweaking) build_list.emplace_back();
            for (const Defense_operation& bud : build_list) {
                int first_larger_earn = -1;
                for (const Sell_operation& curr_sell : sell_list) {
                    if (first_larger_earn < 0 && curr_sell.earn + cash >= bud.cost) first_larger_earn = curr_sell.earn;
                    if (first_larger_earn >= 0 && curr_sell.earn > first_larger_earn && !sell.tweaking) break;

                    // 计算由拆除引发的开销变化
                    int real_cost = bud.cost;
                    int real_loss = bud.loss;
                    if (curr_sell.destroy) {
                        real_cost = real_cost + BUILD_COST[tower_count+1-curr_sell.destroy] - BUILD_COST[tower_count+1];
                        real_loss = real_loss + (BUILD_COST[tower_count+1-curr_sell.destroy] - BUILD_COST[tower_count+1]) / BUILD_LOSS_DIV;
                    }
                    if (cash + curr_sell.earn < real_cost) continue;

                    // 将动作添加进列表中，此处假定是拆完了再开始建
                    ops.push_back(bud);
                    Defense_operation& curr = ops.back();
                    curr.cost = real_cost, curr.loss = real_loss;
                    curr.suspend(curr_sell.round_needed);
                    curr.concat_sell(curr_sell, sell.tweaking ? TWEAK_LOSS_MULT : 1);
                }
            }

            // 解决upgrade子问题
            upgrade_list.clear();
            temp_build.clear();
            for (int i = 0; i < info.towers.size(); i++)
                if (info.towers[i].player == pid && info.towers[i].level() < 3 && !info.is_shielded_by_emp(info.towers[i])) upgrade_recur(i, upgrade.max_count);
            // 与Sell部分进行合并
            for (const Defense_operation& upd : upgrade_list) {
                // 预处理涉及的塔编号
                std::vector<int> tower_ids;
                for (const Task& t : upd.ops) tower_ids.push_back(t.op.arg0);

                int first_larger_earn = -1;
                for (const Sell_operation& curr_sell : sell_list) {
                    if (cash + curr_sell.earn < upd.cost) continue;
                    else if (first_larger_earn < 0) first_larger_earn = curr_sell.earn;
                    if (first_larger_earn >= 0 && curr_sell.earn > first_larger_earn && !sell.tweaking) break;

                    // 检查塔编号是否冲突（不允许在动作序列中降级+升级同一个塔）
                    bool conflict = false;
                    for (int i = 0; i < curr_sell.ops.size() && !conflict; i++) conflict |= std::count(tower_ids.begin(), tower_ids.end(), curr_sell.ops[i].op.arg0);
                    if (conflict) continue;

                    // 将动作添加进列表中，此处假定是拆完了再开始升级
                    ops.push_back(upd);
                    Defense_operation& curr = ops.back();
                    curr.suspend(curr_sell.round_needed);
                    curr.concat_sell(curr_sell, sell.tweaking ? TWEAK_LOSS_MULT : 1);
                }
            }

            // LS部分
            if (ls_cfg.available && info.super_weapon_cd[pid][SuperWeaponType::LightningStorm] <= 0) {
                // 解决LS子问题
                Defense_operation ls;
                ls.loss = 450, ls.cost = 150;
                ls.ops.emplace_back(lightning_op({0, 0}));
                for (int x = 0; x < MAP_SIZE; x++) for (int y = 0; y < MAP_SIZE; y++) {
                    if (!is_valid_pos(x, y)) continue;
                    ls.ops.back().op = lightning_op({x, y});

                    // 与Sell部分进行合并
                    int first_larger_earn = -1;
                    for (const Sell_operation& curr_sell : sell_list) {
                        if (cash + curr_sell.earn < ls.cost) continue;
                        else if (first_larger_earn < 0) first_larger_earn = curr_sell.earn;
                        if (first_larger_earn >= 0 && curr_sell.earn > first_larger_earn) break;

                        // 将动作添加进列表中，此处假定是拆完了再放LS
                        ops.push_back(ls);
                        Defense_operation& curr = ops.back();
                        curr.suspend(curr_sell.round_needed);
                        curr.concat_sell(curr_sell);
                    }
                }
            }
        }

        // 一系列配置函数
        Op_generator& operator<<(const Sell_cfg& new_sell) {
            sell = new_sell;
            sell_list_generated = false;
            return *this;
        }
        Op_generator& operator<<(const Build_cfg& new_build) {
            build = new_build;
            return *this;
        }
        Op_generator& operator<<(const Upgrade_cfg& new_upgrade) {
            upgrade = new_upgrade;
            return *this;
        }
        Op_generator& operator<<(const LS_cfg& new_ls) {
            ls_cfg = new_ls;
            return *this;
        }

    private:
        int tower_count;
        Sell_operation temp_sell;
        void sell_recur(int tower_id, int step_remain, int destory_count) {
            if (step_remain <= 0) return;
            const Tower& t = info.towers[tower_id];
            int t_level = t.level();
            assert(t.player == pid);

            // 1次降级
            if (step_remain >= 1) {
                // 参数计算
                bool dest_1 = (t_level == 1);
                int earn_1 = dest_1 ? TOWER_REFUND[tower_count - destory_count] : DOWNGRADE_REFUND[t_level];
                temp_sell.ops.push_back(Task(Operation(DowngradeTower, t.id)));
                temp_sell.earn += earn_1;
                temp_sell.destroy += dest_1;
                // 递归
                sell_list.push_back(temp_sell);
                for (int i = tower_id+1; i < info.towers.size(); i++)
                    if (info.towers[i].player == pid && !info.is_shielded_by_emp(info.towers[i])) sell_recur(i, step_remain-1, destory_count+dest_1);
                temp_sell.earn -= earn_1;
                temp_sell.destroy -= dest_1;

                // 2次降级
                if (t_level >= 2 && sell.max_level >= 2 && step_remain >= 2) {
                    // 参数计算
                    bool dest_2 = (t_level == 2);
                    int old_round_2 = temp_sell.round_needed;
                    int earn_2 = ((int)dest_2 * TOWER_REFUND[tower_count - destory_count]) + DOWNGRADE_REFUND[t_level] + DOWNGRADE_REFUND[t_level-1];
                    temp_sell.ops.push_back(Task(Operation(DowngradeTower, t.id), 1));
                    temp_sell.earn += earn_2;
                    temp_sell.destroy += dest_2;
                    temp_sell.round_needed = std::max(1, temp_sell.round_needed);
                    // 递归
                    sell_list.push_back(temp_sell);
                    for (int i = tower_id+1; i < info.towers.size(); i++)
                        if (info.towers[i].player == pid && !info.is_shielded_by_emp(info.towers[i])) sell_recur(i, step_remain-2, destory_count+dest_2);
                    temp_sell.earn -= earn_2;
                    temp_sell.destroy -= dest_2;
                    temp_sell.round_needed = old_round_2;

                    // 3次降级
                    if (t_level == 3 && sell.max_level >= 3 && step_remain >= 3) {
                        // 参数计算
                        int old_round_3 = temp_sell.round_needed;
                        int earn_3 = TOWER_REFUND[tower_count - destory_count] + LEVEL_REFUND[3];
                        temp_sell.ops.push_back(Task(Operation(DowngradeTower, t.id), 2));
                        temp_sell.earn += earn_3;
                        temp_sell.destroy++;
                        temp_sell.round_needed = std::max(2, temp_sell.round_needed);
                        // 递归
                        sell_list.push_back(temp_sell);
                        for (int i = tower_id+1; i < info.towers.size(); i++)
                            if (info.towers[i].player == pid && !info.is_shielded_by_emp(info.towers[i])) sell_recur(i, step_remain-3, destory_count+1);
                        temp_sell.earn -= earn_3;
                        temp_sell.destroy--;
                        temp_sell.ops.pop_back();
                        temp_sell.round_needed = old_round_3;
                    }
                    temp_sell.ops.pop_back();
                }
                temp_sell.ops.pop_back();
            }
        }

        Defense_operation temp_build;
        void upgrade_recur(int tower_id, int step_remain) {
            if (step_remain <= 0) return;
            const Tower& t = info.towers[tower_id];
            int t_level = t.level();
            assert(t.player == pid && t_level < 3);

            if (t_level == 1) {
                // 1级塔升2级
                temp_build.cost += UPGRADE_COST[1];
                temp_build.loss += UPGRADE_COST[1] / BUILD_LOSS_DIV;
                for (const TowerType& target : upgrade.lv2_options) {
                    temp_build.ops.emplace_back(upgrade_op(t.id, target));
                    // 递归
                    upgrade_list.push_back(temp_build);
                    if (step_remain > 1) for (int i = tower_id+1; i < info.towers.size(); i++)
                        if (info.towers[i].player == pid && info.towers[i].level() < 3 && !info.is_shielded_by_emp(info.towers[i])) upgrade_recur(i, step_remain-1);
                    temp_build.ops.pop_back();
                }
                // 1级塔升3级
                int old_round_2 = temp_build.round_needed;
                temp_build.cost += UPGRADE_COST[2];
                temp_build.loss += UPGRADE_COST[2] / BUILD_LOSS_DIV;
                temp_build.round_needed = std::max(old_round_2, 1);
                if (step_remain >= 2) for (const TowerType& target : upgrade.lv3_options) {
                    TowerType lv2_target = TowerType((int)target / 10);
                    temp_build.ops.emplace_back(upgrade_op(t.id, lv2_target));
                    temp_build.ops.emplace_back(upgrade_op(t.id, target), 1);
                    // 递归
                    upgrade_list.push_back(temp_build);
                    if (step_remain > 2) for (int i = tower_id+1; i < info.towers.size(); i++)
                        if (info.towers[i].player == pid && info.towers[i].level() < 3 && !info.is_shielded_by_emp(info.towers[i])) upgrade_recur(i, step_remain-2);
                    temp_build.ops.pop_back();
                    temp_build.ops.pop_back();
                }
                temp_build.cost -= (UPGRADE_COST[1] + UPGRADE_COST[2]);
                temp_build.loss -= (UPGRADE_COST[1] + UPGRADE_COST[2]) / BUILD_LOSS_DIV;
                temp_build.round_needed = old_round_2;
            } else {
                // 2级塔升3级
                temp_build.cost += UPGRADE_COST[2];
                temp_build.loss += UPGRADE_COST[2] / BUILD_LOSS_DIV;
                for (const TowerType& target : upgrade.lv3_options) {
                    if ((int)target / 10 != t.type) continue;
                    temp_build.ops.emplace_back(upgrade_op(t.id, target));
                    // 递归
                    upgrade_list.push_back(temp_build);
                    if (step_remain > 1) for (int i = tower_id+1; i < info.towers.size(); i++)
                        if (info.towers[i].player == pid && info.towers[i].level() < 3 && !info.is_shielded_by_emp(info.towers[i])) upgrade_recur(i, step_remain-1);
                    temp_build.ops.pop_back();
                }
                temp_build.cost -= UPGRADE_COST[2];
                temp_build.loss -= UPGRADE_COST[2] / BUILD_LOSS_DIV;
            }

        }

        static constexpr Pos LIGHTNING_POS[2] {{3, 9}, {15, 9}};
        static constexpr int BUILD_LOSS_DIV = 5;
        static constexpr double TWEAK_LOSS_MULT = 0.2;
};
