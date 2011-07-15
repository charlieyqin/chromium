// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/media/audio_sync_reader.h"

#include <algorithm>

#include "base/process_util.h"
#include "base/shared_memory.h"
#include "media/audio/audio_buffers_state.h"
#include "media/audio/audio_util.h"

AudioSyncReader::AudioSyncReader(base::SharedMemory* shared_memory)
    : shared_memory_(shared_memory) {
}

AudioSyncReader::~AudioSyncReader() {
}

// media::AudioOutputController::SyncReader implementations.
void AudioSyncReader::UpdateBufferState(const AudioBuffersState& buffer_state) {
  socket_->Send(&buffer_state, sizeof(buffer_state));
}

uint32 AudioSyncReader::Read(void* data, uint32 size) {
  uint32 read_size =
      std::min(size,
               media::GetMaxDataSizeInBytes(shared_memory_->created_size()));
  read_size = std::min(read_size,
                       media::GetActualDataSizeInBytes(shared_memory_));

  // Get the data from the buffer.
  memcpy(data, media::GetDataPointer(shared_memory_), read_size);

  // Zero out the entire buffer.
  memset(shared_memory_->memory(), 0, shared_memory_->created_size());
  return read_size;
}

void AudioSyncReader::Close() {
  socket_->Close();
}

bool AudioSyncReader::Init() {
  base::SyncSocket* sockets[2] = {0};
  if (base::SyncSocket::CreatePair(sockets)) {
    socket_.reset(sockets[0]);
    foreign_socket_.reset(sockets[1]);
    return true;
  }
  return false;
}

#if defined(OS_WIN)
bool AudioSyncReader::PrepareForeignSocketHandle(
    base::ProcessHandle process_handle,
    base::SyncSocket::Handle* foreign_handle) {
  ::DuplicateHandle(GetCurrentProcess(), foreign_socket_->handle(),
                    process_handle, foreign_handle,
                    0, FALSE, DUPLICATE_SAME_ACCESS);
  if (*foreign_handle != 0)
    return true;
  return false;
}
#else
bool AudioSyncReader::PrepareForeignSocketHandle(
    base::ProcessHandle process_handle,
    base::FileDescriptor* foreign_handle) {
  foreign_handle->fd = foreign_socket_->handle();
  foreign_handle->auto_close = false;
  if (foreign_handle->fd != -1)
    return true;
  return false;
}
#endif
