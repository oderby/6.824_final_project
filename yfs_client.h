#ifndef yfs_client_h
#define yfs_client_h

#include <string>
#include <vector>
//#include "yfs_protocol.h"
#include "lang/verify.h"
#include "extent_client_cache.h"

#include "lock_protocol.h"
#include "lock_client_cache.h"

class yfs_dir;

class yfs_client {
  extent_client_cache *ec;
  lock_client_cache *lc;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, EXIST, DISCONNECTED };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
    unsigned long version;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
    unsigned long version;
  };
  struct dirent {
    std::string name;
    yfs_client::inum inum;
  };

 private:
  static status ext2yfs(extent_protocol::status);
 public:

  yfs_client(std::string, std::string);

  static std::string filename(inum);
  static inum n2i(std::string);
  bool isfile(inum);
  bool isdir(inum);

  int getfile(inum, fileinfo &);
  status getfile_helper(inum, fileinfo &);
  int getdir(inum, dirinfo &);

  status create(inum, const char*, inum &);
  status mkdir(inum, const char*, inum&);
  status unlink(inum, const char*);
  status lookup(inum, const char*, inum &);
  status getdir_contents(inum, yfs_dir**);
  status setattr(inum, unsigned int);
  status read(inum, unsigned int, unsigned int, std::string &);
  status write(inum, const char *, unsigned int, unsigned int);

};

// simple class to provide nice ways of modifying directory strings

class yfs_dir {
 public:
  std::map<std::string, yfs_client::inum> dir_;

  yfs_client::dirent extract_dirent(std::string);


  yfs_dir(void) {};

  yfs_dir(std::string);

  std::string to_string(void);
  void add(yfs_client::dirent);
  void rem(std::string);
  bool exists(std::string);
  yfs_client::inum get(std::string);
};

struct ScopedRemoteLock {
 private:
  lock_protocol::lockid_t ino_;
  lock_client* lc_;
 public:
  ScopedRemoteLock(lock_client* lc, yfs_client::inum ino): lc_(lc) {
    ino_ = ino;
    //lc_ = lc;
    VERIFY(lc_->acquire(ino_)==0);
  }
  ~ScopedRemoteLock() {
    VERIFY(lc_->release(ino_)==0);
  }
};

#endif
