#pragma once

#include <iosfwd>
#include <string>

namespace mm {

class DiscoveryAgent;
class Node;

int run_topic_list(DiscoveryAgent& discovery, std::ostream& out);
int run_topic_echo(Node& node, const std::string& topic, const std::string& type_name,
                   int count, std::ostream& out);
int run_topic_hz(Node& node, const std::string& topic, const std::string& type_name,
                 int window, int count, std::ostream& out);

}  // namespace mm
