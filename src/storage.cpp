#include "storage.h"

namespace storage {
namespace {
Preferences store;
}

bool begin() { return store.begin("relay", false); }

Preferences& prefs() { return store; }
}  // namespace storage
