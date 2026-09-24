#include "basic/program.h"

#include <algorithm>
#include <map>

namespace sc8::basic {

std::vector<uint8_t> encodeProgram(const std::string& text) {
  // The map keeps the lines sorted and makes a repeated number a
  // replacement, which is what the interpreter does on entry.
  std::map<int, std::string> lines;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t i = 0;
    while (i < line.size() && line[i] == ' ') i++;
    if (i >= line.size() || line[i] < '0' || line[i] > '9') continue;
    int n = 0;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
      n = n * 10 + (line[i] - '0');
      i++;
    }
    while (i < line.size() && line[i] == ' ') i++;
    std::string body = line.substr(i);
    if (body.size() > 250) body.resize(250);
    if (n <= 0 || n > 65535) continue;
    if (body.empty()) lines.erase(n);
    else lines[n] = body;
  }
  std::vector<uint8_t> out;
  for (const auto& [n, body] : lines) {
    const size_t rec = body.size() + 4;
    if (out.size() + rec + 3 >= PROGRAM_MAX) break;
    out.push_back(static_cast<uint8_t>(n >> 8));
    out.push_back(static_cast<uint8_t>(n & 255));
    out.push_back(static_cast<uint8_t>(rec));
    out.insert(out.end(), body.begin(), body.end());
    out.push_back(0);
  }
  out.push_back(0);
  out.push_back(0);
  out.push_back(3);
  return out;
}

std::string decodeProgram(std::span<const uint8_t> bytes) {
  std::string out;
  size_t p = 0;
  while (p + 3 <= bytes.size()) {
    const int n = (bytes[p] << 8) | bytes[p + 1];
    if (n == 0) break;
    const size_t rec = bytes[p + 2];
    if (rec < 4 || p + rec > bytes.size()) break;
    out += std::to_string(n);
    out += ' ';
    for (size_t i = p + 3; i < p + rec - 1 && bytes[i]; i++) out += static_cast<char>(bytes[i]);
    out += '\n';
    p += rec;
  }
  return out;
}

}  // namespace sc8::basic
