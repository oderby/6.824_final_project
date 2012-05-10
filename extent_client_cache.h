// extent client interface.

#ifndef extent_client_cache_h
#define extent_client_cache_h

#include <string>
#include "extent_protocol.h"
#include "extent_client.h"
#include "lock_client_cache.h"
#include "rpc.h"

struct extent_entry {
  bool dirty;
  std::string extent;
  extent_protocol::attr attr;
};

class extent_client_cache : public extent_client {
 private:
  std::map<extent_protocol::extentid_t,extent_entry> local_extent_;
  pthread_mutex_t m_; //protect local_extent_ access

 public:
  extent_client_cache(std::string dst);

  extent_protocol::status get_helper(extent_protocol::extentid_t);
  extent_protocol::status get(extent_protocol::extentid_t eid,
			      std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid,
				  extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
  extent_protocol::status flush(extent_protocol::extentid_t eid);
  bool exists(extent_protocol::extentid_t);
  bool is_dirty(extent_protocol::extentid_t);
  bool compare_version(extent_protocol::extentid_t);
};


class extent_user: public lock_release_user {
 private:
  extent_client_cache* ec_;
 public:
  extent_user(extent_client_cache*);
  void dorelease(lock_protocol::lockid_t);
  bool exists(lock_protocol::lockid_t);
  bool isdirty(lock_protocol::lockid_t);
  void remove(lock_protocol::lockid_t);
  bool compareversion(lock_protocol::lockid_t);
};

#endif
