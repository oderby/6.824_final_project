// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extent_server::extent_server()
{
  VERIFY(pthread_mutex_init(&m_, 0) == 0);
}


int extent_server::put(extent_protocol::extentid_t id,unsigned int version, std::string buf, int &r)
{
  printf("extent_server: received put request of %llu %s\n",id, buf.c_str());
  extent_server::finfo f;
  f.buf = buf;
  time_t seconds;
  seconds = time(NULL);
  f.a.ctime = seconds;
  f.a.mtime = seconds;
  f.a.size = buf.size()*sizeof(char);


  VERIFY(pthread_mutex_lock(&m_)==0);

  //if entry exists...
  if (table_.find(id)!=table_.end()) {
    VERIFY(version == table_[id].a.version);
    f.a.version = version+1;
  }

  //else creating new file. set to version 1
  else {
    f.a.version = 1;
  }
    
  table_[id] = f;
  VERIFY(pthread_mutex_unlock(&m_)==0);
  r = extent_protocol::OK;
  return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  VERIFY(pthread_mutex_lock(&m_)==0);
  printf("extent_server: received get request of %llu\n",id);
  if (table_.count(id) == 0) {
    // if id does not exist, return error
    VERIFY(pthread_mutex_unlock(&m_)==0);
    return extent_protocol::NOENT;
  }
  extent_server::finfo f = table_[id];
  time_t seconds;
  seconds = time(NULL);
  f.a.atime = seconds;
  buf.assign(f.buf);
  printf("extent_server: served get request of %llu %s\n",id, buf.c_str());
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return extent_protocol::OK;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  printf("extent_server: received getattr request of %llu\n",id);
  VERIFY(pthread_mutex_lock(&m_)==0);
  if (table_.count(id) == 0) {
    // if id does not exist, return error
    VERIFY(pthread_mutex_unlock(&m_)==0);
    return extent_protocol::NOENT;
  }
  extent_server::finfo f = table_[id];
  a.size = f.a.size;
  a.atime = f.a.atime;
  a.mtime = f.a.mtime;
  a.ctime = f.a.ctime;
  a.version = f.a.version;
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  printf("extent_server: received removu request of %llu\n",id);
  VERIFY(pthread_mutex_lock(&m_)==0);
  table_.erase(id);
  VERIFY(pthread_mutex_unlock(&m_)==0);
  return extent_protocol::OK;
}
