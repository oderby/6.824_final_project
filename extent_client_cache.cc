// RPC stubs for clients to talk to extent_server

#include "extent_client_cache.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "slock.h"

// The calls assume that the caller holds a lock on the extent

extent_client_cache::extent_client_cache(std::string dst) :
    extent_client(dst)
{
  VERIFY(pthread_mutex_init(&m_, 0) == 0);
}

extent_protocol::status
extent_client_cache::get(extent_protocol::extentid_t eid, std::string &buf)
{
  ScopedLock ml(&m_);
  //do we have a cached copy?
  if (local_extent_.count(eid)==0) {
    extent_protocol::status ret = extent_protocol::OK;
    if ((ret = get_helper(eid)) == extent_protocol::NOENT) {
      return ret;
    }
  }
  time_t seconds;
  seconds = time(NULL);
  local_extent_[eid].attr.atime = seconds;
  buf.assign(local_extent_[eid].extent);
  return extent_protocol::OK;
}

extent_protocol::status
extent_client_cache::get_helper(extent_protocol::extentid_t eid)
{
  extent_entry ee;
  ee.dirty = false;
  extent_protocol::status ret = extent_protocol::OK;
  ret = cl->call(extent_protocol::get, eid, ee.extent);
  if (ret == extent_protocol::NOENT) {
    printf("extent_client_cache: tried to get %llu, NOENT!\n",eid);
    return ret;
  }
  VERIFY(ret == extent_protocol::OK);
  printf("extent_client_cache: got %llu with extent %s\n",
         eid, ee.extent.c_str());
  VERIFY(cl->call(extent_protocol::getattr, eid, ee.attr)==extent_protocol::OK);
  local_extent_[eid] = ee;
  return ret;
}

extent_protocol::status
extent_client_cache::getattr(extent_protocol::extentid_t eid,
		       extent_protocol::attr &attr)
{
  ScopedLock ml(&m_);
  if(local_extent_.count(eid)==0) {
    extent_protocol::status ret = extent_protocol::OK;
    if ((ret = get_helper(eid)) == extent_protocol::NOENT) {
      return ret;
    }
    VERIFY(ret == extent_protocol::OK);
  }
  attr = local_extent_[eid].attr;
  printf("version on getattr: %d\n",attr.version);
  return extent_protocol::OK;
}

extent_protocol::status
extent_client_cache::put(extent_protocol::extentid_t eid, std::string buf)
{
  extent_entry ee;
  time_t seconds;
  seconds = time(NULL);
  ee.dirty = true;
  ee.extent = buf;
  ee.attr.ctime = seconds;
  ee.attr.mtime = seconds;
  ee.attr.size = buf.size()*sizeof(char);

  if(local_extent_.count(eid)==0) {
    ee.attr.version = 0;
  } else {
    ee.attr.version = local_extent_[eid].attr.version;
  }


  ScopedLock ml(&m_);
  local_extent_[eid] = ee;
  printf("extent_client_cache: put %s for %llu\n",
         buf.c_str(), eid);
  return extent_protocol::OK;
}

extent_protocol::status
extent_client_cache::remove(extent_protocol::extentid_t eid)
{
  ScopedLock ml(&m_);
  VERIFY(local_extent_.erase(eid)==1);
  return extent_protocol::OK;
}

extent_protocol::status
extent_client_cache::flush(extent_protocol::extentid_t eid)
{
  extent_protocol::status r = extent_protocol::OK;
  ScopedLock ml(&m_);
  printf("extent_client_cache: flushing %llu\n",eid);
  //VERIFY(pthread_mutex_lock(&m_)==0);
  if(local_extent_.count(eid)==0) {
    printf("extent_client_cache: flush: %llu has been deleted, removing from server\n"
           ,eid);
    //VERIFY(pthread_mutex_unlock(&m_)==0);
    VERIFY(cl->call(extent_protocol::remove, eid, r)==extent_protocol::OK);
    return r;
  }
  if (local_extent_[eid].dirty) {
    printf("extent_client_cache: flush: %llu has been modified, putting %s on server\n"
           ,eid, local_extent_[eid].extent.c_str());
    VERIFY(cl->call(extent_protocol::put, eid,local_extent_[eid].attr.version, local_extent_[eid].extent, r)
           ==extent_protocol::OK);
  }
  VERIFY(local_extent_.erase(eid)==1);
  //VERIFY(pthread_mutex_unlock(&m_)==0);
  return r;
}

bool
extent_client_cache::exists(extent_protocol::extentid_t eid)
{
  ScopedLock ml(&m_);
  return local_extent_.count(eid)>0;
}

bool
extent_client_cache::is_dirty(extent_protocol::extentid_t eid)
{
  ScopedLock ml(&m_);
  return local_extent_[eid].dirty;
}

bool
extent_client_cache::compare_version(extent_protocol::extentid_t eid)
{
  ScopedLock ml(&m_);
  extent_entry ee;
  int ret;
  ret = cl->call(extent_protocol::getattr, eid, ee.attr);
  if (ret==extent_protocol::NOENT) {
    if (local_extent_[eid].attr.version==0) {
      return true;
    } else {
      return false;
    }
  }
  VERIFY(ret==extent_protocol::OK);
  VERIFY(ee.attr.version >= local_extent_[eid].attr.version);
  return ee.attr.version == local_extent_[eid].attr.version;
}

extent_user::extent_user(extent_client_cache* ec):
    ec_(ec)
{}

void
extent_user::dorelease(lock_protocol::lockid_t lid)
{
  ec_->flush((extent_protocol::extentid_t) lid);
}

bool
extent_user::exists(lock_protocol::lockid_t lid)
{
  return ec_->exists((extent_protocol::extentid_t) lid);
}

bool
extent_user::isdirty(lock_protocol::lockid_t lid)
{
  return ec_->is_dirty((extent_protocol::extentid_t) lid);
}

void
extent_user::remove(lock_protocol::lockid_t lid)
{
  ec_->remove((extent_protocol::extentid_t) lid);
}

bool
extent_user::compareversion(lock_protocol::lockid_t lid)
{
  return ec_->compare_version((extent_protocol::extentid_t) lid);
}
