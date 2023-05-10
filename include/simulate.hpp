/**
 * @file simulate.hpp
 * @author Jingxuan Liu, Yufei li
 * @brief An integrated module for game simulation.
 * @date 2023-04-01
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#pragma once

#include "game_info.hpp"

/**
 * @brief An integrated module for simulation with simple interfaces for your convenience.
 * Built from the game state of a Controller instance, a Simulator object allows you to
 * simulate the whole game and "predict" the future for decision making.
 */
class Simulator {
public:
    GameInfo info;                          ///< Game state
    std::vector<Operation> operations[2];   ///< Players' operations which are about to be applied to current game state. 

    bool verbose = 0;
    int ants_killed[2] = {0, 0};

    bool one_side = false;
    int attack_side = -1;

    /**
     * @brief Construct a new Simulator object from a GameInfo instance. Current game state will be copied.
     * @param info The GameInfo instance as data source.
     */
    Simulator(const GameInfo& info) : info(info) {}

    void set_side(int side) {
        one_side = true;
        attack_side = side;
    }

private:
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
     *  @brief Try adding an operation to "operations[player_id]". The operation has been constructed elsewhere.
     *         This function will check validness of the operation and add it to "operations[player_id]" if valid.  
     *  @param player_id The player.
     *  @param op The operation to be added.
     *  @return Whether the operation is added successfully.
     */
    bool add_operation_of_player(int player_id, Operation op) {
        if (info.is_operation_valid(player_id, operations[player_id], op)) {
            operations[player_id].push_back(op);
            return true;
        }
        return false;
    }

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