#include <AssetCacheStorage.h>
#include <Memory.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
  char temporary[] = "/tmp/crossink-assets-XXXXXX";
  assert(mkdtemp(temporary));
  const std::filesystem::path root(temporary);
  const auto flashPath = root / "factory.bin";
  const auto sdPath = root / ".crosspoint/asset-cache.bin";
  assert(setenv("CROSSINK_LANGUAGE_FLASH", flashPath.c_str(), 1) == 0);
  assert(setenv("CROSSINK_TEST_SD", temporary, 1) == 0);
  const std::string factory(0x14000, '\x5a');
  std::ofstream(flashPath, std::ios::binary).write(factory.data(), factory.size());
  AssetCacheStorage storage;
#ifdef CROSSINK_ASSET_CACHE_TEST_PSRAM
  assert(storage.begin(false) && storage.usesSd());
  assert(!std::filesystem::exists(sdPath));
  uint8_t data[128];
  assert(storage.read(0, data, sizeof(data)));
  for (auto byte : data) assert(byte == 0xff);
  assert(storage.begin(true));
  assert(std::filesystem::file_size(sdPath) == storage.size());
  std::memset(data, 0x42, sizeof(data));
  assert(storage.write(0, data, sizeof(data)));
  AssetCacheStorage::Mapping snapshot;
  assert(storage.map(0, sizeof(data), snapshot));
  assert(snapshot.psram && snapshot.data[0] == 0x42);
  // Another adapter (language vs hyphenation) can use the same image with no
  // persistent open file handles. The active snapshot remains pinned.
  AssetCacheStorage other;
  assert(other.begin());
  assert(other.erase(0, 4096));
  assert(other.read(0, data, sizeof(data)) && data[0] == 0xff);
  assert(snapshot.data[0] == 0x42);
  assert(!other.write(other.size() - 1, data, sizeof(data)));
  assert(!other.erase(1, 4096));
  failSnapshotAllocation = true;
  AssetCacheStorage::Mapping failed;
  assert(!other.map(0, sizeof(data), failed) && !failed.data);
  failSnapshotAllocation = false;
  // Removing SD after activation cannot invalidate pointers.
  std::filesystem::remove(sdPath);
  assert(!other.erase(0, 4096));
  assert(!other.write(0, data, sizeof(data)));
  assert(snapshot.data[0] == 0x42);
  AssetCacheStorage::unmap(snapshot);
  assert(!snapshot.data && !snapshot.psram);
#else
  // A C3-like device must not substitute scarce internal RAM for PSRAM.
  assert(!storage.begin(true) && !storage.usesSd());
  assert(!std::filesystem::exists(sdPath));
#endif
  std::ifstream file(flashPath, std::ios::binary);
  const std::string actual((std::istreambuf_iterator<char>(file)), {});
  file.close();
  assert(actual == factory);
  std::filesystem::remove_all(root);
}
