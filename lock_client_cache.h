// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_client.h"
#include "lang/verify.h"

// Classes that inherit lock_release_user can override dorelease so that
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 5.
class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};

void* release_lock(void*);
void* send_release(void*);

class lock_client_cache : public lock_client {
 private:
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;
  enum lockstate { NONE, ACQUIRING, WAITING, FREE, LOCKED, RELEASING, FREE_RLS };
  std::map<lock_protocol::lockid_t, lockstate> lock_status_;
  pthread_mutex_t m_; //protect lock_status_
  pthread_cond_t wait_retry_;
 public:
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache() {};
  lock_protocol::status acquire(lock_protocol::lockid_t);
  lock_protocol::status release(lock_protocol::lockid_t);
  rlock_protocol::status revoke_handler(lock_protocol::lockid_t,
                                        int &);
  rlock_protocol::status retry_handler(lock_protocol::lockid_t,
                                       int &);
  //lock_protocol::status send_release(lock_protocol::lockid_t);
};

struct lock_rls_info {
  lock_protocol::lockid_t lid;
  std::string id;
  rpcc* cl;
  //lock_client_cache* ls;
};

#endif
