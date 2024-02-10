// https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797
// [(<accent>;)?<forground>;<background>;
#define ESCAPE_CODE_DULL "\x1b[90;40m" // briht black foreground, black background
#define ESCAPE_CODE_CURSOR_INSERT "\x1b[30;47m" // black foreground, white background
#define ESCAPE_CODE_CURSOR_NORMAL "\x1b[30;100m" // black foreground, bright black background
#define ESCAPE_CODE_CURSOR_SELECT "\x1b[30;46m" // black foreground, cyan background
#define ESCAPE_CODE_RED "\x1b[30;41m" // black foreground, red background
#define ESCAPE_CODE_YELLOW "\x1b[30;43m" // black foreground, yellow background
#define ESCAPE_CODE_GREEN "\x1b[30;42m" // black foreground, green background
#define ESCAPE_CODE_WHITE "\x1b[30;47m" // black foreground, white background
#define ESCAPE_CODE_GRAY "\x1b[30;40m" // black foreground, black background
#define ESCAPE_CODE_BLUE "\x1b[30;44m" // black foreground, blue background
#define ESCAPE_CODE_UNSET "\x1b[0m"

