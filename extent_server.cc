// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extent_server::extent_server() {}


int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
  extent_server::finfo f;
  f.name = buf;
  time_t seconds;
  seconds = time(NULL);
  f.a.ctime = seconds;
  f.a.mtime = seconds;
  VERIFY(pthread_mutex_lock(&m_)==0);
  table_[id] = f;
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  VERIFY(pthread_mutex_lock(&m_)==0);
  if (table_.count(id) == 0) {
    // if id does not exist, return error
    return extent_protocol::NOENT;
  }
  extent_server::finfo f = table_[id];
  time_t seconds;
  seconds = time(NULL);
  f.a.atime = seconds;
  buf = f.name;
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return extent_protocol::OK;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  VERIFY(pthread_mutex_lock(&m_)==0);
  if (table_.count(id) == 0) {
    // if id does not exist, return error
    return extent_protocol::NOENT;
  }
  extent_server::finfo f = table_[id];
  a = f.a;
  /*
  a.size = f.a.size;
  a.atime = f.atime;
  a.mtime = f.mtime;
  a.ctime = f.ctime;
  */
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  VERIFY(pthread_mutex_lock(&m_)==0);
  table_.erase(id);
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return extent_protocol::OK;
}
