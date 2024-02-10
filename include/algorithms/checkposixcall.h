#define CHECK_POSIX_CALL_0(x)                \
    {                                        \
        do {                                 \
            int success = x == 0;            \
            if (!success) {                  \
                perror("POSIX call failed"); \
            };                               \
            assert(success);                 \
        } while (0);                         \
    }
// check that minus 1 is notreturned.
#define CHECK_POSIX_CALL_M1(x)               \
    {                                        \
        do {                                 \
            int fail = x == -1;              \
            if (fail) {                      \
                perror("POSIX call failed"); \
            };                               \
            assert(!fail);                   \
        } while (0);                         \
    }

