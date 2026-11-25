#pragma once
#include <string>
#include <vector>

struct OpdsServer {
  std::string url;
  std::string username;
  std::string password;
};

class OpdsServerStore {
 public:
  std::vector<OpdsServer> servers;
  const std::vector<OpdsServer>& getServers() const { return servers; }
};

inline OpdsServerStore OPDS_STORE;
