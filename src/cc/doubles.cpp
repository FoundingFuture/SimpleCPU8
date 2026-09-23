#include "cc/doubles.h"

#include <cstring>

#include "devices/acp_ports.h"

namespace sc8::cc {

namespace {

int fmtNamed(std::string_view name) {
  for (const auto& nv : acp::FMTS) if (nv.name == name) return nv.value;
  return 0;
}

}  // namespace

const int F64 = fmtNamed("ACP_F64");
const int I64 = fmtNamed("ACP_I64");

std::string slotAt(int n) { return std::string(DT) + " + " + std::to_string(n * DSIZE); }

void acpRun(const Emit& e, const std::string& block, const std::string& cmd, int fmt,
            std::optional<int> rfmt) {
  e("        OUT ACP_ADDR_HI, (" + block + ") >> 8");
  e("        OUT ACP_ADDR_LO, (" + block + ") & 255");
  e("        OUT ACP_FMT, " + std::to_string(fmt));
  if (rfmt) e("        OUT ACP_RFMT, " + std::to_string(*rfmt));
  e("        OUT ACP_ROWS, 1");
  e("        OUT ACP_COLS, 1");
  e("        OUT ACP_CMD, " + cmd);
}

void moveConst(const Emit& e, const std::string& dst, const std::string& src, int len) {
  e("        OUT GPU_FROM_HI, (" + src + ") >> 8");
  e("        OUT GPU_FROM_LO, (" + src + ") & 255");
  e("        OUT GPU_DEST_HI, (" + dst + ") >> 8");
  e("        OUT GPU_DEST_LO, (" + dst + ") & 255");
  e("        OUT GPU_LEN_HI, " + std::to_string((len >> 8) & 0xff));
  e("        OUT GPU_LEN_LO, " + std::to_string(len & 0xff));
  e("        OUT GPU_CMD, CMD_RAM_MOVE");
}

void moveDyn(const Emit& e, const Where& dst, const Where& src, int len) {
  auto put = [&](const std::string& hi, const std::string& lo, const Where& w) {
    if (!w.isZp) {
      e("        OUT " + hi + ", (" + w.text + ") >> 8");
      e("        OUT " + lo + ", (" + w.text + ") & 255");
    } else {
      e("        LD A <- [" + w.text + "]");
      e("        OUT " + hi + " <- A");
      e("        LD A <- [" + w.text + "+1]");
      e("        OUT " + lo + " <- A");
    }
  };
  put("GPU_FROM_HI", "GPU_FROM_LO", src);
  put("GPU_DEST_HI", "GPU_DEST_LO", dst);
  e("        OUT GPU_LEN_HI, " + std::to_string((len >> 8) & 0xff));
  e("        OUT GPU_LEN_LO, " + std::to_string(len & 0xff));
  e("        OUT GPU_CMD, CMD_RAM_MOVE");
}

std::vector<uint8_t> f64Bytes(double v) {
  uint64_t bits;
  std::memcpy(&bits, &v, 8);
  std::vector<uint8_t> out(8);
  for (int i = 0; i < 8; i++) out[static_cast<size_t>(i)] = static_cast<uint8_t>(bits >> (56 - 8 * i));
  return out;
}

std::string doubleOp(const std::string& op) {
  if (op == "+") return "ACP_ADD";
  if (op == "-") return "ACP_SUB";
  if (op == "*") return "ACP_MUL";
  if (op == "/") return "ACP_DIV";
  return "";
}

}  // namespace sc8::cc
