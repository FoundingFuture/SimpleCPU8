// The listings the MICROCODE view's Listing tab chooses between. Which
// ones a program has and which one is on screen. The document a C or a
// BASIC listing shows.

#include <doctest.h>

#include <optional>
#include <string>
#include <vector>

#include "ide/listing.h"

using sc8::hasListing;
using sc8::Listing;
using sc8::LISTING_COUNT;
using sc8::listingDoc;
using sc8::listingKey;
using sc8::listingLabel;
using sc8::listingMissing;
using sc8::listingNamed;
using sc8::Listings;
using sc8::listingShown;

TEST_SUITE("the MICROCODE view's listings") {
  TEST_CASE("--listing names each listing") {
    CHECK(listingNamed("microcode") == Listing::Microcode);
    CHECK(listingNamed("assembly") == Listing::Assembly);
    CHECK(listingNamed("c") == Listing::C);
    CHECK(listingNamed("basic") == Listing::Basic);
  }

  TEST_CASE("--listing refuses a name that is not a listing") {
    CHECK_FALSE(listingNamed("Microcode"));
    CHECK_FALSE(listingNamed("rows"));
    CHECK_FALSE(listingNamed(""));
  }

  TEST_CASE("a listing's key names it again, and its label is the language") {
    for (int i = 0; i < LISTING_COUNT; i++) {
      const Listing l = static_cast<Listing>(i);
      CHECK(listingNamed(listingKey(l)) == l);
    }
    CHECK(std::string(listingLabel(Listing::Microcode)) == "Microcode");
    CHECK(std::string(listingLabel(Listing::Assembly)) == "Assembly");
    CHECK(std::string(listingLabel(Listing::C)) == "C");
    CHECK(std::string(listingLabel(Listing::Basic)) == "BASIC");
  }

  TEST_CASE("the assembly is there for every program") {
    CHECK(hasListing(Listings{}, Listing::Assembly));
    CHECK_FALSE(hasListing(Listings{}, Listing::Microcode));
    CHECK_FALSE(hasListing(Listings{}, Listing::C));
    CHECK_FALSE(hasListing(Listings{}, Listing::Basic));
  }

  TEST_CASE("each other listing is there when the program has it") {
    CHECK(hasListing(Listings{.microcode = true}, Listing::Microcode));
    CHECK(hasListing(Listings{.c = true}, Listing::C));
    CHECK(hasListing(Listings{.basic = true}, Listing::Basic));
    CHECK_FALSE(hasListing(Listings{.microcode = true, .basic = true}, Listing::C));
  }

  TEST_CASE("a chosen listing the program has is the one on screen") {
    const Listings all{.microcode = true, .c = true, .basic = true};
    for (int i = 0; i < LISTING_COUNT; i++) {
      const Listing l = static_cast<Listing>(i);
      CHECK(listingShown(l, all) == l);
    }
  }

  TEST_CASE("a missing listing gives way to the microcode") {
    const Listings rows{.microcode = true};
    CHECK(listingShown(Listing::C, rows) == Listing::Microcode);
    CHECK(listingShown(Listing::Basic, rows) == Listing::Microcode);
  }

  // The optimal set is sealed and has no rows to list.
  TEST_CASE("without the microcode a missing listing gives way to the assembly") {
    CHECK(listingShown(Listing::Microcode, Listings{}) == Listing::Assembly);
    CHECK(listingShown(Listing::C, Listings{.basic = true}) == Listing::Assembly);
    CHECK(listingShown(Listing::Microcode, Listings{.c = true}) == Listing::Assembly);
  }

  TEST_CASE("only a missing listing has a reason, and the assembly has none") {
    CHECK(listingMissing(Listing::Assembly) == nullptr);
    for (const Listing l : {Listing::Microcode, Listing::C, Listing::Basic}) {
      REQUIRE(listingMissing(l) != nullptr);
      CHECK(std::string(listingMissing(l)).size() > 10);
    }
  }
}

TEST_SUITE("the document a C or a BASIC listing shows") {
  const std::vector<std::string> NAMES = {"main.c", "ball.c", "ball.h"};

  TEST_CASE("the document the machine stands in") {
    CHECK(listingDoc("ball.c", "main.c", NAMES) == "ball.c");
  }

  // The startup code and the libraries are in no document of the project.
  TEST_CASE("the one shown last while the machine stands in none") {
    CHECK(listingDoc("", "ball.h", NAMES) == "ball.h");
    CHECK(listingDoc("__libc.c", "ball.h", NAMES) == "ball.h");
  }

  TEST_CASE("the first one when none was shown, or the last one left the project") {
    CHECK(listingDoc("", "", NAMES) == "main.c");
    CHECK(listingDoc("", "gone.c", NAMES) == "main.c");
  }

  TEST_CASE("nothing when the project has no such document") {
    CHECK(listingDoc("main.c", "main.c", {}) == "");
  }
}
