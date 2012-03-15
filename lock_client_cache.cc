// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"


lock_client_cache::lock_client_cache(std::string xdst,
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  VERIFY(pthread_mutex_init(&m_, 0) == 0);

  rpcs *rlsrpc = new rpcs(0);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

  const char *hname;
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlsrpc->port();
  id = host.str();

  VERIFY(lock_status_[0]==lock_client_cache::NONE);
  VERIFY(pthread_cond_init(&wait_retry_, 0) == 0);
  VERIFY(pthread_cond_init(&wait_release_, 0) == 0);
}

/*lock_client_cache::~lock_client_cache()
{
  VERIFY(pthread_mutex_destroy(&m_) == 0);
}
*/

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  lock_protocol::status ret = lock_protocol::OK;
  bool try_acquire = false;
  VERIFY(pthread_mutex_lock(&m_)==0);
  lockstate lis = lock_status_[lid];
  while (lock_status_[lid] != lock_client_cache::NONE
         && lock_status_[lid] != lock_client_cache::FREE) {
    VERIFY(pthread_cond_wait(&wait_release_, &m_) == 0);
  }
  //Check state
  if (lis == lock_client_cache::NONE) {
    try_acquire = true;
    lock_status_[lid] = lock_client_cache::ACQUIRING;
  } else if (lis == lock_client_cache::FREE) {
    tprintf("lock_client_cache(%s): trying acquire of lock %llu in state %d\n",
            id.c_str(), lid, lock_status_[lid]);
    lock_status_[lid] = lock_client_cache::LOCKED;
  }

  while (try_acquire) {
    tprintf("lock_client_cache(%s): trying acquire of lock %llu in state %d\n",
            id.c_str(), lid, lock_status_[lid]);
    // do not hold mutex while making rpc call
    VERIFY(pthread_mutex_unlock(&m_)==0);
    lock_protocol::status r;
    ret = cl->call(lock_protocol::acquire, lid, id, r);
    VERIFY(r == lock_protocol::OK);
    VERIFY(pthread_mutex_lock(&m_)==0);
    try_acquire = false;
    // need to grab (possibly changed) lis
    lis = lock_status_[lid];
    tprintf("lock_client_cache(%s): acquire received %d in state %d for lock %llu\n",
            id.c_str(), ret, lis, lid);
    if (lis == lock_client_cache::ACQUIRING) {
      if (ret == lock_protocol::OK) {
        lock_status_[lid] = lock_client_cache::LOCKED;
      } else if (ret == lock_protocol::RETRY) {
        //wait for retry_handler invocation
        lock_status_[lid] = lock_client_cache::WAITING;
        while (lock_status_[lid] == lock_client_cache::WAITING) {
          VERIFY(pthread_cond_wait(&wait_retry_, &m_) == 0);
        }
        try_acquire = true;
      } else {
        tprintf("lock_client_cache(%s):acquire received unexpected error(%d) for lock %llu\n",
                id.c_str(), ret, lid);
      }
    } else if (lis == lock_client_cache::WAITING) {
      if (ret == lock_protocol::OK) {
        lock_status_[lid] = lock_client_cache::LOCKED;
      } else if (ret == lock_protocol::RETRY) {
        // must have received retry RPC out of order, so just go ahead and retry
        // acq
        try_acquire = true;
        lock_status_[lid] = lock_client_cache::ACQUIRING;
      } else {
        tprintf("lock_client_cache(%s): Received unexpected error(%d) for lock %llu\n",
                id.c_str(), ret, lid);
      }
    } else if (ret != lock_protocol::OK) {
      tprintf("lock_client_cache(%s): Received unexpected error(%d) for lock %llu\n",
              id.c_str(),ret, lid);
    }
  }

  VERIFY(pthread_mutex_unlock(&m_)==0);
  return ret;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
  int ret = rlock_protocol::OK;
  VERIFY(pthread_mutex_lock(&m_)==0);
  lockstate lis = lock_status_[lid];
  tprintf("lock_client_cache(%s): release of lock %llu in state %d\n",
          id.c_str(), lid, lis);
  if (lis == lock_client_cache::LOCKED) {
    lock_status_[lid] = FREE;
    VERIFY(pthread_cond_signal(&wait_release_)==0);
  } else if (lis == lock_client_cache::RELEASING) {
    VERIFY(pthread_mutex_unlock(&m_)==0);
    // release lock to lock server
    lock_protocol::status r;
    ret = cl->call(lock_protocol::release, lid, id, r);
    VERIFY (r == lock_protocol::OK);
    VERIFY(pthread_mutex_lock(&m_)==0);
    if (ret != lock_protocol::OK) {
      tprintf("lock_client_cache(%s): rls->rls Received unexpected error(%d) for lock %llu\n",
              id.c_str(), ret, lid);
    }
    lock_status_[lid] = lock_client_cache::NONE;
    VERIFY(pthread_cond_signal(&wait_release_)==0);
  } else {
    tprintf("lock_client_cache(%s): unhandled state (%d) in rls for lock %llu\n",
            id.c_str(), lis, lid);
  }

  VERIFY(pthread_mutex_unlock(&m_)==0);
  return lock_protocol::OK;
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid,
                                  int &)
{
  int ret = rlock_protocol::OK;
  bool should_release = false;
  VERIFY(pthread_mutex_lock(&m_)==0);
  lockstate lis = lock_status_[lid];
  tprintf("lock_client_cache(%s): received revoke of lock %llu in state %d\n",
          id.c_str(), lid, lis);
  switch(lis) {
    case lock_client_cache::ACQUIRING:
    case lock_client_cache::WAITING:
    case lock_client_cache::LOCKED:
      {
        lock_status_[lid] = lock_client_cache::RELEASING;
        break;
      }
    case lock_client_cache::FREE:
      {
        lock_status_[lid] = lock_client_cache::FREE_RLS;
        //should_release = true;

        VERIFY(pthread_mutex_unlock(&m_)==0);
        // release lock to lock server
        lock_protocol::status r;
        ret = cl->call(lock_protocol::release, lid, id, r);
        VERIFY (r == lock_protocol::OK);
        VERIFY(pthread_mutex_lock(&m_)==0);
        if (ret != lock_protocol::OK) {
          tprintf("lock_client_cache: revoke->rls Received unexpected error(%d) for lock %llu\n",
                  ret, lid);
        }
        lock_status_[lid] = lock_client_cache::NONE;
        VERIFY(pthread_cond_signal(&wait_release_)==0);
        break;
      }
    default:
      tprintf("lock_client_cache: unexpected state (%d) in revoke!\n",lis);
  }

  VERIFY(pthread_mutex_unlock(&m_)==0);
  /*
  if (should_release) {
    lock_rls_info info;
    info.lid = lid;
    //info.lc = this;
    info.id = id;
    info.cl = cl;
    //send_release((void *) &info);
    int rc = pthread_create(0, NULL, send_release, (void *) &info);
    VERIFY(rc==0);
  }
  */
  return ret;
}

void* send_release(void* i)
{
  struct lock_rls_info* info;
  info = (struct lock_rls_info*)i;
  lock_protocol::status r, ret;

  ret = info->cl->call(lock_protocol::release, info->lid, info->id, r);
  VERIFY (r == lock_protocol::OK);
  /*if (ret != lock_protocol::OK) {
    tprintf("lock_client_cache(%s): rls->rls Received unexpected error(%d) for lock %llu\n",
            info->id.c_str(), ret, info->lid);
  }
  */
  //VERIFY(pthread_mutex_lock(&m_)==0);
  //lock_status_[lid] = lock_client_cache::NONE;
  //VERIFY(pthread_mutex_unlock(&m_)==0);
  if (ret == lock_protocol::OK) {
    pthread_exit(NULL);
  } else {
    pthread_exit((void *)ret);
  }
}

/*
lock_protocol::status
lock_client_cache::send_release(lock_protocol::lockid_t lid)
{
  lock_protocol::status r, ret;
  ret = cl->call(lock_protocol::release, lid, id, r);
  VERIFY (r == lock_protocol::OK);
  if (ret != lock_protocol::OK) {
    tprintf("lock_client_cache(%s): rls->rls Received unexpected error(%d) for lock %llu\n",
            id.c_str(), ret, lid);
  }
  //VERIFY(pthread_mutex_lock(&m_)==0);
  //lock_status_[lid] = lock_client_cache::NONE;
  //VERIFY(pthread_mutex_unlock(&m_)==0);
  return ret;
}
*/
/*
void* release_lock(void* i)
{
  struct lock_rls_info* info;
  info = (struct lock_rls_info*)i;
  lock_protocol::status r;

  r = info->lc->send_release(info->lid);

  if (r == lock_protocol::OK) {
    pthread_exit(NULL);
  } else {
    pthread_exit((void *)r);
  }
}
*/

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid,
                                 int &)
{
  int ret = rlock_protocol::OK;
  VERIFY(pthread_mutex_lock(&m_)==0);
  lockstate lis = lock_status_[lid];
  tprintf("lock_client_cache(%s): received retry of lock %llu in state %d\n",
          id.c_str(), lid, lis);
  //Check state
  if (lis == lock_client_cache::ACQUIRING) {
    lock_status_[lid] = lock_client_cache::WAITING;
  } else if (lis == lock_client_cache::WAITING) {
    lock_status_[lid] = lock_client_cache::ACQUIRING;
    VERIFY(pthread_cond_signal(&wait_retry_)==0);
  }
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return ret;
}
