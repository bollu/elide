// https://vt100.net/docs/vt100-ug/chapter3.html#CPR
// control key: maps to 1...26,
// 00011111
// #define CTRL_KEY(k) ((k) & ((1 << 6) - 1))
#define CTRL_KEY(k) ((k)&0x1f)

