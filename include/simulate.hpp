#pragma once

#include "game_info.hpp"

// 模拟结果类
struct Sim_result {
    int succ_ant; // 模拟过程中我方掉的血
    int ant_killed; // 被我方击杀的蚂蚁数
    int first_succ; // 模拟过程中我方第一次掉血的回合数（相对时间）
    int danger_encounter; // “危险抵近”（即容易被套盾发起攻击）的蚂蚁数
    int first_enc; // “第一次危险抵近”的回合数（相对时间）

    int dmg_dealt; // 模拟过程中对方掉的血
    int dmg_time; // 模拟过程中对方第一次掉血的回合数（相对时间）

    bool early_stop; // 这一模拟结果是否是提前停止而得出的

    constexpr Sim_result() : succ_ant(99), ant_killed(99), first_succ(0), danger_encounter(99), first_enc(0), dmg_dealt(0), dmg_time(MAX_ROUND + 1), early_stop(false) {}
};

// 模拟器类
class Simulator {
public:
    static int sim_count;
    static int round_count;

    const int pid;
    GameInfo info;                          // Game state
    std::vector<Operation> operations[2];   // Players' operations which are about to be applied to current game state.
    std::vector<Task> task_list[2];

    bool verbose = 0;
    int ants_killed[2] = {0, 0};

    static constexpr int INIT_HEALTH = 49;
    /**
     * @brief 构造一个新的Simulator对象
     * @param curr_info 初始局面，模拟将自此局面开始
     * @param pid 模拟的“立场”，也即返回的Sim_result中的“我方”玩家编号
     * @param atk_side 本次模拟所关注的“进攻方”，只有进攻方的蚂蚁以及“防守方”的塔会被模拟。默认为两方都模拟
     */
    explicit Simulator(const GameInfo& curr_info, int pid, int atk_side = -1) : info(curr_info), pid(pid) {
        sim_count++;
        for (int i = 0; i < 2; i++) info.bases[i].hp = INIT_HEALTH;
        if (atk_side != -1) set_side(atk_side);
    }

    /**
     * @brief 进入“单边模拟模式”并设置“进攻方”，在“单边模拟模式”中，只有进攻方的蚂蚁以及“防守方”的塔会被模拟
     * @param side 进攻方的编号
     */
    void set_side(int side) {
        one_side = true;
        attack_side = side;
        for (auto it = info.ants.begin(); it != info.ants.end(); ) {
            if (it->player != attack_side) it = info.ants.erase(it);
            else ++it;
        }
    }

    static constexpr int DANGER_RANGE = 4;
    Sim_result simulate(int round, int stopping_f_succ) {
        Sim_result res;
        res.first_succ = res.dmg_time = res.first_enc = MAX_ROUND + 1;

        std::vector<int> enc_ant_id;
        for (int i = 0; i < 2; i++) std::sort(task_list[i].begin(), task_list[i].end(), __cmp_downgrade_last); // 将降级操作排到最后(因为操作从最后开始加)

        for (int _r = 0; _r < round; ++_r) {
            round_count++;
            if (pid == 0) {
                // Add player0's operation
                __add_op(_r, 0);
                // Apply player0's operation
                apply_operations_of_player(0);
                // Add player1's operation
                __add_op(_r, 1);
                // Apply player1's operation
                apply_operations_of_player(1);
                // Next round
                if (!next_round()) break;
            } else {
                // Add player1's operation
                __add_op(_r, 1);
                // Apply player1's operation
                apply_operations_of_player(1);
                // Next round
                if (!next_round()) break;
                // Add player0's operation
                __add_op(_r, 0);
                // Apply player0's operation
                apply_operations_of_player(0);
            }
            if (res.first_succ > MAX_ROUND) for (const Ant& a : info.ants) {
                if (a.player == pid || distance(a.x, a.y, Base::POSITION[pid][0], Base::POSITION[pid][1]) > DANGER_RANGE) continue;
                if (!std::count(enc_ant_id.begin(), enc_ant_id.end(), a.id)) {
                    enc_ant_id.push_back(a.id);
                    if (res.first_enc > MAX_ROUND) res.first_enc = _r;
                }
            }
            if (res.first_succ > MAX_ROUND && INIT_HEALTH != info.bases[pid].hp) res.first_succ = _r;
            if (res.dmg_time > MAX_ROUND && INIT_HEALTH != info.bases[!pid].hp) res.dmg_time = _r;

            if (res.first_succ < stopping_f_succ) { // “挂了就停止”仍然可以考虑
                res.early_stop = true;
                break;
            }
        }

        res.ant_killed = ants_killed[pid];
        res.danger_encounter = enc_ant_id.size();
        res.succ_ant = INIT_HEALTH - info.bases[pid].hp;
        res.dmg_dealt = INIT_HEALTH - info.bases[!pid].hp;
        return res;
    }

private:
    bool one_side = false;
    int attack_side = -1;
    // 将降级操作排到最后(因为操作从最后开始加)
    static bool __cmp_downgrade_last(const Task& a, const Task& b) {
        return a.op.type != DowngradeTower && b.op.type == DowngradeTower;
    }
    void __add_op(int _r, int player) {
        std::vector<Task>& tasks = task_list[player];
        for (int i = tasks.size()-1; i >= 0; i--) {
            const Task& curr_task = tasks[i];
            if (curr_task.round == _r) {
                if (!info.is_operation_valid(player, operations[player], curr_task.op))
                    fprintf(stderr, "[w] Adding invalid operation for player %d at sim round %d: %s\n", player, _r, curr_task.op.str(true).c_str());
                else operations[player].push_back(curr_task.op);
                tasks.erase(tasks.begin() + i);
            }
        }
    }

    /* Round settlement process */
    /**
     * @brief Lightning storms snd towers try attacking ants.
     * 
     * @note A tower may not attack if it has not cooled down (i.e. CD > 0) or if no target is available.
     * Even if it is able to attack, a tower may not cause any damage due to item effects.
     * 
     * @note The state of an ant may be changed. Set AntState::Fail if an ant has negative health points(hp).
     * Set AntState::Frozen if an ant is attacked by a tower of type TowerType::Ice.
     * 
     * @see #AntState for more information on the life cycle of an ant.
     */
    void attack_ants() {
        /* Lightning Storm Attack */
        for (const SuperWeapon& sw: info.super_weapons) {
            if(sw.type != SuperWeaponType::LightningStorm) continue;
            for (Ant &ant : info.ants) {
                if (sw.is_in_range(ant.x, ant.y) && ant.player != sw.player) {
                    ant.hp = 0;
                    ant.state = AntState::Fail;
                    info.update_coin(sw.player, ant.reward());
                }
            }
        }

        /* Tower Attack */
        // Set deflector property
        for (Ant& ant: info.ants) ant.deflector = info.is_shielded_by_deflector(ant);
        // Attack
        for (Tower& tower: info.towers) {
            if (one_side && tower.player == attack_side) continue; // 不模拟进攻方的塔
            // Skip if shielded by EMP
            if (info.is_shielded_by_emp(tower)) continue;
            // Try to attack
            auto targets = tower.attack(info.ants, verbose);
            // Get coins if tower killed the target
            for (int idx: targets) if (info.ants[idx].state == AntState::Fail) info.update_coin(tower.player, info.ants[idx].reward());
            // Reset tower's damage (clear buff effect)
            tower.damage = TOWER_INFO[tower.type].attack;
        }
        // Reset deflector property
        for (Ant& ant: info.ants) ant.deflector = false;
    }
    /**
     * @brief Make alive ants move according to pheromone, without modifying pheromone. 
     * 
     * @note The state of an ant may be changed. Set AntState::TooOld if an ant reaches age limit.
     * Set AntState::Success if an ant has reached opponent's base, then update the base's health points (hp).   
     * 
     * @return Current game state (running / ended for one side's hp <= 0).
     * 
     * @see #AntState for more information on the life cycle of an ant.
     */
    void move_ants() {
        for (Ant& ant: info.ants) {
            // Update age regardless of the state
            ant.age++;
            // 1) No other action for dead ants
            if (ant.state == AntState::Fail) continue;
            // 2) Check if too old
            if (ant.age > Ant::AGE_LIMIT) ant.state = AntState::TooOld;
            // 3) Move if possible (alive)
            if (ant.state == AntState::Alive) ant.move(info.next_move(ant));
            // 4) Check if success (Mark success even if it reaches the age limit)
            if (ant.x == Base::POSITION[!ant.player][0] && ant.y == Base::POSITION[!ant.player][1]) {
                ant.state = AntState::Success;
                info.update_base_hp(!ant.player, -1);
                info.update_coin(ant.player, 5);
                // 不考虑基地血量降至0
            }
            // 5) Unfreeze if frozen
            if (ant.state == AntState::Frozen) ant.state = AntState::Alive;
        }
    }

    /**
     * @brief Bases try generating new ants.
     * @note Generation may not happen if it is not the right time (i.e. round % cycle == 0).
     */
    void generate_ants() {
        for (auto& base: info.bases) {
            if (one_side && base.player != attack_side) continue; // 不为防守方生成蚂蚁
            auto ant = base.generate_ant(info.next_ant_id, info.round);
            if (ant)  {
                info.ants.push_back(std::move(ant.value()));
                info.next_ant_id++;
            }
        }
    }

public:
    /**
     * @brief Apply all operations in "operations[player_id]" to current state.
     * @param player_id The player.
     */
    void apply_operations_of_player(int player_id) {
        // 1) count down long-lasting weapons' left-time
        info.count_down_super_weapons_left_time(player_id);
        // 2) apply opponent's operations
        for (auto& op: operations[player_id]) info.apply_operation(player_id, op);
    }

    /**
     * @brief Update game state at the end of current round.
     * This function is called after both players have applied their operations.
     * @return bool Whether the game is still running.
     */
    bool next_round() {
        // 1) Judge winner at MAX_ROUND
        if (info.round == MAX_ROUND) return false;
        // 2) Towers attack ants
        attack_ants();
        // 3) Ants move
        move_ants();
        // 4) Update pheromone
        if (one_side) info.global_pheromone_attenuation(attack_side); // 仅模拟进攻方的信息素
        else info.global_pheromone_attenuation();
        info.update_pheromone_for_ants(); // 正常update信息素，因为防御方不会出蚂蚁
        // 5) Clear dead and succeeded ants
        for (int i = 0; i < 2; i++) ants_killed[!i] = std::count_if(info.ants.begin(), info.ants.end(), [i](const Ant& a){ return a.state == AntState::Fail && a.player == i; });
        info.clear_dead_and_succeeded_ants();
        // 6) Barracks generate new ants
        generate_ants();
        // 7) Get basic income
        info.coins[0] += BASIC_INCOME;
        info.coins[1] += BASIC_INCOME;
        // 8) Start next round
        info.round++;
        // 9) Count down super weapons' cd
        info.count_down_super_weapons_cd();
        // 10) Clear operations
        operations[0].clear();
        operations[1].clear();

        return true;
    }
};
int Simulator::sim_count = 0;
int Simulator::round_count = 0;
