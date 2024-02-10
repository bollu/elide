#include <string>

struct BaseProcess {
  // buffer to store child stdout data that has not
  // been slurped yet.
  abuf stdout_buffer;
                              
  // read from child in nonblocking fashion. returns number of bytes read.
  virtual int read_nonblocking() = 0;
  virtual int write_blocking() = 0;
};

BaseProcess* createProcess(std::string name, std::vector<std::string> args) {};

#ifdef WINDOWS
#endif