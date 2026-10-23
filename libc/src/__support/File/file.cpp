//===--- Implementation of a platform independent file data structure -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "file.h"

#include "hdr/errno_macros.h"
#include "hdr/func/realloc.h"
#include "hdr/stdio_macros.h"
#include "hdr/types/off_t.h"
#include "hdr/wchar_macros.h"
#include "src/__support/CPP/new.h"
#include "src/__support/CPP/span.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include "src/__support/wchar/character_converter.h"
#include "src/__support/wchar/wcrtomb.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {
File *File::list_all = nullptr;
Mutex File::list_lock(/*timed=*/false, /*recursive=*/false, /*robust=*/false,
                      /*pshared=*/false);

// Adds a file to the global list of all open files.
void File::add_file(File *f) {
  File::list_lock.lock();
  f->next = File::list_all;
  f->prev = nullptr;
  if (File::list_all != nullptr)
    File::list_all->prev = f;
  File::list_all = f;
  File::list_lock.unlock();
}

// Removes a file from the global list of all open files.
void File::remove_file(File *f) {
  File::list_lock.lock();
  if (f->prev != nullptr)
    f->prev->next = f->next;
  if (f->next != nullptr)
    f->next->prev = f->prev;
  if (File::list_all == f)
    File::list_all = f->next;
  f->prev = nullptr;
  f->next = nullptr;
  File::list_lock.unlock();
}

File *File::get_first_file() { return File::list_all; }
void File::lock_list() { File::list_lock.lock(); }
void File::unlock_list() { File::list_lock.unlock(); }

bool File::release_backend_unlocked() {
  if (!destroy_object)
    return false;
  flush_unlocked();
  platform_close(get_backend());
  backend = nullptr;
  // A failed replacement still needs to destroy the original object, without
  // closing or publishing the old association a second time.
  platform_close = [](File *) { return 0; };
  platform_flush = nullptr;
  platform_wide_write = nullptr;
  has_descriptor = false;
  reset_stream_state_unlocked(0);
  return true;
}

void File::install_backend_unlocked(File *replacement) {
  backend = replacement;
  platform_write = replacement->platform_write;
  platform_read = replacement->platform_read;
  platform_seek = replacement->platform_seek;
  platform_close = replacement->platform_close;
  platform_flush = replacement->platform_flush;
  platform_wide_write = replacement->platform_wide_write;
  has_descriptor = replacement->has_descriptor;
  close_on_flush_error = replacement->close_on_flush_error;
  reset_stream_state_unlocked(replacement->mode);
}

FileIOResult File::write_unlocked(const void *data, size_t len) {
  switch (orientation) {
  case Orientation::WIDE:
    err = true;
    return {0, EINVAL};
  case Orientation::UNORIENTED:
    orientation = Orientation::BYTE;
    break;
  case Orientation::BYTE:
    break;
  }
  return write_unlocked_impl(data, len);
}

FileIOResult File::write_unlocked_impl(const void *data, size_t len) {
  if (!write_allowed()) {
    err = true;
    return {0, EBADF};
  }

  begin_output();

  if (bufmode == _IONBF) { // unbuffered.
    FileIOResult ret_val =
        write_unlocked_nbf(static_cast<const uint8_t *>(data), len);
    int flush_error = flush_unlocked();
    if (!ret_val.has_error())
      ret_val.error = flush_error;
    return ret_val;
  }
  // The fallback array guarantees input pushback; it is not an output buffer.
  if (buf == ungetc_buf)
    return write_unlocked_nbf(static_cast<const uint8_t *>(data), len);
  if (bufmode == _IOFBF) // fully buffered
    return write_unlocked_fbf(static_cast<const uint8_t *>(data), len);
  return write_unlocked_lbf(static_cast<const uint8_t *>(data),
                            len); // line buffered
}

FileIOResult File::write_unlocked_nbf(const uint8_t *data, size_t len) {
  if (pos > 0) { // If the buffer is not empty
    int error = flush_unlocked();
    if (error) {
      // No bytes from data were written, so return 0.
      return {0, error};
    }
  }

  FileIOResult write_result = platform_write(get_backend(), data, len);
  if (write_result.has_error() || write_result < len)
    err = true;
  return write_result;
}

FileIOResult File::write_unlocked_fbf(const uint8_t *data, size_t len) {
  const size_t init_pos = pos;
  const size_t bufspace = bufsize - pos;

  // If data is too large to be buffered at all, then just write it unbuffered.
  if (len > bufspace + bufsize)
    return write_unlocked_nbf(data, len);

  // we split |data| (conceptually) using the split point. Then we handle the
  // two pieces separately.
  const size_t split_point = len < bufspace ? len : bufspace;

  // The primary piece is the piece of |data| we want to write to the buffer
  // before flushing. It will always fit into the buffer, since the split point
  // is defined as being min(len, bufspace), and it will always exist if len is
  // non-zero.
  cpp::span<const uint8_t> primary(data, split_point);

  // The second piece is the remainder of |data|. It is written to the buffer if
  // it fits, or written directly to the output if it doesn't. If the primary
  // piece fits entirely in the buffer, the remainder may be nothing.
  cpp::span<const uint8_t> remainder(
      static_cast<const uint8_t *>(data) + split_point, len - split_point);

  cpp::span<uint8_t> bufref(static_cast<uint8_t *>(buf), bufsize);

  // Copy the first piece into the buffer.
  inline_memcpy(bufref.data() + pos, primary.data(), primary.size());
  pos += primary.size();

  // If there is no remainder, we can return early, since the first piece has
  // fit completely into the buffer.
  if (remainder.size() == 0)
    return len;

  // We need to flush the buffer now, since there is still data and the buffer
  // is full.
  const size_t write_size = pos;

  FileIOResult buf_result = platform_write(get_backend(), buf, write_size);
  size_t bytes_written = buf_result.value;

  // If less bytes were written than expected, then an error occurred. Return
  // the number of bytes that have been written from |data|.
  if (buf_result.has_error() || bytes_written < write_size) {
    err = true;
    // Preserve previously accepted data, but not uncommitted new bytes whose
    // count is returned to the caller for retry.
    pos = bytes_written < init_pos ? init_pos - bytes_written : 0;
    for (size_t i = 0; i < pos; ++i)
      buf[i] = buf[i + bytes_written];
    return {bytes_written <= init_pos ? 0 : bytes_written - init_pos,
            buf_result.error ? buf_result.error : EIO};
  }
  pos = 0;

  // The second piece is handled basically the same as the first, although we
  // know that if the second piece has data in it then the buffer has been
  // flushed, meaning that pos is always 0.
  if (remainder.size() < bufsize) {
    inline_memcpy(bufref.data(), remainder.data(), remainder.size());
    pos = remainder.size();
  } else {

    FileIOResult result =
        platform_write(get_backend(), remainder.data(), remainder.size());
    bytes_written = result.value;

    // If less bytes were written than expected, then an error occurred. Return
    // the number of bytes that have been written from |data|.
    if (result.has_error() || bytes_written < remainder.size()) {
      err = true;
      return {primary.size() + bytes_written, result.error};
    }
  }

  return len;
}

FileIOResult File::write_unlocked_lbf(const uint8_t *data, size_t len) {
  constexpr uint8_t NEWLINE_CHAR = '\n';
  size_t last_newline = len;
  for (size_t i = len; i >= 1; --i) {
    if (data[i - 1] == NEWLINE_CHAR) {
      last_newline = i - 1;
      break;
    }
  }

  // If there is no newline, treat this as fully buffered.
  if (last_newline == len) {
    return write_unlocked_fbf(data, len);
  }

  // we split |data| (conceptually) using the split point. Then we handle the
  // two pieces separately.
  const size_t split_point = last_newline + 1;

  // The primary piece is everything in |data| up to the newline. It's written
  // unbuffered to the output.
  cpp::span<const uint8_t> primary(data, split_point);

  // The second piece is the remainder of |data|. It is written fully buffered,
  // meaning it may stay in the buffer if it fits.
  cpp::span<const uint8_t> remainder(
      static_cast<const uint8_t *>(data) + split_point, len - split_point);

  FileIOResult first = write_unlocked_nbf(primary.data(), primary.size());
  if (first.has_error() || first.value < primary.size()) {
    err = true;
    return first;
  }

  int flush_error = flush_unlocked();
  if (flush_error)
    return {first.value, flush_error};

  FileIOResult second =
      write_unlocked_fbf(remainder.data(), remainder.size());
  return {first.value + second.value, second.error};
}

FileIOResult File::read_unlocked(void *data, size_t len) {
  switch (orientation) {
  case Orientation::WIDE:
    err = true;
    return {0, EINVAL};
  case Orientation::UNORIENTED:
    orientation = Orientation::BYTE;
    break;
  case Orientation::BYTE:
    break;
  }
  return read_unlocked_impl(data, len);
}

FileIOResult File::read_unlocked_impl(void *data, size_t len) {
  if (!read_allowed()) {
    err = true;
    return {0, EBADF};
  }

  prev_op = FileOp::READ;

  if (bufmode == _IONBF) { // unbuffered.
    return read_unlocked_nbf(static_cast<uint8_t *>(data), len);
  } else if (bufmode == _IOFBF) { // fully buffered
    return read_unlocked_fbf(static_cast<uint8_t *>(data), len);
  } else /*if (bufmode == _IOLBF) */ { // line buffered
    // There is no line buffered mode for read. Use fully buffered instead.
    return read_unlocked_fbf(static_cast<uint8_t *>(data), len);
  }
}

size_t File::copy_data_from_buf(uint8_t *data, size_t len) {
  if (len == 0)
    return 0;
  size_t prefix = 0;
  if (byte_pushback != EOF) {
    *data++ = static_cast<uint8_t>(byte_pushback);
    byte_pushback = EOF;
    --len;
    prefix = 1;
  }
  cpp::span<uint8_t> bufref(static_cast<uint8_t *>(buf), bufsize);
  cpp::span<uint8_t> dataref(static_cast<uint8_t *>(data), len);

  // Because read_limit is always greater than equal to pos,
  // available_data is never a wrapped around value.
  size_t available_data = read_limit - pos;
  if (len <= available_data) {
    inline_memcpy(dataref.data(), bufref.data() + pos, len);
    pos += len;
    return prefix + len;
  }

  // Copy all of the available data.
  inline_memcpy(dataref.data(), bufref.data() + pos, available_data);
  read_limit = pos = 0; // Reset the pointers.

  return prefix + available_data;
}

FileIOResult File::read_unlocked_fbf(uint8_t *data, size_t len) {
  // Read data from the buffer first.
  size_t available_data = copy_data_from_buf(data, len);
  if (available_data == len)
    return available_data;

  // Update the dataref to reflect that fact that we have already
  // copied |available_data| into |data|.
  size_t to_fetch = len - available_data;
  cpp::span<uint8_t> dataref(static_cast<uint8_t *>(data) + available_data,
                             to_fetch);

  if (to_fetch > bufsize) {
    FileIOResult result = platform_read(get_backend(), dataref.data(), to_fetch);
    size_t fetched_size = result.value;
    if (result.has_error() || fetched_size < to_fetch) {
      if (!result.has_error())
        eof = true;
      else
        err = true;
      return {available_data + fetched_size, result.error};
    }
    return len;
  }

  // Fetch and buffer another buffer worth of data.
  FileIOResult result = platform_read(get_backend(), buf, bufsize);
  size_t fetched_size = result.value;
  read_limit += fetched_size;
  size_t transfer_size = fetched_size >= to_fetch ? to_fetch : fetched_size;
  inline_memcpy(dataref.data(), buf, transfer_size);
  pos += transfer_size;
  if (result.has_error() || fetched_size < to_fetch) {
    if (!result.has_error())
      eof = true;
    else
      err = true;
  }
  return {transfer_size + available_data, result.error};
}

FileIOResult File::read_unlocked_nbf(uint8_t *data, size_t len) {
  // Check whether there is a character in the ungetc buffer.
  size_t available_data = copy_data_from_buf(data, len);
  if (available_data == len)
    return available_data;

  // Directly copy the data into |data|.
  cpp::span<uint8_t> dataref(static_cast<uint8_t *>(data) + available_data,
                             len - available_data);
  FileIOResult result =
      platform_read(get_backend(), dataref.data(), dataref.size());

  if (result.has_error() || result < dataref.size()) {
    if (!result.has_error())
      eof = true;
    else
      err = true;
  }
  return {result + available_data, result.error};
}

int File::ungetc_unlocked(int c) {
  // There is no meaning to unget if:
  // 1. You are trying to push back EOF.
  // 2. Read operations are not allowed on this file.
  // 3. The previous operation was a write operation.
  if (c == EOF || !read_allowed() || (prev_op == FileOp::WRITE) ||
      byte_pushback != EOF)
    return EOF;

  switch (orientation) {
  case Orientation::WIDE:
    err = true;
    return EOF;
  case Orientation::UNORIENTED:
    orientation = Orientation::BYTE;
    break;
  case Orientation::BYTE:
    break;
  }

  cpp::span<uint8_t> bufref(static_cast<uint8_t *>(buf), bufsize);
  if (read_limit == 0) {
    // If |read_limit| is zero, it can mean three things:
    //   a. This file was just created.
    //   b. The previous operation was a seek operation.
    //   c. The previous operation was a read operation which emptied
    //      the buffer.
    // For all the above cases, we simply write |c| at the beginning
    // of the buffer and bump |read_limit|. Note that |pos| will also
    // be zero in this case, so we don't need to adjust it.
    bufref[0] = static_cast<unsigned char>(c);
    ++read_limit;
  } else {
    // If |read_limit| is non-zero, it means that there is data in the buffer
    // from a previous read operation. Which would also mean that |pos| is not
    // zero. So, we decrement |pos| and write |c| in to the buffer at the new
    // |pos|. Scanner lookahead can restore the buffer to its beginning;
    // leave a separate slot for the caller's guaranteed pushback byte.
    if (pos == 0) {
      byte_pushback = static_cast<unsigned char>(c);
    } else {
      --pos;
      bufref[pos] = static_cast<unsigned char>(c);
    }
  }

  eof = false; // There is atleast one character that can be read now.
  prev_op = FileOp::READ;
  return c;
}

ErrorOr<int> File::seek(off_t offset, int whence) {
  FileLock lock(this);
  return seek_unlocked(offset, whence);
}

ErrorOr<int> File::seek_unlocked(off_t offset, int whence) {
  if (prev_op == FileOp::WRITE && (pos || wide_size)) {
    int error = flush_unlocked();
    if (error)
      return Error(error);
  } else if (prev_op == FileOp::READ && whence == SEEK_CUR) {
    // More data could have been read out from the platform file than was
    // required. So, we have to adjust the offset we pass to platform seek
    // function. Note that read_limit >= pos is always true.
    offset -= (read_limit - pos) + wide_pushback_bytes + (byte_pushback != EOF);
  }
  auto result = platform_seek(get_backend(), offset, whence);
  if (!result.has_value())
    return Error(result.error());

  pos = read_limit = 0;
  wide_pushback_bytes = 0;
  byte_pushback = EOF;
  prev_op = FileOp::SEEK;
  // Reset the eof flag as a seek might move the file position to some place
  // readable.
  eof = false;
  if (orientation == Orientation::WIDE ||
      orientation == Orientation::UNORIENTED)
    mbstate = internal::mbstate();

  return 0;
}

ErrorOr<off_t> File::tell() {
  FileLock lock(this);
  return tell_unlocked();
}

ErrorOr<off_t> File::tell_unlocked() {
  auto seek_target = prev_op == FileOp::WRITE && (pos || wide_size) &&
                             (mode & ModeFlags(OpenMode::APPEND))
                         ? SEEK_END
                         : SEEK_CUR;
  auto result = platform_seek(get_backend(), 0, seek_target);
  if (!result.has_value() || result.value() < 0)
    return Error(result.error());
  off_t platform_offset = result.value();
  if (prev_op == FileOp::READ)
    return platform_offset - (read_limit - pos) - wide_pushback_bytes -
           (byte_pushback != EOF);
  if (prev_op == FileOp::WRITE)
    return platform_offset + pos + pending_wide_bytes();
  return platform_offset;
}

ErrorOr<File::Position> File::get_position() {
  FileLock lock(this);
  auto offset = tell_unlocked();
  if (!offset.has_value())
    return Error(offset.error());
  return Position{offset.value(), mbstate};
}

ErrorOr<int> File::set_position(const Position &position) {
  FileLock lock(this);
  auto result = seek_unlocked(position.offset, SEEK_SET);
  if (result.has_value())
    mbstate = position.state;
  return result;
}

int File::flush_unlocked() {
  if (prev_op == FileOp::WRITE && wide_size) {
    int error = flush_wide_unlocked();
    if (error)
      return error;
  }
  if (prev_op == FileOp::WRITE && pos > 0) {
    FileIOResult buf_result = platform_write(get_backend(), buf, pos);
    const size_t consumed = buf_result.value < pos ? buf_result.value : pos;
    for (size_t i = consumed; i < pos; ++i)
      buf[i - consumed] = buf[i];
    pos -= consumed;
    if (buf_result.has_error() || pos > 0) {
      err = true;
      return buf_result.error ? buf_result.error : EIO;
    }
  } else if (prev_op == FileOp::READ) {
    size_t unread =
        read_limit - pos + wide_pushback_bytes + (byte_pushback != EOF);
    if (unread > 0) {
      if (!platform_seek(get_backend(), -static_cast<off_t>(unread), SEEK_CUR)
               .has_value()) {
        // We ignore seek errors for non-seekable files (like pipes) as per
        // POSIX.
      }
    }
    pos = read_limit = 0;
    byte_pushback = EOF;
    wide_pushback_bytes = 0;
    mbstate = internal::mbstate();
  }
  if (platform_flush) {
    int error = platform_flush(get_backend());
    if (error)
      err = true;
    return error;
  }
  return 0;
}

int File::set_buffer(void *buffer, size_t size, int buffer_mode) {
  // We do not need to lock the file as this method should be called before
  // other operations are performed on the file.
  if (buffer != nullptr && size == 0)
    return EINVAL;

  switch (buffer_mode) {
  case _IOFBF:
  case _IOLBF:
  case _IONBF:
    break;
  default:
    return EINVAL;
  }

  if (buffer == nullptr && size != 0 && buffer_mode != _IONBF) {
    // We exclude the case of buffer_mode == _IONBF in this branch
    // because we don't need to allocate buffer in such a case.
    if (own_buf) {
      // This is one of the places where a C allocation function is used
      // as C++ does not have an equivalent of realloc.
      auto *new_buf = reinterpret_cast<uint8_t *>(realloc(buf, size));
      if (new_buf == nullptr)
        return ENOMEM;
      buf = new_buf;
    } else {
      AllocChecker ac;
      buf = new (ac) uint8_t[size];
      if (!ac)
        return ENOMEM;
      own_buf = true;
    }
    bufsize = size;
    // TODO: Handle allocation failures.
  } else {
    if (own_buf)
      delete[] buf;
    if (buffer_mode != _IONBF) {
      buf = static_cast<uint8_t *>(buffer);
      bufsize = size;
    } else {
      // We don't need any buffer.
      buf = nullptr;
      bufsize = 0;
    }
    own_buf = false;
  }
  bufmode = buffer_mode;
  delete[] wide_output;
  wide_output = nullptr;
  wide_capacity = wide_size = wide_prefix = 0;
  adjust_buf();
  return 0;
}

File::ModeFlags File::mode_flags(const char *mode) {
  // First character in |mode| should be 'a', 'r' or 'w'.
  if (*mode != 'a' && *mode != 'r' && *mode != 'w')
    return 0;

  // There should be exaclty one main mode ('a', 'r' or 'w') character.
  // If there are more than one main mode characters listed, then
  // we will consider |mode| as incorrect and return 0;
  int main_mode_count = 0;

  ModeFlags flags = 0;
  for (; *mode != '\0'; ++mode) {
    switch (*mode) {
    case 'r':
      flags |= static_cast<ModeFlags>(OpenMode::READ);
      ++main_mode_count;
      break;
    case 'w':
      flags |= static_cast<ModeFlags>(OpenMode::WRITE);
      ++main_mode_count;
      break;
    case '+':
      flags |= static_cast<ModeFlags>(OpenMode::PLUS);
      break;
    case 'b':
      flags |= static_cast<ModeFlags>(ContentType::BINARY);
      break;
    case 'a':
      flags |= static_cast<ModeFlags>(OpenMode::APPEND);
      ++main_mode_count;
      break;
    case 'x':
      flags |= static_cast<ModeFlags>(CreateType::EXCLUSIVE);
      break;
    case 'e':
      flags |= static_cast<ModeFlags>(OpenMode::CLOEXEC);
      break;
    default:
      return 0;
    }
  }

  if (main_mode_count != 1)
    return 0;

  return flags;
}

ErrorOr<size_t> File::encode_wide(wchar_t wc, uint8_t *bytes) const {
  internal::mbstate state;
  internal::CharacterConverter converter(&state, wide_utf8);
  int error = converter.push(static_cast<char32_t>(wc));
  if (error)
    return Error(error);
  size_t size = 0;
  while (!converter.isEmpty()) {
    auto byte = converter.pop<char8_t>();
    if (!byte.has_value())
      return Error(byte.error());
    bytes[size++] = byte.value();
  }
  return size;
}

size_t File::pending_wide_bytes() const {
  size_t size = 0;
  for (size_t i = 0; i < wide_size; ++i) {
    uint8_t bytes[4];
    size += encode_wide(wide_output[i], bytes).value();
  }
  return size - wide_prefix;
}

int File::flush_wide_unlocked() {
  while (wide_size) {
    uint8_t bytes[256];
    size_t ends[256];
    size_t length = 0, characters = 0;
    while (characters < wide_size && length <= sizeof(bytes) - 4) {
      uint8_t encoded[4];
      size_t size = encode_wide(wide_output[characters], encoded).value();
      size_t skip = characters == 0 ? wide_prefix : 0;
      inline_memcpy(bytes + length, encoded + skip, size - skip);
      length += size - skip;
      ends[characters++] = length;
    }
    auto result = platform_write(get_backend(), bytes, length);
    size_t consumed = result.value < length ? result.value : length;
    size_t complete = 0;
    while (complete < characters && ends[complete] <= consumed)
      ++complete;
    // Keep the first incomplete character, without replaying its written prefix.
    wide_prefix = complete ? consumed - ends[complete - 1]
                           : wide_prefix + consumed;
    for (size_t i = complete; i < wide_size; ++i)
      wide_output[i - complete] = wide_output[i];
    wide_size -= complete;
    if (!wide_size)
      wide_prefix = 0;
    if (result.has_error() || consumed != length) {
      err = true;
      return result.has_error() ? result.error : EIO;
    }
  }
  return 0;
}

FileIOResult File::write_unlocked(const wchar_t *ws, size_t len) {
  switch (orientation) {
  case Orientation::BYTE:
    err = true;
    return {0, EINVAL};
  case Orientation::UNORIENTED:
    try_set_orientation_unlocked(Orientation::WIDE);
    break;
  case Orientation::WIDE:
    break;
  }
  if (!len)
    return size_t(0);
  if (!write_allowed()) {
    err = true;
    return {0, EBADF};
  }
  if (platform_wide_write) {
    begin_output();
    auto result = platform_wide_write(get_backend(), ws, len);
    if (result.has_error() || result.value != len) {
      err = true;
      if (!result.has_error())
        result.error = EIO;
    }
    return result;
  }
  if (bufmode != _IONBF && !wide_output) {
    size_t capacity = bufsize / sizeof(wchar_t);
    if (!capacity)
      capacity = 1;
    AllocChecker ac;
    wchar_t *storage = new (ac) wchar_t[capacity];
    if (!ac) {
      err = true;
      return {0, ENOMEM};
    }
    wide_output = storage;
    wide_capacity = capacity;
  }
  begin_output();

  size_t written = 0;
  for (size_t i = 0; i < len; ++i) {
    uint8_t buffer[4];
    auto encoded = encode_wide(ws[i], buffer);
    if (!encoded.has_value()) {
      err = true;
      return {written, encoded.error()};
    }
    if (bufmode == _IONBF) {
      auto result = write_unlocked_impl(buffer, encoded.value());
      if (result.has_error())
        return {written, result.error};
      if (result.value != encoded.value()) {
        err = true;
        return {written, EIO};
      }
    } else {
      if (wide_size == wide_capacity) {
        int error = flush_wide_unlocked();
        if (error)
          return {written, error};
      }
      wide_output[wide_size++] = ws[i];
      if (bufmode == _IOLBF && ws[i] == L'\n') {
        int error = flush_unlocked();
        if (error)
          return {written, error};
      }
    }
    ++written;
  }
  return {written, 0};
}

FileIOResult File::read_unlocked(wchar_t *ws, size_t len) {
  switch (orientation) {
  case Orientation::BYTE:
    err = true;
    return {0, EINVAL};
  case Orientation::UNORIENTED:
    try_set_orientation_unlocked(Orientation::WIDE);
    break;
  case Orientation::WIDE:
    break;
  }

  size_t read_count = 0;
  for (size_t i = 0; i < len; ++i) {
    if (wide_pushback_bytes != 0) {
      ws[i] = wide_pushback;
      wide_pushback_bytes = 0;
      ++read_count;
      continue;
    }
    internal::CharacterConverter cr(&mbstate, wide_utf8);
    while (!cr.isFull()) {
      uint8_t byte;
      auto read_res = read_unlocked_impl(&byte, 1);
      if (read_res.has_error())
        return {read_count, read_res.error};
      if (read_res.value == 0) { // EOF
        if (cr.isEmpty())
          return {read_count, 0};
        err = true;
        return {read_count, EILSEQ}; // Incomplete character at EOF
      }
      int push_err = cr.push(static_cast<char8_t>(byte));
      if (push_err != 0) {
        err = true;
        return {read_count, push_err};
      }
    }
    auto pop_res = cr.pop<char32_t>();
    if (!pop_res.has_value()) {
      err = true;
      return {read_count, pop_res.error()};
    }
    ws[i] = static_cast<wchar_t>(pop_res.value());
    ++read_count;
  }
  return {read_count, 0};
}

ErrorOr<wint_t> File::ungetwc_unlocked(wint_t wc) {
  // There is no meaning to unget if:
  // 1. You are trying to push back EOF.
  // 2. Read operations are not allowed on this file.
  // 3. The previous operation was a write operation.
  if (wc == WEOF || !read_allowed() || (prev_op == FileOp::WRITE))
    return WEOF;
  switch (orientation) {
  case Orientation::BYTE:
    err = true;
    return WEOF;
  case Orientation::UNORIENTED:
    try_set_orientation_unlocked(Orientation::WIDE);
    break;
  case Orientation::WIDE:
    break;
  }

  if (wide_pushback_bytes != 0)
    return WEOF;

  char mb_buf[4];
  internal::mbstate state;
  auto result =
      internal::wcrtomb(mb_buf, static_cast<wchar_t>(wc), &state, wide_utf8);
  if (!result.has_value())
    return Error(result.error());

  wide_pushback = static_cast<wchar_t>(wc);
  wide_pushback_bytes = result.value();
  prev_op = FileOp::READ;
  eof = false;
  return wc;
}

} // namespace LIBC_NAMESPACE_DECL
