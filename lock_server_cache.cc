// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


lock_server_cache::lock_server_cache():
    nacquire(0)
{
  VERIFY(pthread_mutex_init(&m_, 0) == 0);
}

lock_server_cache::~lock_server_cache()
{
  VERIFY(pthread_mutex_destroy(&m_) == 0);
}

int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id,
                               int &)
{
  lock_protocol::status ret = lock_protocol::OK;
  bool should_revoke;
  std::string owner;
  VERIFY(pthread_mutex_lock(&m_)==0);
  tprintf("acquire request from %s for lid %llu\n", id.c_str(), lid);
  if (lock_status_[lid].locked) {
    if (lock_status_[lid].owner.compare(id)==0) {
      tprintf("lock_server_cache ERROR: %s already has lock lid %llu\n", id.c_str(), lid);
    }
    should_revoke = lock_status_[lid].waiting.empty();
    if (should_revoke) {
      owner = lock_status_[lid].owner;
    }
    lock_status_[lid].waiting.insert(id);
    ret = lock_protocol::RETRY;
  } else {
    lock_status_[lid].locked = true;
    lock_status_[lid].owner = id;
    lock_status_[lid].waiting.clear();
  }
  VERIFY(pthread_mutex_unlock(&m_)==0);
  // do not hold mutex while sending revoke rpc
  if (should_revoke) {
    tprintf("sending revoke to %s for lid %llu\n",
            owner.c_str(), lid);
    lock_protocol::status r = send_revoke(lid, owner);
    if (r != lock_protocol::OK) {
      tprintf("ERROR(%d) sending revoke to %s for lid %llu\n",
              r, owner.c_str(), lid);
    }
  }
  return ret;
}

lock_protocol::status
lock_server_cache::send_revoke(lock_protocol::lockid_t lid, std::string id)
{
  handle h(id);
  rlock_protocol::status r;
  if (h.safebind()) {
    lock_protocol::status ret;
    ret = h.safebind()->call(rlock_protocol::revoke,lid,r);
    VERIFY (ret == lock_protocol::OK);
  } else {
    return lock_protocol::IOERR;
  }
  if (r != rlock_protocol::OK) {
    return lock_protocol::RPCERR;
  }
  return lock_protocol::OK;
}

int
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  std::set<std::string> temp;
  VERIFY(pthread_mutex_lock(&m_)==0);
  tprintf("release request from %s for lid %llu\n", id.c_str(), lid);
  if (!lock_status_[lid].locked) {
    tprintf("release of unlocked lock!\n");
  }
  lock_status_[lid].locked = false;
  VERIFY(lock_status_[lid].owner.compare(id)==0);
  lock_status_[lid].owner.clear();

  lock_retry_info* info = new lock_retry_info();
  info->lid = lid;
  info->waiting = lock_status_[lid].waiting;

  lock_status_[lid].waiting = std::set<std::string>();
  VERIFY(pthread_mutex_unlock(&m_)==0);
  //std::set<std::string>::iterator i=temp.begin();
  lock_protocol::status re;
  //pthread_attr_t attr;
  //pthread_attr_init(&attr);
  //pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t tid;
  int rc = pthread_create(&tid, NULL, retry_wrapper, (void *) info);
  VERIFY(rc==0);
  /*
  for (;i!=temp.end(); i++) {
    lock_retry_info info;
    info.lid = lid;
    info.id = *i;
    pthread_t tid;
    //retry_wrapper((void *)&info);
    int rc = pthread_create(&tid, NULL, retry_wrapper, (void *) &info);
    VERIFY(rc==0);
    /*re = send_retry(lid, *i);
    if (re != lock_protocol::OK) {
      tprintf("lock_server_cache ERROR(%d) sending retry to %s for lid %llu\n",r, i->c_str(), lid);
    }

  }
  */
  //pthread_attr_destroy(&attr);
  return ret;
}

void* retry_wrapper(void* i) {
  struct lock_retry_info* info;
  info = (struct lock_retry_info*)i;

  std::set<std::string>::iterator it=info->waiting.begin();
  rlock_protocol::status r = lock_protocol::OK;
  for (;it!=info->waiting.end(); it++) {
    tprintf("sending retry to %s for lid %llu\n",
            it->c_str(), info->lid);
    handle h(*it);
    if (h.safebind()) {
      lock_protocol::status ret;
      ret = h.safebind()->call(rlock_protocol::retry,info->lid,r);
      VERIFY (ret == lock_protocol::OK);
    } else {
      r = lock_protocol::IOERR;
    }
    if (r != rlock_protocol::OK) {
      r = lock_protocol::RPCERR;
    }
  }

  info->waiting.clear();
  delete info;
  if (r == lock_protocol::OK) {
    pthread_exit(NULL);
  } else {
    pthread_exit((void *)r);
  }
}

lock_protocol::status
lock_server_cache::send_retry(lock_protocol::lockid_t lid, std::string id)
{
  handle h(id);
  rlock_protocol::status r;
  if (h.safebind()) {
    lock_protocol::status ret;
    ret = h.safebind()->call(rlock_protocol::retry,lid,r);
    VERIFY (ret == lock_protocol::OK);
  } else {
    return lock_protocol::IOERR;
  }
  if (r != rlock_protocol::OK) {
    return lock_protocol::RPCERR;
  }
  return lock_protocol::OK;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request from for lid %llu\n", lid);
  r = nacquire;
  return lock_protocol::OK;
}
