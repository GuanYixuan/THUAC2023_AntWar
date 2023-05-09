#pragma once

#include "game_info.hpp"

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


