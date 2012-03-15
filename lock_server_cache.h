#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#include <map>
#include <set>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"

struct lockinfo {
  bool locked;
  std::string owner;
  std::set<std::string> waiting;
};

struct lock_retry_info {
  lock_protocol::lockid_t lid;
  //std::string id;
  std::set<std::string> waiting;
};

void* retry_wrapper(void*);

class lock_server_cache {
 private:
  int nacquire;
  std::map<lock_protocol::lockid_t, lockinfo> lock_status_;
  pthread_mutex_t m_;
  lock_protocol::status send_revoke(lock_protocol::lockid_t, std::string);
  lock_protocol::status send_retry(lock_protocol::lockid_t, std::string);
 public:
  lock_server_cache();
  ~lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);
};

#endif
