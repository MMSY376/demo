/**
 * Generate IPV4 and IPV6 5-tuples to standard output
 * gen at the best effort.
 * Need the VIP:port list
 */
#include "common.h"
#include <csignal>
#include <cstdarg>
#include <gperftools/profiler.h>

#ifndef FIX_DIP_NUM
ofstream queryLog(NAME ".query.data", ios::app), addLog(NAME ".add.data", ios::app), memoryLog(NAME ".mem.data", ios::app), dynamicLog(NAME ".dynamic.data", ios::app);
#else
ofstream queryLog(NAME ".query.fix.data", ios::app), addLog(NAME ".add.fix.data", ios::app), memoryLog(NAME ".mem.fix.data", ios::app), dynamicLog(NAME ".dynamic.fix.data", ios::app);
#endif

int Clocker::currentLevel = 0;
list<Counter> Counter::counters;
Clocker global("root");

int stick_this_thread_to_core(int core_id) {
  int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
  if (core_id < 0 || core_id >= num_cores) return EINVAL;
  
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  
  pthread_t current_thread = pthread_self();
  return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

static pthread_mutex_t printf_mutex;
void sync_printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  
  pthread_mutex_lock(&printf_mutex);
  vprintf(format, args);
  pthread_mutex_unlock(&printf_mutex);
  
  va_end(args);
}

void sig_handler(int sig) {
//  ProfilerStop();
  for (auto &c:Counter::counters) {
    c.lap();
  }
  exit(0);
}

void registerSigHandler() {
  signal(SIGINT, sig_handler);
}

void commonInit() {
  srand(0x19900111);
  
  registerSigHandler();
  pthread_mutex_init(&printf_mutex, NULL);
}

//! convert a 64-bit Integer to human-readable format in K/M/G. e.g, 102400 is converted to "100K".
std::string human(uint64_t word) {
  std::stringstream ss;
  if (word <= 1024) ss << word;
  else if (word <= 10240) ss << std::setprecision(2) << word * 1.0 / 1024 << "K";
  else if (word <= 1048576) ss << word / 1024 << "K";
  else if (word <= 10485760) ss << word * 1.0 / 1048576 << "M";
  else if (word <= (1048576 << 10)) ss << word / 1048576 << "M";
  else ss << word * 1.0 / (1 << 30) << "G";
  std::string s;
  ss >> s;
  return s;
}

//! split a c-style string with delimineter chara.
std::vector<std::string> split(const char *str, char deli) {
  std::istringstream ss(str);
  std::string token;
  std::vector<std::string> ret;
  while (std::getline(ss, token, deli)) {
    if (token.size() >= 1) ret.push_back(token);
  }
  return ret;
}

string Counter::pad() const {
  if (!Clocker::currentLevel) return "";
  
  ostringstream oss;
  for (int i = 0; i < Clocker::currentLevel; ++i) oss << "| ";
  oss << "  ";
  return oss.str();
}
