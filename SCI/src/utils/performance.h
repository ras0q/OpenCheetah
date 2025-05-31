#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include <chrono>
#include <ctime>
#include <iostream>
#include <stack>
#include <string>
#include <utility>

using std::pair;
using std::stack;
using std::string;
using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;

inline double time_log(string tag) {
    static stack<pair<string, high_resolution_clock::time_point>> sentinel;

    if (sentinel.empty() || sentinel.top().first != tag) {
        auto start = high_resolution_clock::now();
        sentinel.push(make_pair(tag, start));
        return 0.0;
    }

    else {
        auto start = sentinel.top().second;
        auto end = high_resolution_clock::now();
        double duration =
            duration_cast<milliseconds>(end - start).count() * 1.0;
        std::cout << "[Time] " << tag << ": " << duration << " ms" << std::endl;
        sentinel.pop();
        return duration;
    }
}

#endif  // PERFORMANCE_H
