// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
//#include "extent_client.h"
//#include "lock_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client_cache(extent_dst);
  extent_user* eu = new extent_user(ec);
  lc = new lock_client_cache(lock_dst, eu);

  // "create" root directory with inum 0x1
  yfs_dir dir("tmp 2:");//new yfs_dir(". 1:.. 1:");
  lc->acquire(1);
  std::string root_dir;
  if (ec->get(1, root_dir) == extent_protocol::NOENT) {
    lc->acquire(2);
    ec->put(1, dir.to_string());
    // create tmp dir
    yfs_dir tmp_dir(":");
    ec->put(2, tmp_dir.to_string());
    lc->release(2);
  }
  lc->release(1);
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

yfs_client::status
yfs_client::ext2yfs(extent_protocol::status r) {
  if (r == extent_protocol::OK) {
    return yfs_client::OK;
  } else if (r == extent_protocol::NOENT) {
    return yfs_client::NOENT;
  } else if (r == extent_protocol::RPCERR) {
    return yfs_client::RPCERR;
  }
  return yfs_client::IOERR;
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
yfs_client::getfile(inum ino, fileinfo &fin)
{
  //int r = OK;
  // You modify this function for Lab 3
  // - hold and release the file lock

  lc->acquire(ino);
  return getfile_helper(ino, fin);
  lc->release(ino);
}

int
yfs_client::getfile_helper(inum ino, fileinfo &fin)
{
  printf("getfile %s\n", yfs_client::filename(ino).c_str());
  extent_protocol::attr a;
  status ret = ext2yfs(ec->getattr(ino, a));

  if (ret == OK) {
    fin.atime = a.atime;
    fin.mtime = a.mtime;
    fin.ctime = a.ctime;
    fin.size = a.size;
    fin.version = a.version;
    printf("getfile %s -> sz %llu\n", yfs_client::filename(ino).c_str(), fin.size);
  }
  return ret;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  printf("getdir %s\n", yfs_client::filename(inum).c_str());
  extent_protocol::attr a;
  lc->acquire(inum);
  status ret = ext2yfs(ec->getattr(inum, a));

  if (ret == OK) {
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;
  }

  lc->release(inum);
  return ret;
}

yfs_client::inum
get_rand_num(void)
{
  return (rand()<<16|rand()) & 0xFFFF;
}

// - If a file named @name already exists in @parent, return EXIST.
// - Pick an ino (with type of yfs_client::inum) for file @name.
//   Make sure ino indicates a file, not a directory!
// - Create an empty extent for ino.
// - Add a <name, ino> entry into @parent.
yfs_client::status
yfs_client::create(inum p_ino, const char* name, inum& new_ino)
{
  if (lc->acquire(p_ino)==lock_protocol::DISCONNECTED) {
    return yfs_client::DISCONNECTED;
  }

  // make sure parent directory exists
  std::string p_dir_str;
  status ret = ext2yfs(ec->get(p_ino, p_dir_str));
  if (ret != OK) {
    lc->release(p_ino);
    return ret;
  }
  yfs_dir p_dir(p_dir_str);
  dirent d;
  // ensure we don't already have a file with the same name in this directory
  d.name = std::string(name);
  if (p_dir.exists(d.name)) {
    lc->release(p_ino);
    return EXIST;
  }

  // generate a inum for new file
  std::string dummy;
  while (true) {
    new_ino = get_rand_num() | 0x80000000;
    lc->acquire((lock_protocol::lockid_t) new_ino);
    ret = ext2yfs(ec->get(new_ino, dummy));
    if (ret == NOENT) //keep lock, break out
      break;
    printf("rofl conflict on new ino %llu\n",new_ino);
    lc->release((lock_protocol::lockid_t) new_ino);
  }
  //printf("new_ino: %s %llX\n", filename(new_ino).c_str(), new_ino);
  d.inum = new_ino;

  // create empty extent for new file
  ret = ext2yfs(ec->put(new_ino, ""));
  if (ret == OK) {
    ret = ext2yfs(ec->rename(new_ino, d.name));
    if (ret == OK) {
      // add new file to parent directory
      p_dir.add(d);
      ret = ext2yfs(ec->put(p_ino, p_dir.to_string()));
      if (ret != OK) {
        //try to remove the file extent if we failed to update the parent
        ec->remove(new_ino);
      }
    }
  }
  lc->release((lock_protocol::lockid_t) new_ino);
  lc->release(p_ino);
  return ret;
}

yfs_client::status
yfs_client::mkdir(inum p_ino, const char* name, inum& new_ino)
{
    if (lc->acquire(p_ino)==lock_protocol::DISCONNECTED) {
    return yfs_client::DISCONNECTED;
  }
  // make sure parent directory exists
  std::string p_dir_str;
  status ret = ext2yfs(ec->get(p_ino, p_dir_str));
  if (ret != OK) {
    lc->release(p_ino);
    return ret;
  }
  yfs_dir p_dir(p_dir_str);
  dirent d;
  // ensure we don't already have a file with the same name in this directory
  d.name = std::string(name);
  if (p_dir.exists(d.name)) {
    lc->release(p_ino);
    return EXIST;
  }

  // generate a inum for new file
  std::string dummy;
  while (true) {
    new_ino = get_rand_num();
    lc->acquire((lock_protocol::lockid_t) new_ino);
    ret = ext2yfs(ec->get(new_ino, dummy));
    if (ret == NOENT) //keep lock, break out
      break;
    printf("rofl conflict on new dir ino %llu\n",new_ino);
    lc->release((lock_protocol::lockid_t) new_ino);
  }
  //printf("new_dir_ino: %s %llX\n", filename(new_ino).c_str(), new_ino);
  d.inum = new_ino;

  // create empty extent for new file
  ret = ext2yfs(ec->put(new_ino, ":"));
  if (ret == OK) {
    // add new file to parent directory
    p_dir.add(d);
    ret = ext2yfs(ec->put(p_ino, p_dir.to_string()));
    if (ret != OK) {
      //try to remove the file extent if we failed to update the parent
      ec->remove(new_ino);
    }
  }
  lc->release((lock_protocol::lockid_t) new_ino);
  lc->release(p_ino);
  return ret;
}

yfs_client::status
yfs_client::unlink(inum p_ino, const char* name)
{
  // make sure parent directory exists
  std::string p_dir_str;
    if (lc->acquire(p_ino)==lock_protocol::DISCONNECTED) {
    return yfs_client::DISCONNECTED;
  }
  printf("yfs_client::unlink %s %s\n",filename(p_ino).c_str(), name);
  status ret = ext2yfs(ec->get(p_ino, p_dir_str));
  if (ret != OK) {
    lc->release(p_ino);
    return ret;
  }
  yfs_dir p_dir(p_dir_str);
  printf("yfs_client::unlink dir exists\n");

  // ensure the file exists in the directory
  std::string f_name(name);
  if (!p_dir.exists(f_name)) {
    printf("file %s doesn't exist in dir %s!\n", name, p_dir_str.c_str());
    lc->release(p_ino);
    return NOENT;
  }

  inum f_ino = p_dir.get(f_name);
  if (isdir(f_ino)) {
    lc->release(p_ino);
    return IOERR;
  }
  if (lc->acquire(f_ino)==lock_protocol::DISCONNECTED) {
    lc->release(p_ino);
    return yfs_client::DISCONNECTED;
  }

  //attempt to remove file entry from directory
  p_dir.rem(f_name);
  ret = ext2yfs(ec->put(p_ino, p_dir.to_string()));
  if (ret != OK) {
    printf("unlink: error updating dir %s for file %s - %d\n", p_dir_str.c_str(), name, ret);
    lc->release(f_ino);
    lc->release(p_ino);
    return ret;
  }
  //try to remove the file extent
  lc->release(f_ino);
  lc->release(p_ino);
  return ext2yfs(ec->remove(f_ino));
}

yfs_client::status
yfs_client::lookup(inum p_ino, const char* name, inum& f_ino)
{
  // make sure parent directory exists
  std::string p_dir_str;
  if (lc->acquire(p_ino)==lock_protocol::DISCONNECTED) {
    return yfs_client::DISCONNECTED;
  }
  status ret = ext2yfs(ec->get(p_ino, p_dir_str));
  if (ret != OK) {
    lc->release(p_ino);
    return ret;
  }
  yfs_dir p_dir(p_dir_str);
  // ensure file exists
  std::string f_name(name);
  if (!p_dir.exists(f_name)) {
    lc->release(p_ino);
    return NOENT;
  }
  f_ino = p_dir.get(f_name);
  lc->release(p_ino);
  return OK;
}

yfs_client::status
yfs_client::getdir_contents(inum ino, yfs_dir** dir)
{
  //int r = OK;
  // You modify this function for Lab 3
  // - hold and release the directory lock

  if (lc->acquire(ino)==lock_protocol::DISCONNECTED) {
    return yfs_client::DISCONNECTED;
  }
  std::string dir_str;
  status ret = ext2yfs(ec->get(ino, dir_str));
  if (ret == OK) {
    printf("getdir_contents found dir %s\n",dir_str.c_str());
    *dir = new yfs_dir(dir_str);
  }
  lc->release(ino);
  return ret;
}

yfs_client::status
yfs_client::setattr(inum ino, unsigned int new_size)
{
    if (lc->acquire(ino)==lock_protocol::DISCONNECTED) {
    return yfs_client::DISCONNECTED;
  }
  printf("yfs_client::setattr %s->%u\n",yfs_client::filename(ino).c_str(), new_size);
  std::string f_data;
  status ret = ext2yfs(ec->get(ino, f_data));
  if (ret != OK) {
    lc->release(ino);
    return ret;
  }

  fileinfo fin;
  ret = getfile_helper(ino, fin);
  if (ret != OK) {
    lc->release(ino);
    return ret;
  }

  unsigned int data_size = fin.size;//f_data.size()*sizeof(char);

  if (new_size == 0) {
    ret = ext2yfs(ec->put(ino, ""));
  } else if (new_size < data_size) {
    // shrink data
    ret = ext2yfs(ec->put(ino, f_data.substr(0,new_size)));
  } else if (new_size > data_size) {
    // pad data out to larger size
    unsigned int extra = new_size-data_size;
    while (extra-- > 0) {
      f_data+='\0';
    }
    ret = ext2yfs(ec->put(ino, f_data));
  }
  lc->release(ino);
  return ret;
}

yfs_client::status
yfs_client::read(inum ino, unsigned int off, unsigned int size, std::string & buf)
{
    if (lc->acquire(ino)==lock_protocol::DISCONNECTED) {
    return yfs_client::DISCONNECTED;
  }
  printf("yfs_client::read %u %u %s\n",off, size, yfs_client::filename(ino).c_str());
  std::string f_data;
  status ret = ext2yfs(ec->get(ino, f_data));
  if (ret != OK) {
    lc->release(ino);
    return ret;
  }
  // if @off is as big as file, read zero bytes
  if (off<f_data.length()) {
    buf.assign(f_data,off,size);
  }
  printf("read %s from %s\n",buf.c_str(),yfs_client::filename(ino).c_str());
  lc->release(ino);
  return OK;
}

yfs_client::status
yfs_client::write(inum ino, const char * buf, unsigned int off, unsigned int size)
{
  if (lc->acquire(ino)==lock_protocol::DISCONNECTED) {
    return yfs_client::DISCONNECTED;
  }

  printf("yfs_client::write %u %u %s->%s\n",off, size, buf,yfs_client::filename(ino).c_str());
  std::string f_data;
  // get the current file
  status ret = ext2yfs(ec->get(ino, f_data));
  if (ret != OK) {
    lc->release(ino);
    return ret;
  }

  //build up the new string (take the parts we want to keep from the old string,
  //but insert the new part we want to write)
  std::string new_data = f_data.substr(0,off);
  if (off > f_data.length()) {
    new_data.append(off-f_data.length(), '\0');
  }
  new_data.append(buf, size);
  if (new_data.length() < f_data.length()) {
    new_data.append(f_data, new_data.length(), std::string::npos);
  }
  //printf("new_str=%s\n",new_data.c_str());
  //printf("orig_str=%s\n",f_data.c_str());

  // send the new data back to disk
  ret = ext2yfs(ec->put(ino, new_data));
  lc->release(ino);
  return ret;
}

// parse string of directory content
yfs_dir::yfs_dir(std::string dir){
  if (dir.length() > 1) { //empty dir specified by ":"
    //TODO: better separating character than ':' and " " (see extract_dirent)
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
    printf("yfs_dir: initialized empty dir!\n");
  }
}

// parse string of dirent tuple
yfs_client::dirent
yfs_dir::extract_dirent(std::string str) {
  //printf("told to extract dir entry from %s\n", str.c_str());
  size_t found = str.find(" ");
  yfs_client::dirent dir;
  dir.name = str.substr(0,found);
  dir.inum = yfs_client::n2i(str.substr(found).c_str());
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
    for (it = dir_.begin(); it!= dir_.end(); it++) {
      str_out += it->first;
      str_out += " ";
      str_out += yfs_client::filename(it->second);
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

bool
yfs_dir::exists(std::string name) {
  return dir_.count(name)==1;
}

yfs_client::inum
yfs_dir::get(std::string name) {
  return dir_[name];
}
