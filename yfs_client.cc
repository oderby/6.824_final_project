// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  // "create" root directory with inum 0x1
  yfs_dir* dir = new yfs_dir("lala 2:po 3:tree 32124:");
  dir->rem("tree");
  yfs_client::dirent d;
  d.name = "tester";
  d.inum = 12453;
  dir->add(d);
  dir->rem("trial");
  dir->add(d);
  printf("%s\n",dir->to_string().c_str());
  ec->put(100,dir->to_string());
  ec->remove(100);
  dir = new yfs_dir(". 1:.. 1:");
  ec->put(1, dir->to_string());
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;

  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;

  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}

// parse string of directory content
yfs_dir::yfs_dir(std::string dir){
  if (dir.length() > 1) { //empty dir specified by ":"
    size_t found = dir.find(":");
    size_t last_found = 0;
    yfs_client::dirent dirent;
    while (found != std::string::npos) {
      dirent = yfs_dir::extract_dirent(dir.substr(last_found, found-last_found));
      dir_[dirent.name] = dirent.inum;
      last_found = found+1;
      found = dir.find(":", last_found);
    }
    //dirent = yfs_dir::extract_dirent(dir.substr(last_found));
    //dir_[dirent.name] = dirent.inum;
  } else {
    printf("yfs_dir: initialized empty dir!");
  }
}

// parse string of dirent tuple
yfs_client::dirent
yfs_dir::extract_dirent(std::string str) {
  printf("told to extract dir entry from %s\n", str.c_str());
  size_t found = str.find(" ");
  yfs_client::dirent dir;
  dir.name = str.substr(0,found);
  dir.inum = atoi(str.substr(found).c_str());
  return dir;
}

// return string of dir structure
std::string
yfs_dir::to_string(void) {
  std::string str_out;
  if (dir_.empty()) {
    str_out = std::string(":");
  } else {
    std::map<std::string, yfs_client::inum>::iterator it;
    char buffer[50];
    int n;
    for (it = dir_.begin(); it!= dir_.end(); it++) {
      str_out += it->first;
      str_out += " ";
      n = sprintf(buffer,"%lld",it->second);
      str_out += buffer;
      str_out += ":";
    }
  }
  return str_out;
}

void
yfs_dir::add(yfs_client::dirent dir) {
  dir_[dir.name] = dir.inum;
}

void yfs_dir::rem(std::string name) {
  dir_.erase(name);
}
