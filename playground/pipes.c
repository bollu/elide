#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#define PIPE_WRITE_IX 1
#define PIPE_READ_IX 0


int main_ls() {
  int child_stdout_to_parent_buffer[2];
  const char *process = "/bin/ls";
  const char * const argv[] = { process, "-a", "-l", "/tmp/", NULL };
  const char *const env[] = { NULL };
  pid_t childpid;

  pipe(child_stdout_to_parent_buffer);

  if((childpid = fork()) == -1) {
    perror("fork failed");
    exit(1);
  };

  if(childpid == 0) {
    printf("CHILD: childpid: '%d'\n", childpid);
    dup2(child_stdout_to_parent_buffer[PIPE_WRITE_IX], STDOUT_FILENO); // write part of pipe.
    close(child_stdout_to_parent_buffer[PIPE_READ_IX]);
    if(execve(process, argv, env) == -1) {
      perror("exec failed");
      exit(1);
    }
  } else {
    printf("PARENT: childpid: '%d'\n", childpid);
    int SLEEP_TIME = 5;
    printf("PARENT: sleeping for %4dsec...\n", SLEEP_TIME);
    sleep(SLEEP_TIME);

    printf("PARENT: closing pipe write side...\n");
    close(child_stdout_to_parent_buffer[PIPE_WRITE_IX]);
    // assert(0 && "parent");
    static const int BUF_SIZE = 8196;
    char buf[BUF_SIZE];
    printf("PARENT: woke up...\n");
    printf("PARENT: reading from buffer\n");

    close(child_stdout_to_parent_buffer[1]);
    int nread = read(child_stdout_to_parent_buffer[PIPE_READ_IX], buf, BUF_SIZE - 1);
    buf[nread] = 0;
    printf("PARENT: read '%d' bytes\n", nread);
    printf("output: '%s'\n", buf);
    fflush(stdout);
    return 0;
  }
}

int main_cat() {
  int child_stdout_to_parent_buffer[2];
  int parent_buffer_to_child_stdin[2];
  const char *process = "/bin/cat";
  const char * const argv[] = { process, NULL };
  pid_t childpid;

  pipe(child_stdout_to_parent_buffer);
  pipe(parent_buffer_to_child_stdin);

  if((childpid = fork()) == -1) {
    perror("fork failed");
    exit(1);
  };

  if(childpid == 0) {
    printf("CHILD: childpid: '%d'\n", childpid);
    close(child_stdout_to_parent_buffer[PIPE_READ_IX]);
    close(parent_buffer_to_child_stdin[PIPE_WRITE_IX]);

    dup2(child_stdout_to_parent_buffer[PIPE_WRITE_IX], STDOUT_FILENO); // write part of pipe.
    dup2(parent_buffer_to_child_stdin[PIPE_READ_IX], STDIN_FILENO); // read from read end of pipe.
    if(execvp(process, argv) == -1) {
      perror("exec failed");
      exit(1);
    }
  } else {
    printf("PARENT: childpid: '%d'\n", childpid);
    int SLEEP_TIME = 1;
    printf("PARENT: sleeping for %4dsec...\n", SLEEP_TIME);
    sleep(SLEEP_TIME);

    printf("PARENT: closing pipe write side...\n");
    close(child_stdout_to_parent_buffer[PIPE_WRITE_IX]);

    printf("PARENT: closing pipe read side...\n");
    close(parent_buffer_to_child_stdin[PIPE_READ_IX]);

    printf("PARENT: sleeping for %4dsec...\n", SLEEP_TIME);
    sleep(SLEEP_TIME);

    static const int BUF_SIZE = 8196;
    char buf[BUF_SIZE];

    printf("PARENT: writing into write buffer\n");
    int nwritten = sprintf(buf, "foo bar baz quux");
    write(parent_buffer_to_child_stdin[PIPE_WRITE_IX], buf, nwritten);
    printf("written '%d' bytes\n", nwritten);

    printf("PARENT: sleeping for %4dsec...\n", SLEEP_TIME);
    sleep(SLEEP_TIME);

    printf("PARENT: woke up...\n");
    printf("PARENT: reading from buffer\n");

    close(child_stdout_to_parent_buffer[1]);
    int nread = read(child_stdout_to_parent_buffer[PIPE_READ_IX], buf, BUF_SIZE - 1);
    buf[nread] = 0;
    printf("PARENT: read '%d' bytes\n", nread);
    printf("output: '%s'\n", buf);
    fflush(stdout);
    return 0;
  }
}


int main_lean_server() {
  int child_stdout_to_parent_buffer[2];
  int parent_buffer_to_child_stdin[2];
  const char *process = "lean";
  const char * const argv[] = { process, "--server", NULL };
  pid_t childpid;

  pipe(child_stdout_to_parent_buffer);
  pipe(parent_buffer_to_child_stdin);

  if((childpid = fork()) == -1) {
    perror("fork failed");
    exit(1);
  };

  if(childpid == 0) {
    printf("CHILD: childpid: '%d'\n", childpid);
    close(child_stdout_to_parent_buffer[PIPE_READ_IX]);
    close(parent_buffer_to_child_stdin[PIPE_WRITE_IX]);

    dup2(child_stdout_to_parent_buffer[PIPE_WRITE_IX], STDOUT_FILENO); // write part of pipe.
    dup2(parent_buffer_to_child_stdin[PIPE_READ_IX], STDIN_FILENO); // read from read end of pipe.
    if(execvp(process, argv) == -1) {
      perror("exec failed");
      exit(1);
    }
  } else {
    printf("PARENT: childpid: '%d'\n", childpid);
    int SLEEP_TIME = 1;
    printf("PARENT: sleeping for %4dsec...\n", SLEEP_TIME);
    sleep(SLEEP_TIME);

    printf("PARENT: closing pipe write side...\n");
    close(child_stdout_to_parent_buffer[PIPE_WRITE_IX]);

    printf("PARENT: closing pipe read side...\n");
    close(parent_buffer_to_child_stdin[PIPE_READ_IX]);

    printf("PARENT: sleeping for %4dsec...\n", SLEEP_TIME);
    sleep(SLEEP_TIME);

    static const int BUF_SIZE = 8196;
    char buf[BUF_SIZE];

    printf("PARENT: writing into write buffer\n");
    int nwritten = sprintf(buf, "aaabbcc\n");
    write(parent_buffer_to_child_stdin[PIPE_WRITE_IX], buf, nwritten);
    printf("written '%d' bytes\n", nwritten);

    printf("PARENT: sleeping for %4dsec...\n", SLEEP_TIME);
    sleep(SLEEP_TIME);

    printf("PARENT: woke up...\n");
    printf("PARENT: reading from buffer\n");

    close(child_stdout_to_parent_buffer[1]);
    int nread = read(child_stdout_to_parent_buffer[PIPE_READ_IX], buf, BUF_SIZE - 1);
    buf[nread] = 0;
    printf("PARENT: read '%d' bytes\n", nread);
    printf("output: '%s'\n", buf);
    fflush(stdout);
    return 0;
  }
}

int main() {
  main_lean_server();
}
