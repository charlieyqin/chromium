// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/fileapi/file_system_file_util_proxy.h"

#include "base/bind.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop_proxy.h"
#include "webkit/fileapi/cross_file_util_helper.h"
#include "webkit/fileapi/file_system_file_util.h"
#include "webkit/fileapi/file_system_operation_context.h"

namespace fileapi {

using base::Bind;
using base::Callback;
using base::Owned;
using base::PlatformFileError;
using base::Unretained;

namespace {

typedef fileapi::FileSystemFileUtilProxy Proxy;

class CopyOrMoveHelper {
 public:
  CopyOrMoveHelper(CrossFileUtilHelper* helper)
      : helper_(helper),
        error_(base::PLATFORM_FILE_OK) {}
  ~CopyOrMoveHelper() {}

  void RunWork() {
    error_ = helper_->DoWork();
  }

  void Reply(const Proxy::StatusCallback& callback) {
    if (!callback.is_null())
      callback.Run(error_);
  }

 private:
  scoped_ptr<CrossFileUtilHelper> helper_;
  base::PlatformFileError error_;
  DISALLOW_COPY_AND_ASSIGN(CopyOrMoveHelper);
};

class EnsureFileExistsHelper {
 public:
  EnsureFileExistsHelper() : error_(base::PLATFORM_FILE_OK), created_(false) {}

  void RunWork(FileSystemFileUtil* file_util,
               FileSystemOperationContext* context,
               const FileSystemPath& path) {
    error_ = file_util->EnsureFileExists(context, path, &created_);
  }

  void Reply(const Proxy::EnsureFileExistsCallback& callback) {
    if (!callback.is_null())
      callback.Run(error_, created_);
  }

 private:
  base::PlatformFileError error_;
  bool created_;
  DISALLOW_COPY_AND_ASSIGN(EnsureFileExistsHelper);
};

class GetFileInfoHelper {
 public:
  GetFileInfoHelper() : error_(base::PLATFORM_FILE_OK) {}

  void RunWork(FileSystemFileUtil* file_util,
               FileSystemOperationContext* context,
               const FileSystemPath& path) {
    error_ = file_util->GetFileInfo(
        context, path, &file_info_, &platform_path_);
  }

  void Reply(const Proxy::GetFileInfoCallback& callback) {
    if (!callback.is_null())
      callback.Run(error_, file_info_, platform_path_);
  }

 private:
  base::PlatformFileError error_;
  base::PlatformFileInfo file_info_;
  FilePath platform_path_;
  DISALLOW_COPY_AND_ASSIGN(GetFileInfoHelper);
};

class ReadDirectoryHelper {
 public:
  ReadDirectoryHelper() : error_(base::PLATFORM_FILE_OK) {}

  void RunWork(FileSystemFileUtil* file_util,
               FileSystemOperationContext* context,
               const FileSystemPath& path) {
    error_ = file_util->ReadDirectory(context, path, &entries_);
  }

  void Reply(const Proxy::ReadDirectoryCallback& callback) {
    if (!callback.is_null())
      callback.Run(error_, entries_, false  /* has_more */);
  }

 private:
  base::PlatformFileError error_;
  std::vector<Proxy::Entry> entries_;
  DISALLOW_COPY_AND_ASSIGN(ReadDirectoryHelper);
};

}  // namespace

// static
bool FileSystemFileUtilProxy::Delete(
    MessageLoopProxy* message_loop_proxy,
    FileSystemOperationContext* context,
    FileSystemFileUtil* file_util,
    const FileSystemPath& path,
    bool recursive,
    const StatusCallback& callback) {
  return base::FileUtilProxy::RelayFileTask(
      message_loop_proxy, FROM_HERE,
      base::Bind(&FileSystemFileUtil::Delete, base::Unretained(file_util),
                 context, path, recursive),
      callback);
}

// static
bool FileSystemFileUtilProxy::CreateOrOpen(
    MessageLoopProxy* message_loop_proxy,
    FileSystemOperationContext* context,
    FileSystemFileUtil* file_util,
    const FileSystemPath& path,
    int file_flags,
    const CreateOrOpenCallback& callback) {
  return base::FileUtilProxy::RelayCreateOrOpen(
      message_loop_proxy,
      base::Bind(&FileSystemFileUtil::CreateOrOpen, base::Unretained(file_util),
                 context, path, file_flags),
      base::Bind(&FileSystemFileUtil::Close, base::Unretained(file_util),
                 context),
      callback);
}

// static
bool FileSystemFileUtilProxy::Copy(
    MessageLoopProxy* message_loop_proxy,
    FileSystemOperationContext* context,
    FileSystemFileUtil* src_util,
    FileSystemFileUtil* dest_util,
    const FileSystemPath& src_path,
    const FileSystemPath& dest_path,
    const StatusCallback& callback) {
  CopyOrMoveHelper* helper = new CopyOrMoveHelper(
      new CrossFileUtilHelper(
          context, src_util, dest_util, src_path, dest_path,
          CrossFileUtilHelper::OPERATION_COPY));
  return message_loop_proxy->PostTaskAndReply(
        FROM_HERE,
        Bind(&CopyOrMoveHelper::RunWork, Unretained(helper)),
        Bind(&CopyOrMoveHelper::Reply, Owned(helper), callback));
}

// static
bool FileSystemFileUtilProxy::Move(
    MessageLoopProxy* message_loop_proxy,
    FileSystemOperationContext* context,
      FileSystemFileUtil* src_util,
      FileSystemFileUtil* dest_util,
      const FileSystemPath& src_path,
      const FileSystemPath& dest_path,
    const StatusCallback& callback) {
  CopyOrMoveHelper* helper = new CopyOrMoveHelper(
      new CrossFileUtilHelper(
          context, src_util, dest_util, src_path, dest_path,
          CrossFileUtilHelper::OPERATION_MOVE));
  return message_loop_proxy->PostTaskAndReply(
        FROM_HERE,
        Bind(&CopyOrMoveHelper::RunWork, Unretained(helper)),
        Bind(&CopyOrMoveHelper::Reply, Owned(helper), callback));
}

// static
bool FileSystemFileUtilProxy::EnsureFileExists(
    MessageLoopProxy* message_loop_proxy,
    FileSystemOperationContext* context,
    FileSystemFileUtil* file_util,
    const FileSystemPath& path,
    const EnsureFileExistsCallback& callback) {
  EnsureFileExistsHelper* helper = new EnsureFileExistsHelper;
  return message_loop_proxy->PostTaskAndReply(
        FROM_HERE,
        Bind(&EnsureFileExistsHelper::RunWork, Unretained(helper),
             file_util, context, path),
        Bind(&EnsureFileExistsHelper::Reply, Owned(helper), callback));
}

// static
bool FileSystemFileUtilProxy::CreateDirectory(
    MessageLoopProxy* message_loop_proxy,
    FileSystemOperationContext* context,
    FileSystemFileUtil* file_util,
    const FileSystemPath& path,
    bool exclusive,
    bool recursive,
    const StatusCallback& callback) {
  return base::FileUtilProxy::RelayFileTask(
      message_loop_proxy, FROM_HERE,
      base::Bind(&FileSystemFileUtil::CreateDirectory,
                 base::Unretained(file_util),
                 context, path, exclusive, recursive),
      callback);
}

// static
bool FileSystemFileUtilProxy::GetFileInfo(
    MessageLoopProxy* message_loop_proxy,
    FileSystemOperationContext* context,
    FileSystemFileUtil* file_util,
    const FileSystemPath& path,
    const GetFileInfoCallback& callback) {
  GetFileInfoHelper* helper = new GetFileInfoHelper;
  return message_loop_proxy->PostTaskAndReply(
        FROM_HERE,
        Bind(&GetFileInfoHelper::RunWork, Unretained(helper),
             file_util, context, path),
        Bind(&GetFileInfoHelper::Reply, Owned(helper), callback));
}

// static
bool FileSystemFileUtilProxy::ReadDirectory(
    MessageLoopProxy* message_loop_proxy,
    FileSystemOperationContext* context,
    FileSystemFileUtil* file_util,
    const FileSystemPath& path,
    const ReadDirectoryCallback& callback) {
  ReadDirectoryHelper* helper = new ReadDirectoryHelper;
  return message_loop_proxy->PostTaskAndReply(
        FROM_HERE,
        Bind(&ReadDirectoryHelper::RunWork, Unretained(helper),
             file_util, context, path),
        Bind(&ReadDirectoryHelper::Reply, Owned(helper), callback));
}

// Touches a file by calling |file_util|'s FileSystemFileUtil::Touch
// on the given |message_loop_proxy|.
bool FileSystemFileUtilProxy::Touch(
    MessageLoopProxy* message_loop_proxy,
    FileSystemOperationContext* context,
    FileSystemFileUtil* file_util,
    const FileSystemPath& path,
    const base::Time& last_access_time,
    const base::Time& last_modified_time,
    const StatusCallback& callback) {
  return base::FileUtilProxy::RelayFileTask(
      message_loop_proxy, FROM_HERE,
      base::Bind(&FileSystemFileUtil::Touch, base::Unretained(file_util),
                 context, path, last_access_time, last_modified_time),
      callback);
}

// static
bool FileSystemFileUtilProxy::Truncate(
    MessageLoopProxy* message_loop_proxy,
    FileSystemOperationContext* context,
    FileSystemFileUtil* file_util,
    const FileSystemPath& path,
    int64 length,
    const StatusCallback& callback) {
  return base::FileUtilProxy::RelayFileTask(
      message_loop_proxy, FROM_HERE,
      base::Bind(&FileSystemFileUtil::Truncate, base::Unretained(file_util),
                 context, path, length),
      callback);
}

}  // namespace fileapi
