#pragma once
#include <chrono>

// TODO: move to https://en.cppreference.com/w/cpp/chrono
namespace chr = std::chrono;

// Store the current time, and tells when it is legal to run another operation.
struct Debouncer {
    // return if debounce is possible. If this returns `true`, the client is
    // expected to perform the action.
    // Prefer to use this API as follows:
    // 1) if (debouncer.shouldAct()) { /* perform action */ }
    // 2) if (!debouncer.shouldAct()) { return; } /* perform action */
    bool shouldAct()
    {
        chr::time_point<chr::high_resolution_clock> tcur = get_time();
        chr::nanoseconds elapsed_nsec = tcur - last_acted_time;
        chr::seconds elapsed_sec = 
            chr::duration_cast<chr::seconds>(elapsed_nsec);
        elapsed_nsec -= chr::duration_cast<chr::nanoseconds>(elapsed_sec);
        // we have spent as much time as we wanted.
        if (elapsed_sec >= debounce_sec && 
            elapsed_nsec >= debounce_nanosec) {
            last_acted_time = tcur;
            return true;
        }
        return false;
    }

    Debouncer(chr::seconds sec, chr::nanoseconds nanosec)
        : debounce_sec(sec)
        , debounce_nanosec(nanosec)
    {
        last_acted_time = chr::high_resolution_clock::now();
    };

    static chr::time_point<chr::high_resolution_clock> get_time()
    {
        return chr::high_resolution_clock::now();
    }

private:
    const chr::seconds debounce_sec; // debounce duration, seconds.
    const chr::nanoseconds debounce_nanosec; // debounce duration, nanoseconds.
    chr::time_point<chr::high_resolution_clock> last_acted_time;
};
