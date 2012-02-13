// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server():
  nacquire (0)
{
  VERIFY(pthread_mutex_init(&m_, 0) == 0);
  VERIFY(pthread_cond_init(&wait_unlock_, 0) == 0);
}

lock_server::~lock_server()
{
  VERIFY(pthread_mutex_destroy(&m_) == 0);
  VERIFY(pthread_cond_destroy(&wait_unlock_) == 0);
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("acquire request from clt %d\n", clt);
  r = nacquire;
  VERIFY(pthread_mutex_lock(&m_)==0);
  while (1) {
    if (lock_status_[lid]) {
      VERIFY(pthread_cond_wait(&wait_unlock_, &m_) == 0);
    } else {
      break;
    }
  }
  lock_status_[lid] = true;
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("release request from clt %d\n", clt);
  r = nacquire;
  lock_status_[lid] = false;
  VERIFY(pthread_cond_signal(&wait_unlock_)==0);
  return ret;
}
