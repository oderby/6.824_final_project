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
  virtual bool exists(lock_protocol::lockid_t) = 0;
  virtual bool isdirty(lock_protocol::lockid_t) = 0;
  virtual void remove(lock_protocol::lockid_t) = 0;
  virtual bool compareversion(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};

class lock_client_cache : public lock_client {
 private:
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;
  struct lockstate {
    lock_protocol::state state;
    // is it possible that our state is stale?
    bool stale;
   public:
    lockstate() {
      state = lock_protocol::NONE;
      stale = false;
    }
  };

  std::map<lock_protocol::lockid_t, lockstate> lock_status_;
  pthread_mutex_t m_; //protect lock_status_
  pthread_cond_t wait_retry_;
  pthread_cond_t wait_release_;
  bool disconnected;
  lock_protocol::status acquire_wo(lock_protocol::lockid_t);
  lock_protocol::status release_wo(lock_protocol::lockid_t);
  void lock_acquired(lock_protocol::lockid_t);

 public:
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache() {};
  lock_protocol::status acquire(lock_protocol::lockid_t);
  lock_protocol::status release(lock_protocol::lockid_t);
  lock_test_protocol::status disconnect_server();
  rlock_protocol::status revoke_handler(lock_protocol::lockid_t,
                                        int &);
  rlock_protocol::status retry_handler(lock_protocol::lockid_t,
                                       int &);
  lock_test_protocol::status disconnect(bool, int &);
};

struct lock_rls_info {
  lock_protocol::lockid_t lid;
  std::string id;
  rpcc* cl;
  //lock_client_cache* ls;
};

#endif
