#pragma once

namespace storage {

// Mounts LittleFS (formatting it if the mount fails). The app's boot: no writes.
bool mount();

// mount() plus a write/read check (Stage 0 hardware test only: it writes on every boot).
bool fsSelfTest();

}  // namespace storage
