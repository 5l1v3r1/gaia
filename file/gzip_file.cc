
//
#include "file/gzip_file.h"
#include <zlib.h>

#include "absl/strings/str_cat.h"
#include "base/logging.h"

using util::Status;
using util::StatusCode;

namespace file {

GzipFile::GzipFile(StringPiece file_name, unsigned level) : WriteFile(file_name), level_(level) {
}

bool GzipFile::Open() {
  gz_file_ = gzopen(create_file_name_.c_str(), "wb");
  if (gz_file_ == nullptr) {
    LOG(WARNING) << "Can't open " << create_file_name_
                << " (errno = " << StatusFileError() << ").";
    return false;
  }
  gzbuffer(gz_file_, 1 << 16);
  gzsetparams(gz_file_, level_, Z_DEFAULT_STRATEGY);

  return true;
}

bool GzipFile::Close() {
  gzclose_w(gz_file_);

  delete this;

  return true;
}

Status GzipFile::Write(const uint8* buffer, uint64 length) {
  CHECK_GT(length, 0);

  unsigned bytes = gzwrite(gz_file_, buffer, length);
  if (bytes == 0) {
    int err;
    const char* str = gzerror(gz_file_, &err);
    return Status(StatusCode::IO_ERROR, absl::StrCat(str, " ", err));
  }
  CHECK_EQ(bytes, length);

  return Status::OK;
}

}  // namespace file
