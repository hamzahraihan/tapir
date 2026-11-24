#pragma once

#include "history_codec.h"

namespace tapir::history_store {

// Reads history.dat. A missing file is normal; any corruption backs the file
// up once and returns empty history after logging a reason-only line.
history::Records Load();

// Writes via history.dat.tmp and an atomic replace. Returns false when the
// write failed; the in-memory history must be kept and retried on next change.
bool Save(bool encrypt, const history::Records& records);

}  // namespace tapir::history_store
