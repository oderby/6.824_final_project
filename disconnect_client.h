// rsmtest client interface.

#ifndef rsmtest_client_h
#define rsmtest_client_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"

// Client interface to the rsmtest server
class rsmtest_client {
 protected:
  rpcc *cl;
 public:
  disconnect_client(std::string d);
  virtual ~disconnect_client() {};
  virtual lock_test_protocol::status disconnect(bool);
};
#endif
