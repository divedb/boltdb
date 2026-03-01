#pragma once

#include <unordered_map>

#include "boltdb/page.hh"

namespace boltdb {

class DB;
class Meta;

class Txn {
 private:
  DB* db_;
  Meta* meta_;

  std::unordered_map<PageId, Page*> pages_;
};

}  // namespace boltdb