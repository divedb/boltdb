#include "boltdb/db.hh"

#include "boltdb/meta.hh"
#include "boltdb/page.hh"

namespace boltdb {

std::error_code DB::FormatEmptyDatabase() noexcept {
  const int page_size = options_.PageSize();
  std::vector<std::byte> buf(page_size * 4);
  std::byte* ptr = buf.data();
  int pgid = 0;

  for (; pgid < 2; ++pgid) {
    Page* page = new (ptr) Page(PageId(pgid), PageFlag::kMeta);
    void* meta = page->GetMeta();
    new (meta) Meta(page_size, TransactionId(pgid));
    ptr += page_size;
  }

  new (ptr) Page(PageId(pgid), PageFlag::kFreelist);
  ptr += page_size;
  new (ptr) Page(PageId(pgid + 1), PageFlag::kLeaf);
  std::size_t bytes_written = handle_.Write(buf.data(), buf.size(), 0);

  if (bytes_written != buf.size()) {
    return make_error_code(Errc::kWriteFailed);
  }

  return handle_.Sync();
}

std::error_code DB::VerifyMetaPage() noexcept {
  const int page_size = options_.PageSize();
  std::vector<std::byte> buf(page_size);
  const auto bytes_read = handle_.ReadAt(buf.data(), buf.size(), 0);

  if (!bytes_read.HasValue()) return bytes_read.Error();

  if (bytes_read.Value() != buf.size()) {
    return make_error_code(Errc::kShortRead);
  }

  auto* page = reinterpret_cast<const Page*>(buf.data());

  if (page->Id() != 0) {
    return make_error_code(Errc::kInvalidDatabase);
  }

  auto* meta = page->GetMeta();

  return meta->Validate();
}

}  // namespace boltdb