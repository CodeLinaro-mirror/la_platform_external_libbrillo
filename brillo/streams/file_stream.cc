// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
// Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <brillo/streams/file_stream.h>

#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/errors/error_codes.h>
#include <brillo/message_loops/message_loop.h>
#include <brillo/streams/stream_errors.h>
#include <brillo/streams/stream_utils.h>

#if USE_MTD
#define SYSTEM_UBI_MTD_NUM  "/sys/class/ubi/ubi0/mtd_num"
#endif

namespace brillo {

// FileDescriptor is a helper class that serves two purposes:
// 1. It wraps low-level system APIs (as FileDescriptorInterface) to allow
//    mocking calls to them in tests.
// 2. It provides file descriptor watching services using FileDescriptorWatcher
//    and MessageLoopForIO::Watcher interface.
// The real FileStream uses this class to perform actual file I/O on the
// contained file descriptor.
class FileDescriptor : public FileStream::FileDescriptorInterface {
 public:
#if USE_MTD
#if USE_TELAF
  FileDescriptor(int fd, bool own, bool mtd, bool ubi) : fd_{fd}, own_{own}, mtd_{mtd}, ubi_{ubi} {
    pages_read_ = 0;
    if(mtd_){
      mtd_info_t *mtd_info;
      mtd_info = (mtd_info_t *) malloc(sizeof(mtd_info_t));
      if (mtd_info == NULL){
        LOG(ERROR) << " FileStream::  mtd_info memory allocation failure  ";
      } else {
        int ret = telaf_mtd_information(mtd_info);
        if (ret != 0){
          LOG(ERROR) << " FileStream::  get mtd info error  ";
          free (mtd_info);
        } else {
          total_blocks_number_ =  mtd_info->blk_num;
          bad_blocks_number_ = mtd_info->bad_blk_num;
          erase_size_ = mtd_info->erase_size;
          write_size_ = mtd_info->write_size;
        }
      }
    } else if (ubi_) {
      LOG(INFO) << " FileStream:: get ubi info and fill info in ubi class members ";
    }
  }
#else
  FileDescriptor(int fd, bool own, nad_mtd_hndl_t * mtd_hndl, bool mtd) : fd_{fd}, own_{own}, mtd_hndl_{mtd_hndl}, mtd_{mtd} {
    if (mtd_hndl_ != nullptr){
      LOG(INFO) << "FileDescriptor 1.9 mtd_hndl_" << mtd_hndl_->info.writesize;
      ubi_ = false;
    } else {
      LOG(INFO) << "FileDescriptor 1.9 mtd_hndl_ is null ";
      mtd_ = false;
      ubi_ = false;
    }
    pages_read_ = 0;
    LOG(INFO) << "FileDescriptor 1.9 pages_read_ " << pages_read_;
  }
  FileDescriptor(int fd, bool own, nad_ubi_hndl_t * ubi_hndl, bool ubi) : fd_{fd}, own_{own}, ubi_hndl_{ubi_hndl}, ubi_{ubi} {
    if (ubi_hndl_ != nullptr){
      LOG(INFO) << "FileDescriptor 1.9 ubi_hndl_ " << ubi_hndl_->vol_info.leb_size;
      mtd_ = false;
    } else {
      LOG(INFO) << "FileDescriptor 1.9 ubi_hndl_ is null ";
      mtd_ = false;
      ubi_ = false;
    }
    pages_read_ = 0;
    LOG(INFO) << "FileDescriptor 1.9 pages_read_ " << pages_read_;
  }
#endif
#else
  FileDescriptor(int fd, bool own) : fd_{fd}, own_{own} {}
#endif
  ~FileDescriptor() override {
    if (IsOpen()) {
      Close();
    }
  }

  // Overrides for FileStream::FileDescriptorInterface methods.
  bool IsOpen() const override { return fd_ >= 0; }

  ssize_t Read(void* buf, size_t nbyte) override {
#if USE_MTD
#if !USE_TELAF
    LOG(INFO) << " Read  1.8 nbyte " << nbyte ;
    LOG(INFO) << " Read  from mtd : ? " << mtd_ ;
    LOG(INFO) << " Read  from ubi : ? " << ubi_ ;
#endif
    ssize_t ret = -1;
    if (mtd_ == 1){
#if USE_TELAF
      // assumption nbyte is always multiple of erase size except for last read at image boundary
      if(write_size_ == 0) {
        LOG(ERROR) << " mtd info not available ";
        return -1;
      }
      if (nbyte >= erase_size_) {
        unsigned char dest[erase_size_];
        int iter = nbyte/erase_size_;
        int pages_read = pages_read_;
        for(int i = 0 ; i < iter ; i++) {
          memset(dest, 0, sizeof(dest));
          ret = telaf_mtd_read_block (dest, pages_read + i, erase_size_);
          if(ret != 0) {
            LOG(ERROR) << " mtd read failed ";
            return -1;
          }
          memcpy(reinterpret_cast<unsigned char*>(const_cast<void*>(buf)) + i*erase_size_, dest, erase_size_);
          if ((pages_read == 0) && (i == 0)){
            FILE *fp1;
            fp1 = fopen( "/tmp/boot_b_first_leb" , "w" );
            fwrite(dest , 1 , erase_size_ , fp1);
            fclose(fp1);
          }
        }
        pages_read_ += iter;
      } else {
        ret = telaf_mtd_read_block (reinterpret_cast<unsigned char*>(const_cast<void*>(buf)), pages_read_, nbyte);
        if(ret != 0) {
          LOG(ERROR) << " mtd read failed ";
          return -1;
        }
        pages_read_ += nbyte;
      }
      return HANDLE_EINTR(nbyte);
#else
      int iter = nbyte/mtd_hndl_->info.writesize;
      unsigned char* source[nbyte];// = reinterpret_cast<unsigned char*>(const_cast<void*>(buf));
      unsigned char dest[mtd_hndl_->info.writesize];
      LOG(INFO) << " Read  from mtd write: " << mtd_hndl_->info.writesize ;
      memset(dest, 0, sizeof(dest));
      memset(source, 0, sizeof(source));
      int page_read = pages_read_; 
      for (int i=1 ; i <= iter ; i++){
        memset(dest, 0, sizeof(dest));
        ret = nad_mtd_read_page(mtd_hndl_, dest,
                         page_read + (i-1), mtd_hndl_->info.writesize);
        if(ret == NAD_MTD_PAGE_ERASED) {
          LOG(INFO) << " Read  from mtd erase page ";
          //memset(dest, 0xFF, sizeof(dest));
        } else if(ret != 0) {
          LOG(ERROR) << " mtd read failed " << ret;
          return -1;
        }
        memcpy(reinterpret_cast<unsigned char*>(const_cast<void*>(buf)) + (i-1)*(mtd_hndl_->info.writesize), dest, mtd_hndl_->info.writesize);

      }

      pages_read_ += iter;
      return HANDLE_EINTR(nbyte);
#endif
    } else if(ubi_ == 1) {
#if USE_TELAF
      int iter = nbyte/16384;
      unsigned char dest[16384];
      memset(dest, 0, sizeof(dest));
      int page_read = pages_read_;
      for (int i=1 ; i <= iter ; i++){
        memset(dest, 0, sizeof(dest));
        ret = telaf_ubi_read (dest, page_read + (i-1)*16384, 16384);
        if(ret != 0) {
          LOG(ERROR) << " ubi read failed ";
          return -1;
        }
        memcpy(reinterpret_cast<unsigned char*>(const_cast<void*>(buf)) + (i-1)*16384, dest, 16384);
      }
      if (nbyte%16384 != 0)
      {
        int to_read = nbyte - iter*16384;
        memset(dest, 0, sizeof(dest));
        ret = telaf_ubi_read (dest, page_read + iter*16384, 16384);
        if(ret != 0) {
          LOG(ERROR) << " ubi read failed ";
          return -1;
        }
        memcpy(reinterpret_cast<unsigned char*>(const_cast<void*>(buf)) + iter*16384, dest, to_read);
      }
      pages_read_ += nbyte;
      return HANDLE_EINTR(nbyte);
#else
      LOG(INFO) << " read from ubi device  pages_read_ before reading "<< pages_read_;
      LOG(INFO) << " read from ubi device  volume size "<< ubi_hndl_->vol_info.data_bytes;
      unsigned char *dest = malloc(nbyte);
      if (dest == NULL) {
        LOG(ERROR) << " read from ubi memory alloc failure ";
        return -1;
      }
      memset(dest, 0, nbyte);
      ret = nad_ubi_read(ubi_hndl_, dest, pages_read_, nbyte);
      LOG(INFO) << " read from ubi device ret: " << ret;
      if(ret < 0 ) {
        LOG(ERROR) << " ubi read failed ";
        free(dest);
        return -1;
      }
      memcpy(reinterpret_cast<unsigned char*>(const_cast<void*>(buf)), dest, nbyte);
      pages_read_ += nbyte;
      LOG(INFO) << " read from ubi device  pages_read_ after reading " << pages_read_;
      free(dest);
      return HANDLE_EINTR(nbyte);
#endif
    } else {
      return HANDLE_EINTR(read(fd_, buf, nbyte));
    }
#else
    return HANDLE_EINTR(read(fd_, buf, nbyte));
#endif
  }

  ssize_t Write(const void* buf, size_t nbyte) override {
    return HANDLE_EINTR(write(fd_, buf, nbyte));
  }

  off64_t Seek(off64_t offset, int whence) override {
    return lseek64(fd_, offset, whence);
  }

  mode_t GetFileMode() const override {
    struct stat file_stat;
    if (fstat(fd_, &file_stat) < 0)
      return 0;
    return file_stat.st_mode;
  }

  uint64_t GetSize() const override {
    struct stat file_stat;
    if (fstat(fd_, &file_stat) < 0)
      return 0;
    return file_stat.st_size;
  }

  int Truncate(off64_t length) const override {
    return HANDLE_EINTR(ftruncate(fd_, length));
  }

  int Close() override {
    int fd = -1;
    // The stream may or may not own the file descriptor stored in |fd_|.
    // Despite that, we will need to set |fd_| to -1 when Close() finished.
    // So, here we set it to -1 first and if we own the old descriptor, close
    // it before exiting.
    std::swap(fd, fd_);
    CancelPendingAsyncOperations();
#if USE_MTD
    if (mtd_) {
      LOG(INFO) << " Close mtd device 1.1 ";
#if USE_TELAF
       telaf_mtd_close();
       total_blocks_number_ = 0;
       bad_blocks_number_ = 0;
       erase_size_ = 0;
       write_size_ = 0;
#else
      nad_mtd_close(mtd_hndl_);
#endif
    } else if (ubi_) {
      LOG(INFO) << " Close ubi device 1.2 ";
#if USE_TELAF
      telaf_ubi_close();
#else
      nad_ubi_close(ubi_hndl_);
#endif
    } else {
      LOG(INFO) << " Close ";
    }
#endif
    return own_ ? IGNORE_EINTR(close(fd)) : 0;
  }

  bool WaitForData(Stream::AccessMode mode,
                   const DataCallback& data_callback,
                   ErrorPtr* error) override {
    if (stream_utils::IsReadAccessMode(mode)) {
      CHECK(read_data_callback_.is_null());
      MessageLoop::current()->CancelTask(read_watcher_);
      read_watcher_ = MessageLoop::current()->WatchFileDescriptor(
          FROM_HERE,
          fd_,
          MessageLoop::WatchMode::kWatchRead,
          false,  // persistent
          base::Bind(&FileDescriptor::OnFileCanReadWithoutBlocking,
                     base::Unretained(this)));
      if (read_watcher_ == MessageLoop::kTaskIdNull) {
        Error::AddTo(error, FROM_HERE, errors::stream::kDomain,
                     errors::stream::kInvalidParameter,
                     "File descriptor doesn't support watching for reading.");
        return false;
      }
      read_data_callback_ = data_callback;
    }
    if (stream_utils::IsWriteAccessMode(mode)) {
      CHECK(write_data_callback_.is_null());
      MessageLoop::current()->CancelTask(write_watcher_);
      write_watcher_ = MessageLoop::current()->WatchFileDescriptor(
          FROM_HERE,
          fd_,
          MessageLoop::WatchMode::kWatchWrite,
          false,  // persistent
          base::Bind(&FileDescriptor::OnFileCanWriteWithoutBlocking,
                     base::Unretained(this)));
      if (write_watcher_ == MessageLoop::kTaskIdNull) {
        Error::AddTo(error, FROM_HERE, errors::stream::kDomain,
                     errors::stream::kInvalidParameter,
                     "File descriptor doesn't support watching for writing.");
        return false;
      }
      write_data_callback_ = data_callback;
    }
    return true;
  }

  int WaitForDataBlocking(Stream::AccessMode in_mode,
                          base::TimeDelta timeout,
                          Stream::AccessMode* out_mode) override {
    fd_set read_fds;
    fd_set write_fds;
    fd_set error_fds;

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&error_fds);

    if (stream_utils::IsReadAccessMode(in_mode))
      FD_SET(fd_, &read_fds);

    if (stream_utils::IsWriteAccessMode(in_mode))
      FD_SET(fd_, &write_fds);

    FD_SET(fd_, &error_fds);
    timeval timeout_val = {};
    if (!timeout.is_max()) {
      const timespec ts = timeout.ToTimeSpec();
      TIMESPEC_TO_TIMEVAL(&timeout_val, &ts);
    }
    int res = HANDLE_EINTR(select(fd_ + 1, &read_fds, &write_fds, &error_fds,
                                  timeout.is_max() ? nullptr : &timeout_val));
    if (res > 0 && out_mode) {
      *out_mode = stream_utils::MakeAccessMode(FD_ISSET(fd_, &read_fds),
                                               FD_ISSET(fd_, &write_fds));
    }
    return res;
  }

  void CancelPendingAsyncOperations() override {
    read_data_callback_.Reset();
    if (read_watcher_ != MessageLoop::kTaskIdNull) {
      MessageLoop::current()->CancelTask(read_watcher_);
      read_watcher_ = MessageLoop::kTaskIdNull;
    }

    write_data_callback_.Reset();
    if (write_watcher_ != MessageLoop::kTaskIdNull) {
      MessageLoop::current()->CancelTask(write_watcher_);
      write_watcher_ = MessageLoop::kTaskIdNull;
    }
  }

  // Called from the brillo::MessageLoop when the file descriptor is available
  // for reading.
  void OnFileCanReadWithoutBlocking() {
    CHECK(!read_data_callback_.is_null());
    DataCallback cb = read_data_callback_;
    read_data_callback_.Reset();
    cb.Run(Stream::AccessMode::READ);
  }

  void OnFileCanWriteWithoutBlocking() {
    CHECK(!write_data_callback_.is_null());
    DataCallback cb = write_data_callback_;
    write_data_callback_.Reset();
    cb.Run(Stream::AccessMode::WRITE);
  }

 private:
  // The actual file descriptor we are working with. Will contain -1 if the
  // file stream has been closed.
  int fd_;

  // |own_| is set to true if the file stream owns the file descriptor |fd_| and
  // must close it when the stream is closed. This will be false for file
  // descriptors that shouldn't be closed (e.g. stdin, stdout, stderr).
  bool own_;

  // Stream callbacks to be called when read and/or write operations can be
  // performed on the file descriptor without blocking.
  DataCallback read_data_callback_;
  DataCallback write_data_callback_;

#if USE_MTD
#if !USE_TELAF
  nad_mtd_hndl_t *mtd_hndl_;
  nad_ubi_hndl_t *ubi_hndl_;
#endif
  bool mtd_;
  bool ubi_;
  uint32_t total_blocks_number_;
  uint32_t bad_blocks_number_;
  uint32_t erase_size_;
  uint32_t write_size_;
  int pages_read_;
#endif
  // MessageLoop tasks monitoring read/write operations on the file descriptor.
  MessageLoop::TaskId read_watcher_{MessageLoop::kTaskIdNull};
  MessageLoop::TaskId write_watcher_{MessageLoop::kTaskIdNull};

  DISALLOW_COPY_AND_ASSIGN(FileDescriptor);
};


#if USE_MTD && !USE_TELAF
nad_mtd_hndl_t *FileStream::NadMtdHandler(const char *dev_node_name) {
    nad_mtd_hndl_t *mtd_hndl;
    int ret = -1;
    LOG(INFO) << " FileStream::NadMtdHandler 1.1 mtd devnode name " << dev_node_name;
    mtd_hndl = (nad_mtd_hndl_t *)malloc(sizeof(nad_mtd_hndl_t));
    if(mtd_hndl == NULL){
      LOG(ERROR) << " FileStream::NadMtdHandler  memory allocation failure ";
      return NULL;
    }
    LOG(INFO) << " FileStream::NadMtdHandler 1.2 ";
    ret = nad_mtd_open(dev_node_name, mtd_hndl);
    LOG(INFO) << " FileStream::NadMtdHandler 1.3 ";
    if(0 != ret)
    {
      LOG(ERROR) << " FileStream::NadMtdHandler open nad_mtd_open failed, ret " << ret;
      ret = -1;
      free(mtd_hndl);
      return NULL;
    }
    LOG(INFO) << " FileStream::NadMtdHandler 1.4 ";
    LOG(INFO) << " FileStream::NadMtdHandler writesize " << mtd_hndl->info.writesize;
    LOG(INFO) << " FileStream::NadMtdHandler 1.5 ";
    return mtd_hndl;
}


nad_ubi_hndl_t *FileStream::NadUbiHandler(const char *dev_node_name) {
    nad_ubi_hndl_t *ubi_hndl;
    int ret = -1;
    LOG(INFO) << " FileStream::NadUbiHandler 1.1 open " << dev_node_name ;
    ubi_hndl = (nad_ubi_hndl_t *)malloc(sizeof(nad_ubi_hndl_t));
    if (ubi_hndl == NULL) {
      LOG(ERROR) << " FileStream::NadUbiHandler memory allocation failure ";
      ret = -1;
      return NULL;
    }
    LOG(INFO) << " FileStream::NadUbiHandler 1.2 ";
    ret = nad_ubi_open(dev_node_name, ubi_hndl, 1);
    LOG(INFO) << " FileStream::NadUbiHandler 1.3 ";
    if(0 != ret)
    {
      LOG(ERROR) << " FileStream::NadUbiHandler open nad_ubi_open failed, ret " << ret;
      free(ubi_hndl);
      ret = -1;
      return NULL;
    }
    LOG(INFO) << " FileStream::NadUbiHandler 1.4 ";
    LOG(INFO) << " FileStream::NadubiHandler writesize " << ubi_hndl->vol_info.leb_size;
    LOG(INFO) << " FileStream::NadubiHandler 1.5 ";
    return ubi_hndl;
}
#endif

StreamPtr FileStream::Open(const base::FilePath& path,
                           AccessMode mode,
                           Disposition disposition,
                           ErrorPtr* error) {
  LOG(INFO) << " FileStream 1 ";
  StreamPtr stream;
  int open_flags = O_CLOEXEC;
  switch (mode) {
    case AccessMode::READ:
      open_flags |= O_RDONLY;
      break;
    case AccessMode::WRITE:
      open_flags |= O_WRONLY;
      break;
    case AccessMode::READ_WRITE:
      open_flags |= O_RDWR;
      break;
  }

  switch (disposition) {
    case Disposition::OPEN_EXISTING:
      // Nothing else to do.
      break;
    case Disposition::CREATE_ALWAYS:
      open_flags |= O_CREAT | O_TRUNC;
      break;
    case Disposition::CREATE_NEW_ONLY:
      open_flags |= O_CREAT | O_EXCL;
      break;
    case Disposition::TRUNCATE_EXISTING:
      open_flags |= O_TRUNC;
      break;
  }

  mode_t creation_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
#if USE_MTD
  LOG(INFO) << " FileStream 1 " << path.value().c_str();
  int fd = -1;
  int ret = -1;
#if !USE_TELAF
  nad_mtd_hndl_t *hndl = nullptr;
  nad_ubi_hndl_t *hndlubi = nullptr;
#endif
  bool read_mtd = false;
  bool read_ubi = false;
  std::unique_ptr<FileDescriptorInterface> fd_interface;
  LOG(INFO) << " FileStream 1 is mtd device ?:  " << IsMtd(path.value().c_str());
  if(IsMtd(path.value().c_str())) {
    LOG(INFO) << " FileStream mtd device " << path.value().c_str();
#if USE_TELAF
    ret = telaf_mtd_open (path.value().c_str());
    if (ret != 0){
      LOG(ERROR) << " FileStream::  open mtd error  ";
      brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
      IGNORE_EINTR(close(fd));
      return stream;
    }
    fd = 0;
    read_mtd = true;
#else
     hndl = NadMtdHandler(path.value().c_str());
     if (hndl != NULL){
         LOG(INFO) << " FileStream::  open mtdhandler  " << hndl->info.writesize;
         //std::unique_ptr<FileDescriptorInterface> fd_interface{
         // new FileDescriptor{file_descriptor, own_descriptor}};
         //nad_mtd_ctx_.reset(NadMtdHandler(path.value().c_str()));
         //nad_mtd_hndl_t *hndl1 = nad_mtd_ctx_.get();
         fd =  hndl->fd;
         read_mtd = true;
     }
#endif
  } else if (IsUbi(path.value().c_str())){
   LOG(INFO) << " FileStream ubi device " << path.value().c_str();
#if USE_TELAF
    ret = telaf_ubi_open (path.value().c_str(), 1);
    if (ret != 0){
      LOG(ERROR) << " FileStream::  open ubi error  ";
      brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
      IGNORE_EINTR(close(fd));
      return stream;
    }
    read_ubi = true;
    fd = 0;
#else
     LOG(INFO) << " FileStream 1 ubi device ";
     hndlubi = NadUbiHandler(path.value().c_str());
     //hndlubi = NadUbiHandler("/dev/ubi0_1");
     if (hndlubi != NULL){
         fd =  hndlubi->fd;
         read_ubi = true;
         LOG(INFO) << " FileStream::  open ubihandler  " << hndlubi->vol_info.leb_size;
     }
#endif
  } else {
    fd = HANDLE_EINTR(open(path.value().c_str(), open_flags, creation_mode));
  }
  if (fd < 0) {
    brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
    return stream;
  }

  if (HANDLE_EINTR(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)) < 0) {
    brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
    IGNORE_EINTR(close(fd));
    return stream;
  }

  LOG(INFO) << " FileStream:: create FileDescriptor path:   " << path.value().c_str();
#if USE_TELAF
  fd_interface = std::unique_ptr<FileDescriptorInterface>{new FileDescriptor{fd, true, read_mtd, read_ubi}};
  stream.reset(new FileStream{std::move(fd_interface), mode});
  return stream;
#else
  if(IsMtd(path.value().c_str())) {
    if (hndl != nullptr) {
      LOG(INFO) << " FileStream:: create FileDescriptor  with mtd hndl  read_mtd: " << read_mtd;
      fd_interface = std::unique_ptr<FileDescriptorInterface>{new FileDescriptor{fd, true, hndl, read_mtd}};
    } else {
      LOG(ERROR) << " FileStream::  open mtdhandler error  ";
      brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
      IGNORE_EINTR(close(fd));
      return stream;
    }
  } else if(IsUbi(path.value().c_str())) {
    if (hndlubi != nullptr) {
      LOG(INFO) << " FileStream:: create FileDescriptor  with ubi hndl read_ubi: " << read_ubi;
      fd_interface = std::unique_ptr<FileDescriptorInterface>{new FileDescriptor{fd, true, hndlubi, read_ubi}};
    } else {
      LOG(ERROR) << " FileStream::  open ubihandler error  ";
      brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
      IGNORE_EINTR(close(fd));
      return stream;
    }
  } else {
    LOG(INFO) << " FileStream:: create FileDescriptor  without mtd hndl  ";
    fd_interface = std::unique_ptr<FileDescriptorInterface>{new FileDescriptor{fd, true, hndl, false}};
  }
  stream.reset(new FileStream{std::move(fd_interface), mode});
  return stream;
#endif
#else
  int fd = HANDLE_EINTR(open(path.value().c_str(), open_flags, creation_mode));
  if (fd < 0) {
    brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
    return stream;
  }
  if (HANDLE_EINTR(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)) < 0) {
    brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
    IGNORE_EINTR(close(fd));
    return stream;
  }
  std::unique_ptr<FileDescriptorInterface> fd_interface{
      new FileDescriptor{fd, true}};
  stream.reset(new FileStream{std::move(fd_interface), mode});
  return stream;
#endif
}

StreamPtr FileStream::CreateTemporary(ErrorPtr* error) {
  StreamPtr stream;
  base::FilePath path;
  // The "proper" solution would be here to add O_TMPFILE flag to |open_flags|
  // below and pass just the temp directory path to open(), so the actual file
  // name isn't even needed. However this is supported only as of Linux kernel
  // 3.11 and not all our configurations have that. So, for now just create
  // a temp file first and then open it.
  if (!base::CreateTemporaryFile(&path)) {
    brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
    return stream;
  }
  int open_flags = O_CLOEXEC | O_RDWR | O_CREAT | O_TRUNC;
  mode_t creation_mode = S_IRUSR | S_IWUSR;
  int fd = HANDLE_EINTR(open(path.value().c_str(), open_flags, creation_mode));
  if (fd < 0) {
    brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
    return stream;
  }
  unlink(path.value().c_str());

  stream = FromFileDescriptor(fd, true, error);
  if (!stream)
    IGNORE_EINTR(close(fd));
  return stream;
}

StreamPtr FileStream::FromFileDescriptor(int file_descriptor,
                                         bool own_descriptor,
                                         ErrorPtr* error) {
  StreamPtr stream;
  if (file_descriptor < 0 || file_descriptor >= FD_SETSIZE) {
    Error::AddTo(error, FROM_HERE, errors::stream::kDomain,
                 errors::stream::kInvalidParameter,
                 "Invalid file descriptor value");
    return stream;
  }

  int fd_flags = HANDLE_EINTR(fcntl(file_descriptor, F_GETFL));
  if (fd_flags < 0) {
    brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
    return stream;
  }
  int file_access_mode = (fd_flags & O_ACCMODE);
  AccessMode access_mode = AccessMode::READ_WRITE;
  if (file_access_mode == O_RDONLY)
    access_mode = AccessMode::READ;
  else if (file_access_mode == O_WRONLY)
    access_mode = AccessMode::WRITE;

  // Make sure the file descriptor is set to perform non-blocking operations
  // if not enabled already.
  if ((fd_flags & O_NONBLOCK) == 0) {
    fd_flags |= O_NONBLOCK;
    if (HANDLE_EINTR(fcntl(file_descriptor, F_SETFL, fd_flags)) < 0) {
      brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
      return stream;
    }
  }

#if USE_MTD
#if USE_TELAF
  std::unique_ptr<FileDescriptorInterface> fd_interface{
      new FileDescriptor{file_descriptor, own_descriptor, false, false}};
#else
  nad_mtd_hndl_t *hndl = nullptr;
  std::unique_ptr<FileDescriptorInterface> fd_interface{
      new FileDescriptor{file_descriptor, own_descriptor, hndl, false}};
#endif
#else
  std::unique_ptr<FileDescriptorInterface> fd_interface{
      new FileDescriptor{file_descriptor, own_descriptor}};
#endif

  stream.reset(new FileStream{std::move(fd_interface), access_mode});
  return stream;
}

FileStream::FileStream(std::unique_ptr<FileDescriptorInterface> fd_interface,
                       AccessMode mode)
    : fd_interface_(std::move(fd_interface)),
      access_mode_(mode) {
  switch (fd_interface_->GetFileMode() & S_IFMT) {
    case S_IFCHR:  // Character device
    case S_IFSOCK:  // Socket
    case S_IFIFO:  // FIFO/pipe
      // We know that these devices are not seekable and stream size is unknown.
      seekable_ = false;
      can_get_size_ = false;
      break;

    case S_IFBLK:  // Block device
    case S_IFDIR:  // Directory
    case S_IFREG:  // Normal file
    case S_IFLNK:  // Symbolic link
    default:
      // The above devices support seek. Also, if not sure/in doubt, err on the
      // side of "allowable".
      seekable_ = true;
      can_get_size_ = true;
      break;
  }
}

bool FileStream::IsOpen() const {
  return fd_interface_->IsOpen();
}

#if USE_MTD
bool FileStream::IsMtd(const char* path) {
    char mtd_no[32] = {"/dev/"};
#if USE_TELAF
    int err = get_mtdno(path,(mtd_no+5));
#else
    int err = nad_get_mtdno(path,(mtd_no+5));
#endif
    if (err != 0) {
      LOG(INFO) << " can-not open any such partition ";
      return false;
    }
    int uid, vid;
    char ubi_path[64];
    LOG(INFO) << " IsMtd,  mtdno " << mtd_no;
    std::string ubi_str(mtd_no);
    LOG(INFO) << " IsMtd,  ubi_str " << ubi_str;
    LOG(INFO) << " IsMtd  mtd no: " << std::stoi(ubi_str.substr(8, 2));
#if USE_TELAF
    int mtd_system_ubi =  get_value_from_file(SYSTEM_UBI_MTD_NUM);
#else
    int mtd_system_ubi =  nad_get_value_from_file(SYSTEM_UBI_MTD_NUM);
#endif
    if (mtd_system_ubi == -1) {
      LOG(INFO) << " failed to get mtd no for system ubi partition ";
      return false;
    }

    LOG(INFO) << " system ubi,  mtdno " << mtd_system_ubi;

    if (std::stoi(ubi_str.substr(8, 2)) > mtd_system_ubi){
      LOG(INFO) << mtd_no << " is ubi device " ;
      return false;
    } else {
        LOG(INFO) << mtd_no << " is mtd device " ;
      return true;
    }
}

bool FileStream::IsUbi(const char* path) {

    char mtd_no[32] = {"/dev/"};
#if USE_TELAF
    int err = get_mtdno(path,(mtd_no+5));
#else
    int err = nad_get_mtdno(path,(mtd_no+5));
#endif
    if (err != 0) {
      LOG(INFO) << " can-not open any such partition ";
      return false;
    }
    int uid, vid;
    char ubi_path[64];
    LOG(INFO) << " IsUbi,  mtdno " << mtd_no;
    std::string ubi_str(mtd_no);
    LOG(INFO) << " IsUbi, ubi_str " << ubi_str;
    LOG(INFO) << " IsUbi mtd no: " << std::stoi(ubi_str.substr(8, 2));

#if USE_TELAF
    int mtd_system_ubi =  get_value_from_file(SYSTEM_UBI_MTD_NUM);
#else
    int mtd_system_ubi =  nad_get_value_from_file(SYSTEM_UBI_MTD_NUM);
#endif
    if (mtd_system_ubi == -1) {
      LOG(INFO) << " failed to get mtd no for system ubi partition ";
      return false;
    }
    LOG(INFO) << " system ubi,  mtdno " << mtd_system_ubi;

    if (std::stoi(ubi_str.substr(8, 2)) > mtd_system_ubi){
      LOG(INFO) << mtd_no << " is ubi device " ;
      return true;
    } else {
        LOG(INFO) << mtd_no << " is mtd device " ;
      return false;
    }
}
#endif

bool FileStream::CanRead() const {
  return IsOpen() && stream_utils::IsReadAccessMode(access_mode_);
}

bool FileStream::CanWrite() const {
  return IsOpen() && stream_utils::IsWriteAccessMode(access_mode_);
}

bool FileStream::CanSeek() const {
  return IsOpen() && seekable_;
}

bool FileStream::CanGetSize() const {
  return IsOpen() && can_get_size_;
}

uint64_t FileStream::GetSize() const {
  return IsOpen() ? fd_interface_->GetSize() : 0;
}

bool FileStream::SetSizeBlocking(uint64_t size, ErrorPtr* error) {
  if (!IsOpen())
    return stream_utils::ErrorStreamClosed(FROM_HERE, error);

  if (!stream_utils::CheckInt64Overflow(FROM_HERE, size, 0, error))
    return false;

  if (fd_interface_->Truncate(size) >= 0)
    return true;

  errors::system::AddSystemError(error, FROM_HERE, errno);
  return false;
}

uint64_t FileStream::GetRemainingSize() const {
  if (!CanGetSize())
    return 0;
  uint64_t pos = GetPosition();
  uint64_t size = GetSize();
  return (pos < size) ? (size - pos) : 0;
}

uint64_t FileStream::GetPosition() const {
  if (!CanSeek())
    return 0;

  off64_t pos = fd_interface_->Seek(0, SEEK_CUR);
  const off64_t min_pos = 0;
  return std::max(min_pos, pos);
}

bool FileStream::Seek(int64_t offset,
                      Whence whence,
                      uint64_t* new_position,
                      ErrorPtr* error) {
  if (!IsOpen())
    return stream_utils::ErrorStreamClosed(FROM_HERE, error);

  int raw_whence = 0;
  switch (whence) {
    case Whence::FROM_BEGIN:
      raw_whence = SEEK_SET;
      break;
    case Whence::FROM_CURRENT:
      raw_whence = SEEK_CUR;
      break;
    case Whence::FROM_END:
      raw_whence = SEEK_END;
      break;
    default:
      Error::AddTo(error, FROM_HERE, errors::stream::kDomain,
                   errors::stream::kInvalidParameter, "Invalid whence");
      return false;
  }
  off64_t pos = fd_interface_->Seek(offset, raw_whence);
  if (pos < 0) {
    errors::system::AddSystemError(error, FROM_HERE, errno);
    return false;
  }

  if (new_position)
    *new_position = static_cast<uint64_t>(pos);
  return true;
}

bool FileStream::ReadNonBlocking(void* buffer,
                                 size_t size_to_read,
                                 size_t* size_read,
                                 bool* end_of_stream,
                                 ErrorPtr* error) {
  if (!IsOpen())
    return stream_utils::ErrorStreamClosed(FROM_HERE, error);

  ssize_t read = fd_interface_->Read(buffer, size_to_read);
  if (read < 0) {
    // If read() fails, check if this is due to no data being currently
    // available and we do non-blocking I/O.
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      if (end_of_stream)
        *end_of_stream = false;
      *size_read = 0;
      return true;
    }
    // Otherwise a real problem occurred.
    errors::system::AddSystemError(error, FROM_HERE, errno);
    return false;
  }
  if (end_of_stream)
    *end_of_stream = (read == 0 && size_to_read != 0);
  *size_read = read;
  return true;
}

bool FileStream::WriteNonBlocking(const void* buffer,
                                  size_t size_to_write,
                                  size_t* size_written,
                                  ErrorPtr* error) {
  if (!IsOpen())
    return stream_utils::ErrorStreamClosed(FROM_HERE, error);

  ssize_t written = fd_interface_->Write(buffer, size_to_write);
  if (written < 0) {
    // If write() fails, check if this is due to the fact that no data
    // can be presently written and we do non-blocking I/O.
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      *size_written = 0;
      return true;
    }
    // Otherwise a real problem occurred.
    errors::system::AddSystemError(error, FROM_HERE, errno);
    return false;
  }
  *size_written = written;
  return true;
}

bool FileStream::FlushBlocking(ErrorPtr* error) {
  if (!IsOpen())
    return stream_utils::ErrorStreamClosed(FROM_HERE, error);

  // File descriptors don't have an internal buffer to flush.
  return true;
}

bool FileStream::CloseBlocking(ErrorPtr* error) {
  if (!IsOpen())
    return true;

  if (fd_interface_->Close() < 0) {
    errors::system::AddSystemError(error, FROM_HERE, errno);
    return false;
  }

  return true;
}

bool FileStream::WaitForData(
    AccessMode mode,
    const base::Callback<void(AccessMode)>& callback,
    ErrorPtr* error) {
  if (!IsOpen())
    return stream_utils::ErrorStreamClosed(FROM_HERE, error);

  return fd_interface_->WaitForData(mode, callback, error);
}

bool FileStream::WaitForDataBlocking(AccessMode in_mode,
                                     base::TimeDelta timeout,
                                     AccessMode* out_mode,
                                     ErrorPtr* error) {
  if (!IsOpen())
    return stream_utils::ErrorStreamClosed(FROM_HERE, error);

  int ret = fd_interface_->WaitForDataBlocking(in_mode, timeout, out_mode);
  if (ret < 0) {
    errors::system::AddSystemError(error, FROM_HERE, errno);
    return false;
  }
  if (ret == 0)
    return stream_utils::ErrorOperationTimeout(FROM_HERE, error);

  return true;
}

void FileStream::CancelPendingAsyncOperations() {
  if (IsOpen()) {
    fd_interface_->CancelPendingAsyncOperations();
  }
  Stream::CancelPendingAsyncOperations();
}

}  // namespace brillo
