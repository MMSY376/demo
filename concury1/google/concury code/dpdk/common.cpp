/**
 * Generate IPV4 and IPV6 5-tuples to standard output
 * gen at the best effort.
 * Need the VIP:port list
 */
#include "common.h"
#include <csignal>
#include <cstdarg>
#include <gperftools/profiler.h>

uint VIP_NUM = 1;                       // must be power of 2
uint VIP_MASK = (VIP_NUM - 1);
uint CONN_NUM = (16 * 1024 * 1024);    // must be multiple of VIP_NUM
uint DIP_NUM = (VIP_NUM * 32);
uint DIP_NUM_MIN = 8;
uint DIP_NUM_MAX = 64;
uint LOG_INTERVAL = (50 * 1000000);       // must be multiple of 1E6
uint HT_SIZE = 4096;                    // must be power of 2
uint STO_NUM = (CONN_NUM);                // simulate control plane

int Clocker::currentLevel = 0;
list<Counter> Counter::counters;

void sig_handler(int sig) {
  ProfilerStop();
  for (auto &c:Counter::counters) {
    c.lap();
  }
  exit(0);
}

void doNotOptimize() {

}

void registerSigHandler() {
  signal(SIGINT, sig_handler);
}

void commonInit() {
  srand(0x19900111);
  
  registerSigHandler();
}

//! convert a 64-bit Integer to human-readable format in K/M/G. e.g, 102400 is converted to "100K".
std::string human(uint64_t word) {
  std::stringstream ss;
  if (word <= 1024) { ss << word; }
  else if (word <= 10240) { ss << std::setprecision(2) << word * 1.0 / 1024 << "K"; }
  else if (word <= 1048576) { ss << word / 1024 << "K"; }
  else if (word <= 10485760) { ss << word * 1.0 / 1048576 << "M"; }
  else if (word <= (1048576 << 10)) { ss << word / 1048576 << "M"; }
  else { ss << word * 1.0 / (1 << 30) << "G"; }
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
