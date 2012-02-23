// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include <pthread.h>
#include <time.h>
#include "extent_protocol.h"
#include "lang/verify.h"

class extent_server {
  struct finfo {
    std::string buf;
    extent_protocol::attr a;
  };

  std::map<extent_protocol::extentid_t, finfo> table_;
  // table lock
  pthread_mutex_t m_;

 public:
  extent_server();

  int put(extent_protocol::extentid_t id, std::string, int &);
  int get(extent_protocol::extentid_t id, std::string &);
  int getattr(extent_protocol::extentid_t id, extent_protocol::attr &);
  int remove(extent_protocol::extentid_t id, int &);
};

#endif
