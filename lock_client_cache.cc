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
  rlsrpc->reg(lock_test_protocol::disconnect, this, &lock_client_cache::disconnect);

  const char *hname;
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlsrpc->port();
  id = host.str();
  disconnected = false;

  VERIFY(lock_status_[0].state==lock_protocol::NONE);
  VERIFY(pthread_cond_init(&wait_retry_, 0) == 0);
  VERIFY(pthread_cond_init(&wait_release_, 0) == 0);
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  VERIFY(pthread_mutex_lock(&m_)==0);
  lock_protocol::status ret = acquire_wo(lid);
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return ret;
}

//Assumes mutex is held!
lock_protocol::status
lock_client_cache::acquire_wo(lock_protocol::lockid_t lid) {
  if (disconnected) {
    VERIFY(lock_status_[lid].state != lock_protocol::RELEASING);
    VERIFY(lock_status_[lid].state != lock_protocol::FREE_RLS);
  }
  lock_protocol::status ret = lock_protocol::OK;
  bool try_acquire = false;
  while (lock_status_[lid].state != lock_protocol::NONE
         && lock_status_[lid].state != lock_protocol::FREE) {
    tprintf("lock_client_cache(%s:%lu): waiting to acquire lock %llu in state %d\n",
            id.c_str(), pthread_self(), lid, lock_status_[lid].state);
    VERIFY(pthread_cond_wait(&wait_release_, &m_) == 0);
  }

  tprintf("lock_client_cache(%s:%lu): trying acquire of lock %llu in state %d\n",
          id.c_str(),pthread_self(), lid, lock_status_[lid].state);
  //Check state
  lock_protocol::state lis = lock_status_[lid].state;
  if (disconnected && lis == lock_protocol::NONE) {
    //Disconnected, can't acquire lock atm
    return lock_protocol::DISCONNECTED;
  }

  if (lock_status_[lid].stale || lis == lock_protocol::NONE) {
    try_acquire = true;
    lock_status_[lid].state = lock_protocol::ACQUIRING;
  } else if (lis == lock_protocol::FREE) {
    lock_status_[lid].state = lock_protocol::LOCKED;
  }

  while (try_acquire) {
    tprintf("lock_client_cache(%s:%lu): sending acq rpc for lock %llu in state %d\n",
            id.c_str(), pthread_self(), lid, lock_status_[lid].state);
    // do not hold mutex while making rpc call
    VERIFY(pthread_mutex_unlock(&m_)==0);
    lock_protocol::status r;
    r = cl->call(lock_protocol::acquire, lid, id, ret);
    tprintf("lock_client_cache(%s:%lu): got %d back from rpcc, %d back from server\n",
            id.c_str(), pthread_self(), r, ret);
    VERIFY(r == lock_protocol::OK);
    VERIFY(pthread_mutex_lock(&m_)==0);
    if (disconnected || ret == lock_protocol::DISCONNECTED) {
      lock_status_[lid].state = lock_protocol::NONE;
      ret = lock_protocol::DISCONNECTED;
      break;
    }
    try_acquire = false;
    // need to grab (possibly changed) lis
    lis = lock_status_[lid].state;
    tprintf("lock_client_cache(%s:%lu): acquire received %d in state %d for lock %llu\n",
            id.c_str(), pthread_self(), ret, lis, lid);
    if (lis == lock_protocol::ACQUIRING) {
      if (ret == lock_protocol::OK) {
        lock_acquired(lid);
      } else if (ret == lock_protocol::RETRY) {
        //wait for retry_handler invocation
        lock_status_[lid].state = lock_protocol::WAITING;
        while (lock_status_[lid].state == lock_protocol::WAITING) {
          VERIFY(pthread_cond_wait(&wait_retry_, &m_) == 0);
        }
        try_acquire = true;
      } else {
        tprintf("lock_client_cache(%s:%lu):acquire received unexpected error(%d) for lock %llu\n",
                id.c_str(), pthread_self(), ret, lid);
      }
    } else if (lis == lock_protocol::WAITING) {
      if (ret == lock_protocol::OK) {
        lock_acquired(lid);
      } else if (ret == lock_protocol::RETRY) {
        // must have received retry RPC out of order, so just go ahead and retry
        // acq
        try_acquire = true;
        lock_status_[lid].state = lock_protocol::ACQUIRING;
      } else {
        tprintf("lock_client_cache(%s:%lu): acquire received unexpected error(%d) for lock %llu\n",
                id.c_str(), pthread_self(), ret, lid);
      }
    } else if (ret != lock_protocol::OK) {
      tprintf("lock_client_cache(%s:%lu): acquire received unexpected error(%d) for lock %llu in state %d\n",
              id.c_str(), pthread_self(), ret, lid, lis);
    }
  }

  return ret;
}

void
lock_client_cache::lock_acquired(lock_protocol::lockid_t lid)
{
  if (lock_status_[lid].stale) {
    //If our extent is not dirty, we can just unset the stale flag,
    //and use the new extent from the server.
    //Else if the server extent version is the same as local, then we can
    //just use our own extent (we have most recent changes)
    //Else, write/write conflict present, and we must merge!
    if (!lu->exists(lid)) {
      lock_status_[lid].stale = false;
    } else if (!lu->isdirty(lid)) {
      lock_status_[lid].stale = false;
      lu->remove(lid);
      //TODO
      //should we force a get from server here?
    } else if (lu->compareversion(lid)) {
      lock_status_[lid].stale = false;
    } else {
      //TODO
      //create new file with same contents as local conflicting file
      //flush new file
      //replace extent contents of old file with server extent contents
      //resolve conflicts!
      VERIFY(0);
    }
  }
  lock_status_[lid].state = lock_protocol::LOCKED;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
  VERIFY(pthread_mutex_lock(&m_)==0);
  int ret = release_wo(lid);
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return ret;
}

lock_protocol::status
lock_client_cache::release_wo(lock_protocol::lockid_t lid)
{
  int ret = rlock_protocol::OK;
  lock_protocol::state lis = lock_status_[lid].state;
  tprintf("lock_client_cache(%s:%lu): release of lock %llu in state %d\n",
          id.c_str(), pthread_self(), lid, lis);
  if (lis == lock_protocol::LOCKED) {
    if (lock_status_[lid].stale) {
      //force a reload of the extent, even if we don't really need to...
      lock_status_[lid].state = lock_protocol::NONE;
    } else {
      lock_status_[lid].state = lock_protocol::FREE;
    }
    VERIFY(pthread_cond_signal(&wait_release_)==0);
  } else if (lis == lock_protocol::RELEASING) {
    VERIFY(!lock_status_[lid].stale);
    VERIFY(!disconnected);
    lu->dorelease(lid);
    VERIFY(pthread_mutex_unlock(&m_)==0);
    // release lock to lock server
    lock_protocol::status r;
    r = cl->call(lock_protocol::release, lid, id, ret);
    VERIFY (r == lock_protocol::OK);
    VERIFY(pthread_mutex_lock(&m_)==0);
    if (ret != lock_protocol::OK) {
      tprintf("lock_client_cache(%s:%lu): rls->rls Received unexpected error(%d) for lock %llu\n",
              id.c_str(), pthread_self(), ret, lid);
    }
    lock_status_[lid].state = lock_protocol::NONE;
    VERIFY(pthread_cond_signal(&wait_release_)==0);
  } else {
    tprintf("lock_client_cache(%s:%lu): unhandled state (%d) in rls for lock %llu\n",
            id.c_str(), pthread_self(), lis, lid);
    VERIFY(0);
  }

  return lock_protocol::OK;
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, int &r)
{
  r = rlock_protocol::OK;
  VERIFY(pthread_mutex_lock(&m_)==0);
  if (disconnected) {
    r = rlock_protocol::DISCONNECTED;
    VERIFY(pthread_mutex_unlock(&m_)==0);
    return rlock_protocol::DISCONNECTED;
  }
  lock_protocol::state lis = lock_status_[lid].state;
  tprintf("lock_client_cache(%s:%lu): received revoke of lock %llu in state %d\n",
          id.c_str(), pthread_self(), lid, lis);
  //if we receive a revoke for a lock we thought might be stale, we know it
  //can't be because the server still thinks we own the lock.
  if (lock_status_[lid].stale) {
    tprintf("lock_client_cache(%s:%lu): received revoke of lock %llu in state %d while marked stale...\n",
            id.c_str(), pthread_self(), lid, lis);
    lock_status_[lid].stale = false;
  }

  switch(lis) {
    case lock_protocol::ACQUIRING:
    case lock_protocol::WAITING:
    case lock_protocol::LOCKED:
      {
        lock_status_[lid].state = lock_protocol::RELEASING;
        break;
      }
    case lock_protocol::FREE:
      {
        lock_status_[lid].state = lock_protocol::FREE_RLS;
        lu->dorelease(lid);

        VERIFY(pthread_mutex_unlock(&m_)==0);
        // release lock to lock server
        lock_protocol::status ret;
        ret = cl->call(lock_protocol::release, lid, id, r);
        VERIFY(ret == lock_protocol::OK);
        VERIFY(pthread_mutex_lock(&m_)==0);
        if (r != lock_protocol::OK) {
          tprintf("lock_client_cache(%s:%lu): revoke->rls Received unexpected error(%d) for lock %llu\n",
                  id.c_str(), pthread_self(), r, lid);
        }
        lock_status_[lid].state = lock_protocol::NONE;
        VERIFY(pthread_cond_signal(&wait_release_)==0);
        break;
      }
    default:
      tprintf("lock_client_cache(%s:%lu): unexpected state (%d) in revoke!\n",
              id.c_str(), pthread_self(), lis);
  }

  VERIFY(pthread_mutex_unlock(&m_)==0);
  return rlock_protocol::OK;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, int &r)
{
  r = rlock_protocol::OK;
  VERIFY(pthread_mutex_lock(&m_)==0);
  if (disconnected) {
    r = rlock_protocol::DISCONNECTED;
    VERIFY(pthread_mutex_unlock(&m_)==0);
    return rlock_protocol::DISCONNECTED;
  }
  lock_protocol::state lis = lock_status_[lid].state;
  tprintf("lock_client_cache(%s:%lu): received retry of lock %llu in state %d\n",
          id.c_str(), pthread_self(), lid, lis);
  //Check state
  if (lis == lock_protocol::ACQUIRING) {
    lock_status_[lid].state = lock_protocol::WAITING;
  } else if (lis == lock_protocol::WAITING) {
    lock_status_[lid].state = lock_protocol::ACQUIRING;
    VERIFY(pthread_cond_signal(&wait_retry_)==0);
  }
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return rlock_protocol::OK;
}

lock_test_protocol::status
lock_client_cache::disconnect(bool kill, int &r)
{
  r = lock_test_protocol::OK;
  VERIFY(pthread_mutex_lock(&m_)==0);
  tprintf("lock_client_cache(%s:%lu): received disconnect rpc %d\n",
          id.c_str(), pthread_self(), kill);
  if (disconnected != kill) {
    std::map<lock_protocol::lockid_t, lockstate>::iterator i;
    if (kill) {
      int ret = disconnect_server();
      //TODO: On disconnect: Any lock we have in state RELEASING -> LOCKED
      //                     Any lock we have in state FREE_RLS -> NONE (extent is invalidated)
      //                     What about states ACQUIRING and WAITING??
      for (i = lock_status_.begin(); i!=lock_status_.end(); i++) {
        lock_protocol::state lis = i->second.state;
        if (lis == lock_protocol::RELEASING) {
          lock_status_[i->first].state = lock_protocol::LOCKED;
        } else if (lis == lock_protocol::FREE_RLS) {
          lock_status_[i->first].state = lock_protocol::NONE;
        } else if (lis == lock_protocol::WAITING) {
          lock_status_[i->first].state = lock_protocol::ACQUIRING;
        }
      }
      VERIFY(pthread_cond_signal(&wait_retry_)==0);
    } else {
      //TODO: On reconnect: Any locks not in state NONE, try to reacquire
      for (i = lock_status_.begin(); i!=lock_status_.end(); i++) {
        lock_protocol::state lis = i->second.state;
        VERIFY(lis != lock_protocol::FREE_RLS);
        VERIFY(lis != lock_protocol::RELEASING);
        VERIFY(lis != lock_protocol::WAITING);
        VERIFY(lis != lock_protocol::ACQUIRING);
        if (lis != lock_protocol::NONE) {
          lock_status_[i->first].stale = true;
          VERIFY(acquire_wo(i->first)==lock_protocol::OK);
          VERIFY(release_wo(i->first)==lock_protocol::OK);
        }
      }
    }
    disconnected = kill;
  }
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return lock_test_protocol::OK;
}

lock_test_protocol::status
lock_client_cache::disconnect_server()
{
  lock_test_protocol::status ret = lock_test_protocol::OK;
  VERIFY(cl->call(lock_test_protocol::disconnect_server,id,ret)==lock_test_protocol::OK);
  return lock_test_protocol::OK;
}
