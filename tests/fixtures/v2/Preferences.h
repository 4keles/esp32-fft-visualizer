#pragma once
#include <map>
#include <string>
#include <vector>
namespace fake {
inline std::map<std::string,int32_t> nvs;
inline int nvsWrites = 0;
inline bool failNvs = false;
inline std::vector<std::string> nvsKeys;
inline std::vector<std::string> nvsTypes;
}
class Preferences {
 public:
  bool begin(const char*, bool) { return true; }
  bool isKey(const char* k) { return fake::nvs.count(k); }
  int32_t getInt(const char* k, int32_t d) { return isKey(k) ? fake::nvs[k] : d; }
  bool getBool(const char* k, bool d) { return isKey(k) ? fake::nvs[k] : d; }
  size_t putInt(const char* k, int32_t v) {
    ++fake::nvsWrites; fake::nvsKeys.push_back(k);
    fake::nvsTypes.push_back("int");
    fake::trace.push_back("nvs:" + std::string(k) + ":" + std::to_string(v));
    if (fake::failNvs) return 0;
    fake::nvs[k]=v; return 4;
  }
  size_t putBool(const char* k, bool v) {
    ++fake::nvsWrites; fake::nvsKeys.push_back(k); fake::nvsTypes.push_back("bool");
    fake::trace.push_back("nvs:" + std::string(k) + ":" + std::to_string(v));
    if (fake::failNvs) return 0;
    fake::nvs[k]=v; return 1;
  }
  bool remove(const char* k) { ++fake::nvsWrites; fake::nvsKeys.push_back(k); return fake::nvs.erase(k); }
  bool clear() { ++fake::nvsWrites; fake::nvs.clear(); return true; }
};
