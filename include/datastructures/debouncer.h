#pragma once

// TODO: move to https://en.cppreference.com/w/cpp/chrono

// Store the current time, and tells when it is legal to run another operation.
struct Debouncer {
    // return if debounce is possible. If this returns `true`, the client is
    // expected to perform the action.
    // Prefer to use this API as follows:
    // 1) if (debouncer.shouldAct()) { /* perform action */ }
    // 2) if (!debouncer.shouldAct()) { return; } /* perform action */
    bool shouldAct()
    {
        timespec tcur;
        get_time(&tcur);
        const long elapsed_nanosec = tcur.tv_nsec - last_acted_time.tv_nsec;
        const long elapsed_sec = tcur.tv_sec - last_acted_time.tv_sec;
        // we have spent as much time as we wanted.
        if (elapsed_sec >= debounce_sec && elapsed_nanosec >= debounce_nanosec) {
            last_acted_time = tcur;
            return true;
        }
        return false;
    }

    static long millisToNanos(long millis)
    {
        return millis * 1000000;
    }

    Debouncer(long sec, long nanosec)
        : debounce_sec(sec)
        , debounce_nanosec(nanosec)
    {
        last_acted_time.tv_sec = last_acted_time.tv_nsec = 0;
    };

    static void get_time(timespec* ts)
    {
        if (clock_gettime(CLOCK_REALTIME, ts) == -1) {
            perror("unable to get time from clock.");
            exit(1);
        }
    }

private:
    const int debounce_sec; // debounce duration, seconds.
    const int debounce_nanosec; // debounce duration, nanoseconds.
    timespec last_acted_time;
};
