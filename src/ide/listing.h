// The listings the MICROCODE view's Listing tab chooses between. They
// are the microcode set's rows, the assembled program, and the C or the
// BASIC the program came from. Only the standard library is used here,
// so the tests need no window.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sc8 {

// From the machine's level up, in the order the tab offers them.
enum class Listing { Microcode, Assembly, C, Basic };
constexpr int LISTING_COUNT = 4;

// The name --listing takes: microcode, assembly, c, basic.
const char* listingKey(Listing listing);
// The name on the tab's choice.
const char* listingLabel(Listing listing);
// A listing by its key, for --listing.
std::optional<Listing> listingNamed(std::string_view key);

// What the loaded program has a listing of. The assembly is always there.
struct Listings {
  bool microcode = false;  // the set shows its rows: every set but the sealed one
  bool c = false;          // the project has a C document
  bool basic = false;      // the project has a BASIC document
};
bool hasListing(const Listings& have, Listing listing);

// The listing on screen: the chosen one when the program has it. Without
// it the microcode, and the assembly when the set is sealed.
Listing listingShown(Listing chosen, const Listings& have);

// What a choice says when the program has no such listing. Null for the
// assembly, which is never missing.
const char* listingMissing(Listing listing);

// The document a C or a BASIC listing shows, out of the project's
// documents of that language. It is the one the machine stands in, else
// the one shown last, else the first. Empty when there is none.
std::string listingDoc(const std::string& standing, const std::string& last, const std::vector<std::string>& names);

}  // namespace sc8
