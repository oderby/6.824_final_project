// extent client interface.

#ifndef extent_client_cache_h
#define extent_client_cache_h

#include <string>
#include "extent_protocol.h"
#include "extent_client.h"
#include "rpc.h"

struct extent_entry {
  bool dirty;
  std::string extent;
  extent_protocol::attr attr;
};

class extent_client_cache : public extent_client {
 private:
  std::map<extent_protocol::extentid_t,extent_entry> local_extent_;

 public:
  extent_client_cache(std::string dst);

  extent_protocol::status get(extent_protocol::extentid_t eid,
			      std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid,
				  extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
};

#endif
