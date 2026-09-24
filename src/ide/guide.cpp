// The guide renderer. A guide's markdown is parsed once into a vector of
// blocks: headings, paragraphs, fenced code, lists and tables. Inline
// text is split into words, each with a style, so a paragraph wraps by
// word at draw time. A block remembers its drawn height. A block outside
// the window leaves a gap of that height instead of drawing.
// The parser covers what the four guides use and nothing more.
#include "ide/guide.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "ide/panes.h"

#include "guides_data.h"

namespace sc8 {

namespace {

enum class Style { Plain, Code, Bold };

struct Word {
  std::string text;
  Style style = Style::Plain;
  bool spaceAfter = false;
};

using Words = std::vector<Word>;

enum class Kind { Heading, Paragraph, Code, List, Table };

struct Block {
  Kind kind = Kind::Paragraph;
  int level = 0;                 // a heading's level, 1 to 3
  Words words;                   // a heading or a paragraph
  std::vector<std::string> lines;  // a code block
  bool ordered = false;          // a numbered list
  std::vector<Words> items;      // the list's items
  std::vector<std::vector<Words>> rows;  // a table, the header row first
  int chapter = -1;              // the chapter this heading opens
  // The layout cache. height is the drawn height at width, or -1.
  float width = -1.0f;
  float height = -1.0f;
  float codeWidth = -1.0f;  // the widest code line
  std::vector<float> weights;  // a table's column weights
};

struct Chapter {
  std::string title;
  std::string key;  // the title and the body in lower case, for the filter
  size_t block = 0;
};

struct Doc {
  std::vector<Block> blocks;
  std::vector<Chapter> chapters;
};

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

bool endsWith(std::string_view s, std::string_view p) {
  return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

// ---- inline text

// Splits a line of markdown into styled words. A short code span is one
// word, spaces and all, so it never wraps in the middle. A link keeps its
// text and drops the target. Bold markers switch the style.
void addInline(Words& out, std::string_view s) {
  std::string cur;
  Style style = Style::Plain;
  auto flush = [&](bool space) {
    if (cur.empty()) {
      if (space && !out.empty()) out.back().spaceAfter = true;
      return;
    }
    out.push_back({cur, style, space});
    cur.clear();
  };
  size_t i = 0;
  while (i < s.size()) {
    const char c = s[i];
    if (c == '`') {
      const size_t end = s.find('`', i + 1);
      if (end != std::string_view::npos) {
        flush(false);
        // A short span, an instruction or a name, stays one word so it
        // never breaks in the middle. A long one, a quoted message, is
        // split at its spaces and wraps like prose.
        const std::string_view span = s.substr(i + 1, end - i - 1);
        if (span.size() <= 24) {
          out.push_back({std::string(span), Style::Code, false});
        } else {
          size_t at = 0;
          while (at < span.size()) {
            size_t sp = span.find(' ', at);
            if (sp == std::string_view::npos) sp = span.size();
            if (sp > at) out.push_back({std::string(span.substr(at, sp - at)), Style::Code, sp < span.size()});
            at = sp + 1;
          }
        }
        i = end + 1;
        continue;
      }
    }
    if (c == '[') {
      const size_t close = s.find("](", i);
      const size_t end = close == std::string_view::npos ? close : s.find(')', close);
      if (end != std::string_view::npos) {
        flush(false);
        addInline(out, s.substr(i + 1, close - i - 1));
        i = end + 1;
        continue;
      }
    }
    if (c == '*' && i + 1 < s.size() && s[i + 1] == '*') {
      flush(false);
      style = style == Style::Bold ? Style::Plain : Style::Bold;
      i += 2;
      continue;
    }
    if (c == ' ') {
      flush(true);
      i++;
      continue;
    }
    cur += c;
    i++;
  }
  flush(false);
}

Words inlineOf(std::string_view s) {
  Words w;
  addInline(w, trim(s));
  return w;
}

std::string plainOf(const Words& words) {
  std::string s;
  for (const Word& w : words) {
    s += w.text;
    if (w.spaceAfter) s += ' ';
  }
  return s;
}

// ---- the block parser

// A table row's cells. A bar inside a code span does not split.
std::vector<std::string_view> cellsOf(std::string_view line) {
  std::vector<std::string_view> cells;
  line = trim(line);
  if (startsWith(line, "|")) line.remove_prefix(1);
  if (endsWith(line, "|")) line.remove_suffix(1);
  size_t start = 0;
  bool code = false;
  for (size_t i = 0; i <= line.size(); i++) {
    if (i < line.size() && line[i] == '`') code = !code;
    if (i == line.size() || (line[i] == '|' && !code)) {
      cells.push_back(trim(line.substr(start, i - start)));
      start = i + 1;
    }
  }
  return cells;
}

bool isSeparatorRow(std::string_view line) {
  for (const std::string_view c : cellsOf(line)) {
    if (c.empty() || c.find_first_not_of("-:") != std::string_view::npos) return false;
  }
  return true;
}

// A numbered item is "1. text". Returns the text's offset, or 0.
size_t numberedAt(std::string_view line) {
  size_t i = 0;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) i++;
  if (i == 0 || i + 1 >= line.size() || line[i] != '.' || line[i + 1] != ' ') return 0;
  return i + 2;
}

bool isBullet(std::string_view line) { return startsWith(line, "- ") || startsWith(line, "* "); }

Doc parse(std::string_view text) {
  Doc doc;
  std::vector<std::string_view> lines;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string_view::npos) nl = text.size();
    std::string_view line = text.substr(pos, nl - pos);
    if (endsWith(line, "\r")) line.remove_suffix(1);
    lines.push_back(line);
    pos = nl + 1;
  }

  std::string chapterBody;  // the text of the open chapter, for the filter
  auto closeChapter = [&] {
    if (!doc.chapters.empty()) doc.chapters.back().key += lower(chapterBody);
    chapterBody.clear();
  };

  size_t i = 0;
  while (i < lines.size()) {
    std::string_view line = lines[i];
    if (trim(line).empty()) {
      i++;
      continue;
    }
    if (startsWith(line, "```")) {
      Block b;
      b.kind = Kind::Code;
      i++;
      while (i < lines.size() && !startsWith(lines[i], "```")) {
        b.lines.emplace_back(lines[i]);
        chapterBody += lines[i];
        chapterBody += '\n';
        i++;
      }
      i++;
      doc.blocks.push_back(std::move(b));
      continue;
    }
    if (startsWith(line, "#")) {
      int level = 0;
      while (level < static_cast<int>(line.size()) && line[static_cast<size_t>(level)] == '#') level++;
      if (level <= 3 && static_cast<size_t>(level) < line.size() && line[static_cast<size_t>(level)] == ' ') {
        Block b;
        b.kind = Kind::Heading;
        b.level = level;
        b.words = inlineOf(line.substr(static_cast<size_t>(level) + 1));
        if (level == 2) {
          closeChapter();
          b.chapter = static_cast<int>(doc.chapters.size());
          Chapter ch;
          ch.title = plainOf(b.words);
          ch.key = lower(ch.title) + '\n';
          ch.block = doc.blocks.size();
          doc.chapters.push_back(std::move(ch));
        } else {
          chapterBody += line;
          chapterBody += '\n';
        }
        doc.blocks.push_back(std::move(b));
        i++;
        continue;
      }
    }
    if (startsWith(line, "|")) {
      Block b;
      b.kind = Kind::Table;
      while (i < lines.size() && startsWith(lines[i], "|")) {
        if (!isSeparatorRow(lines[i])) {
          std::vector<Words> row;
          for (const std::string_view c : cellsOf(lines[i])) row.push_back(inlineOf(c));
          b.rows.push_back(std::move(row));
        }
        chapterBody += lines[i];
        chapterBody += '\n';
        i++;
      }
      if (!b.rows.empty()) doc.blocks.push_back(std::move(b));
      continue;
    }
    if (isBullet(line) || numberedAt(line) != 0) {
      Block b;
      b.kind = Kind::List;
      b.ordered = !isBullet(line);
      while (i < lines.size()) {
        const std::string_view l = lines[i];
        const size_t at = b.ordered ? numberedAt(l) : (isBullet(l) ? 2 : 0);
        if (at == 0) break;
        std::string item(l.substr(at));
        chapterBody += l;
        chapterBody += '\n';
        i++;
        // An indented line continues the item.
        while (i < lines.size() && startsWith(lines[i], "  ") && !trim(lines[i]).empty()) {
          item += ' ';
          item += trim(lines[i]);
          chapterBody += lines[i];
          chapterBody += '\n';
          i++;
        }
        b.items.push_back(inlineOf(item));
      }
      doc.blocks.push_back(std::move(b));
      continue;
    }
    // A paragraph runs to the next blank line or the next block opener.
    // A numbered line inside it stays text, so "255. A program" reads on.
    Block b;
    b.kind = Kind::Paragraph;
    std::string para;
    while (i < lines.size()) {
      const std::string_view l = lines[i];
      if (trim(l).empty() || startsWith(l, "```") || startsWith(l, "#") || startsWith(l, "|") || isBullet(l)) break;
      if (!para.empty()) para += ' ';
      para += trim(l);
      chapterBody += l;
      chapterBody += '\n';
      i++;
    }
    b.words = inlineOf(para);
    doc.blocks.push_back(std::move(b));
  }
  closeChapter();
  return doc;
}

const char* const GUIDE_FILES[GUIDE_COUNT] = {"basic.md", "c.md", "assembly.md", "microcode.md"};

Doc& docOf(Guide guide) {
  static Doc docs[GUIDE_COUNT];
  static bool parsed[GUIDE_COUNT] = {false, false, false, false};
  const size_t n = static_cast<size_t>(guide);
  if (!parsed[n]) {
    parsed[n] = true;
    for (unsigned long k = 0; k < GUIDES_COUNT; k++) {
      if (std::strcmp(GUIDES[k].name, GUIDE_FILES[n]) == 0) docs[n] = parse(GUIDES[k].text);
    }
  }
  return docs[n];
}

// ---- drawing

const ImVec4 HEADING_COLOR(0.55f, 0.75f, 1.0f, 1.0f);
const ImVec4 CODE_COLOR(0.95f, 0.82f, 0.55f, 1.0f);
const ImU32 CODE_BG = IM_COL32(255, 255, 255, 18);
const ImU32 BLOCK_BG = IM_COL32(255, 255, 255, 12);
// A table column up to this wide keeps its width, in pixels.
const float NARROW_COLUMN = 110.0f;

ImU32 colorOf(Style style) {
  switch (style) {
    case Style::Code: return ImGui::GetColorU32(CODE_COLOR);
    case Style::Bold: return IM_COL32(255, 255, 255, 255);
    default: return ImGui::GetColorU32(ImGuiCol_Text);
  }
}

// Lays the words out from the cursor, wrapping at width, and advances
// the cursor past them. Words go straight to the draw list, so a long
// paragraph costs no items.
void drawWords(const Words& words, float width, ImU32 plain = 0) {
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float lineH = ImGui::GetTextLineHeight();
  const float spaceW = ImGui::CalcTextSize(" ").x;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float x = 0.0f, y = 0.0f;
  for (const Word& w : words) {
    const ImVec2 size = ImGui::CalcTextSize(w.text.c_str());
    if (x > 0.0f && x + size.x > width) {
      x = 0.0f;
      y += lineH;
    }
    const ImVec2 at(origin.x + x, origin.y + y);
    if (w.style == Style::Code) {
      dl->AddRectFilled(ImVec2(at.x - 1.0f, at.y), ImVec2(at.x + size.x + 1.0f, at.y + lineH), CODE_BG, 2.0f);
    }
    const ImU32 col = w.style == Style::Plain && plain != 0 ? plain : colorOf(w.style);
    dl->AddText(at, col, w.text.c_str());
    x += size.x + (w.spaceAfter ? spaceW : 0.0f);
  }
  ImGui::Dummy(ImVec2(width, words.empty() ? 0.0f : y + lineH));
}

void drawHeading(const Block& b, float width) {
  const float base = ImGui::GetStyle().FontSizeBase;
  const float scale = b.level == 1 ? 1.6f : b.level == 2 ? 1.3f : 1.0f;
  if (b.level == 2) ImGui::Spacing();
  ImGui::PushFont(nullptr, base * scale);
  drawWords(b.words, width, ImGui::GetColorU32(HEADING_COLOR));
  ImGui::PopFont();
  if (b.level <= 2) ImGui::Separator();
}

void drawCode(Block& b, float width) {
  const ImGuiStyle& style = ImGui::GetStyle();
  const float lineH = ImGui::GetTextLineHeight();
  const ImVec2 pad(8.0f, 6.0f);
  if (b.codeWidth < 0.0f) {
    b.codeWidth = 0.0f;
    for (const std::string& l : b.lines) b.codeWidth = std::max(b.codeWidth, ImGui::CalcTextSize(l.c_str()).x);
  }
  const bool scroll = b.codeWidth + 2.0f * pad.x > width;
  const float h = static_cast<float>(b.lines.size()) * lineH + 2.0f * pad.y + (scroll ? style.ScrollbarSize : 0.0f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, BLOCK_BG);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
  if (ImGui::BeginChild("##code", ImVec2(width, h), ImGuiChildFlags_AlwaysUseWindowPadding,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    for (const std::string& l : b.lines) ImGui::TextUnformatted(l.c_str());
  }
  ImGui::EndChild();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
}

void drawList(const Block& b, float width) {
  const float indent = ImGui::CalcTextSize(b.ordered ? "00. " : "-  ").x;
  int n = 1;
  for (const Words& item : b.items) {
    char marker[16];
    if (b.ordered) std::snprintf(marker, sizeof marker, "%d.", n++);
    else std::snprintf(marker, sizeof marker, "-");
    ImGui::TextUnformatted(marker);
    ImGui::SameLine(indent, 0.0f);
    drawWords(item, width - indent);
  }
}

void drawTable(Block& b) {
  const int cols = static_cast<int>(b.rows.front().size());
  if (b.weights.empty()) {
    // A column's width is its widest cell. A narrow column keeps that
    // width, so a name is never cut. A wide one stretches and wraps.
    b.weights.assign(static_cast<size_t>(cols), 0.0f);
    for (const std::vector<Words>& row : b.rows) {
      for (size_t c = 0; c < row.size() && c < b.weights.size(); c++) {
        float w = 0.0f;
        for (const Word& word : row[c]) w = std::max(w, ImGui::CalcTextSize(word.text.c_str()).x);
        w = std::max(w, ImGui::CalcTextSize(plainOf(row[c]).c_str()).x * 0.5f);
        b.weights[c] = std::max(b.weights[c], w);
      }
    }
  }
  const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable("##t", cols, flags)) return;
  const float pad = 2.0f * ImGui::GetStyle().CellPadding.x;
  for (int c = 0; c < cols; c++) {
    const float w = b.weights[static_cast<size_t>(c)];
    if (w <= NARROW_COLUMN) ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, w + pad);
    else ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, w);
  }
  bool header = true;
  for (const std::vector<Words>& row : b.rows) {
    ImGui::TableNextRow();
    if (header) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
    for (int c = 0; c < cols; c++) {
      ImGui::TableNextColumn();
      if (static_cast<size_t>(c) >= row.size()) continue;
      drawWords(row[static_cast<size_t>(c)], ImGui::GetContentRegionAvail().x,
                header ? IM_COL32(255, 255, 255, 255) : 0);
    }
    header = false;
  }
  ImGui::EndTable();
}

void drawBlock(Block& b, float width) {
  switch (b.kind) {
    case Kind::Heading: drawHeading(b, width); break;
    case Kind::Paragraph: drawWords(b.words, width); break;
    case Kind::Code: drawCode(b, width); break;
    case Kind::List: drawList(b, width); break;
    case Kind::Table: drawTable(b); break;
  }
}

// The text column. A block outside the window is skipped through its
// cached height. A block never drawn, or drawn at another width, is
// drawn so its height is known.
void drawText(Doc& doc, GuideView& view) {
  const float top = ImGui::GetWindowPos().y;
  const float bottom = top + ImGui::GetWindowSize().y;
  const float lineH = ImGui::GetTextLineHeight();
  const float scrollY = ImGui::GetScrollY();
  const bool track = view.settle == 0 && scrollY != view.lastScroll;
  int current = view.chapter;
  for (size_t i = 0; i < doc.blocks.size(); i++) {
    Block& b = doc.blocks[i];
    const float width = ImGui::GetContentRegionAvail().x;
    const float y = ImGui::GetCursorScreenPos().y;
    if (b.chapter >= 0 && track && y <= top + lineH) current = b.chapter;
    const bool target = b.chapter >= 0 && b.chapter == view.scrollTo;
    const bool known = b.height >= 0.0f && b.width == width;
    if (known && !target && (y + b.height < top || y > bottom)) {
      ImGui::Dummy(ImVec2(width, b.height));
      continue;
    }
    ImGui::PushID(static_cast<int>(i));
    ImGui::BeginGroup();
    drawBlock(b, width);
    ImGui::EndGroup();
    ImGui::PopID();
    b.height = ImGui::GetItemRectSize().y;
    b.width = width;
    if (target) {
      // The heading goes to the window's top edge, so nothing of the
      // chapter before it shows.
      ImGui::SetScrollFromPosY(ImGui::GetItemRectMin().y - ImGui::GetWindowPos().y, 0.0f);
      view.scrollTo = -1;
      view.settle = 3;
    }
  }
  if (track) view.chapter = current;
  if (view.settle > 0) view.settle--;
  view.lastScroll = scrollY;
}

}  // namespace

const char* guideTitle(Guide guide) {
  switch (guide) {
    case Guide::Basic: return "BASIC for beginners";
    case Guide::C: return "C on SimpleCPU-8";
    case Guide::Assembly: return "Assembly on SimpleCPU-8";
    case Guide::Microcode: return "Microcode on SimpleCPU-8";
  }
  return "";
}

std::optional<Guide> guideFor(std::string_view fileName) {
  const size_t slash = fileName.find_last_of("/\\");
  const std::string name = lower(slash == std::string_view::npos ? fileName : fileName.substr(slash + 1));
  if (name == "microcode.txt") return Guide::Microcode;
  if (endsWith(name, ".bas")) return Guide::Basic;
  if (endsWith(name, ".c") || endsWith(name, ".h")) return Guide::C;
  if (endsWith(name, ".asm")) return Guide::Assembly;
  return std::nullopt;
}

void drawGuide(GuideView& view) {
  Doc& doc = docOf(view.guide);
  ImGui::BeginChild("chapters", ImVec2(220.0f, 0.0f), ImGuiChildFlags_Borders);
  ImGui::SetNextItemWidth(-1.0f);
  panes::inputLine("##guidefilter", view.filter, "filter chapters");
  const std::string needle = lower(trim(view.filter));
  for (size_t i = 0; i < doc.chapters.size(); i++) {
    const Chapter& ch = doc.chapters[i];
    if (!needle.empty() && ch.key.find(needle) == std::string::npos) continue;
    if (ImGui::Selectable(ch.title.c_str(), view.chapter == static_cast<int>(i))) {
      view.chapter = static_cast<int>(i);
      view.scrollTo = static_cast<int>(i);
    }
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
  ImGui::BeginChild("text", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  drawText(doc, view);
  ImGui::EndChild();
}

}  // namespace sc8
