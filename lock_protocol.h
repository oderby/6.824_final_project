// lock protocol

#ifndef lock_protocol_h
#define lock_protocol_h

#include "rpc.h"

class lock_protocol {
 public:
  enum xxstatus { OK, RETRY, RPCERR, NOENT, IOERR };
  typedef int status;
  typedef unsigned long long lockid_t;
  typedef unsigned long long xid_t;
  enum state { NONE=101,
               ACQUIRING,
               WAITING,
               FREE,
               LOCKED,
               RELEASING,
               FREE_RLS };
  enum rpc_numbers {
    acquire = 0x7001,
    release,
    stat
  };
};

class rlock_protocol {
 public:
  enum xxstatus { OK, RPCERR };
  typedef int status;
  enum rpc_numbers {
    revoke = 0x8001,
    retry = 0x8002
  };
};

class lock_test_protocol {
 public:
  enum xxstatus {OK, RPCERR};
  typedef int status;
  enum rpc_numbers {
    disconnect =  0x9001,
    disconnect_server = 0x9002
  };
};

#endif
