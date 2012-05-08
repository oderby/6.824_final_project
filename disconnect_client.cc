// RPC stubs for clients to talk to rsmtest_server

#include "disconnect_client.h"
#include <arpa/inet.h>

#include <sstream>
#include <iostream>
#include <stdio.h>

disconnect_client::disconnect_client(std::string dst)
{
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() < 0) {
    printf("disconnect_client: call bind\n");
  }
}

int
disconnect_client::disconnect(bool kill)
{
  int r;
  int ret = cl->call(lock_test_protocol::disconnect, kill, r);
  VERIFY(ret == lock_test_protocol::OK);
  return r;
}
