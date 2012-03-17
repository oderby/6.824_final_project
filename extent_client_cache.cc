// RPC stubs for clients to talk to extent_server

#include "extent_client_cache.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// The calls assume that the caller holds a lock on the extent

extent_client_cache::extent_client_cache(std::string dst) :
    extent_client(dst)
{}

extent_protocol::status
extent_client_cache::get(extent_protocol::extentid_t eid, std::string &buf)
{
  //do we have a cached copy?
  if (local_extent_.count(eid)==0) {

    extent_entry ee;
    ee.dirty = false;
    extent_protocol::status ret = extent_protocol::OK;
    ret = cl->call(extent_protocol::get, eid, ee.extent);
    if (ret == extent_protocol::NOENT) {
      return ret;
    }
    VERIFY(ret == extent_protocol::OK);
    VERIFY(cl->call(extent_protocol::getattr, eid, ee.attr)==extent_protocol::OK);
    local_extent_[eid] = ee;
  }
  time_t seconds;
  seconds = time(NULL);
  local_extent_[eid].attr.atime = seconds;
  buf.assign(local_extent_[eid].extent);
  return extent_protocol::OK;
}

extent_protocol::status
extent_client_cache::getattr(extent_protocol::extentid_t eid,
		       extent_protocol::attr &attr)
{
  //VERIFY(cl->call(extent_protocol::getattr, eid, attr)==extent_protocol::OK);
  if(local_extent_.count(eid)==0) {
    return extent_protocol::NOENT;
  }
  attr = local_extent_[eid].attr;
  return extent_protocol::OK;
}

extent_protocol::status
extent_client_cache::put(extent_protocol::extentid_t eid, std::string buf)
{
  //extent_protocol::status r = extent_protocol::OK;
  //VERIFY(cl->call(extent_protocol::put, eid, buf, r)==extent_protocol::OK);
  //return r;
  extent_entry ee;
  time_t seconds;
  seconds = time(NULL);
  ee.dirty = true;
  ee.extent = buf;
  ee.attr.ctime = seconds;
  ee.attr.mtime = seconds;
  ee.attr.size = buf.size()*sizeof(char);
  local_extent_[eid] = ee;
  return extent_protocol::OK;
}

extent_protocol::status
extent_client_cache::remove(extent_protocol::extentid_t eid)
{
  //extent_protocol::status r = extent_protocol::OK;
  //VERIFY(cl->call(extent_protocol::remove, eid, r)==extent_protocol::OK);
  //return r;
  VERIFY(local_extent_.erase(eid)==1);
  return extent_protocol::OK;
}
