#include "init/config.h"

#include <charconv>
#include <sstream>

#include "init/property_store.h"

namespace mini {

namespace {

std::vector<std::string> splitWords(const std::string& line) {
  std::istringstream in(line);
  std::vector<std::string> words;
  std::string w;
  while (in >> w) words.push_back(w);
  return words;
}

}  // namespace

Result<InitConfig> parseInitConfig(std::string_view text, std::string& error) {
  InitConfig config;
  int lineNo = 0;
  auto fail = [&](const std::string& what) {
    error = "line " + std::to_string(lineNo) + ": " + what;
    return Status::InvalidArgument;
  };

  std::istringstream in{std::string(text)};
  std::string line;
  while (std::getline(in, line)) {
    ++lineNo;
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    std::vector<std::string> w = splitWords(line);
    if (w.empty()) continue;

    if (w[0] == "setprop") {
      if (w.size() < 3) return fail("setprop needs a key and a value");
      if (!PropertyStore::isValidKey(w[1])) return fail("invalid property key '" + w[1] + "'");
      std::string value = w[2];
      for (size_t i = 3; i < w.size(); ++i) value += " " + w[i];
      config.properties.emplace_back(w[1], value);
    } else if (w[0] == "service") {
      if (w.size() < 3) return fail("service needs a name and a path");
      if (!PropertyStore::isValidKey(w[1])) return fail("invalid service name '" + w[1] + "'");
      for (const ServiceDef& s : config.services) {
        if (s.name == w[1]) return fail("duplicate service '" + w[1] + "'");
      }
      ServiceDef def;
      def.name = w[1];
      def.path = w[2];
      def.args.assign(w.begin() + 3, w.end());
      config.services.push_back(std::move(def));
    } else if (w[0] == "oneshot" || w[0] == "restart_delay") {
      // Options belong to the most recent `service` line.
      if (config.services.empty()) return fail("'" + w[0] + "' outside a service");
      ServiceDef& def = config.services.back();
      if (w[0] == "oneshot") {
        def.oneshot = true;
      } else {
        int ms = 0;
        if (w.size() != 2 ||
            std::from_chars(w[1].data(), w[1].data() + w[1].size(), ms).ec != std::errc() ||
            ms < 0) {
          return fail("restart_delay needs a non-negative number of milliseconds");
        }
        def.restartDelay = std::chrono::milliseconds(ms);
      }
    } else {
      return fail("unknown directive '" + w[0] + "'");
    }
  }
  return config;
}

}  // namespace mini
