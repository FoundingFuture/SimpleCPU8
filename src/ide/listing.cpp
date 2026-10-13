#include "ide/listing.h"

#include <algorithm>

namespace sc8 {

namespace {

// One row per listing, in the order of the enum and of the tab's choices.
struct ListingRow {
  const char* key;
  const char* label;
  const char* missing;  // what the choice says when the program has none
};

constexpr ListingRow LISTINGS[LISTING_COUNT] = {
    {"microcode", "Microcode", "the optimal set is sealed and shows no rows: Run, Microcode picks another set"},
    {"assembly", "Assembly", nullptr},
    {"c", "C", "this project has no C document"},
    {"basic", "BASIC", "this project has no BASIC document"},
};

const ListingRow& row(Listing listing) { return LISTINGS[static_cast<int>(listing)]; }

}  // namespace

const char* listingKey(Listing listing) { return row(listing).key; }
const char* listingLabel(Listing listing) { return row(listing).label; }
const char* listingMissing(Listing listing) { return row(listing).missing; }

std::optional<Listing> listingNamed(std::string_view key) {
  for (int i = 0; i < LISTING_COUNT; i++) {
    if (key == LISTINGS[i].key) return static_cast<Listing>(i);
  }
  return std::nullopt;
}

bool hasListing(const Listings& have, Listing listing) {
  switch (listing) {
    case Listing::Microcode: return have.microcode;
    case Listing::Assembly: return true;
    case Listing::C: return have.c;
    case Listing::Basic: return have.basic;
  }
  return false;
}

Listing listingShown(Listing chosen, const Listings& have) {
  if (hasListing(have, chosen)) return chosen;
  return have.microcode ? Listing::Microcode : Listing::Assembly;
}

std::string listingDoc(const std::string& standing, const std::string& last, const std::vector<std::string>& names) {
  if (names.empty()) return "";
  for (const std::string* wanted : {&standing, &last}) {
    if (std::find(names.begin(), names.end(), *wanted) != names.end()) return *wanted;
  }
  return names.front();
}

}  // namespace sc8
