#include <OpdsParser.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
bool parseSingleBookEntry(OpdsEntry& entryOut, const char* href, const char* type = "application/epub+zip") {
  const std::string xml = std::string(R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-1</id>
    <link rel="http://opds-spec.org/acquisition" type=")") +
                          type + R"(" href=")" + href + R"("/>
  </entry>
</feed>)";

  std::vector<OpdsEntry> entries;
  OpdsParser parser;
  parser.onEntryParsed = [&](OpdsEntry e) { entries.push_back(std::move(e)); };
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();

  if (parser.error()) {
    ADD_FAILURE() << "parser.error() returned true";
    return false;
  }
  if (entries.size() != 1) {
    ADD_FAILURE() << "entries.size() == " << entries.size() << ", expected 1";
    return false;
  }
  entryOut = entries.front();
  return true;
}

bool assertSingleFormat(const OpdsEntry& entry, const char* formatKey, const char* fileExtension) {
  if (entry.type != OpdsEntryType::BOOK) {
    ADD_FAILURE() << "entry.type != OpdsEntryType::BOOK";
    return false;
  }
  if (entry.acquisitionLinks.size() != 1) {
    ADD_FAILURE() << "entry.acquisitionLinks.size() == " << entry.acquisitionLinks.size() << ", expected 1";
    return false;
  }
  if (entry.acquisitionLinks[0].formatKey != formatKey) {
    ADD_FAILURE() << "formatKey actual='" << entry.acquisitionLinks[0].formatKey << "' expected='" << formatKey << "'";
    return false;
  }
  if (entry.acquisitionLinks[0].fileExtension != fileExtension) {
    ADD_FAILURE() << "fileExtension actual='" << entry.acquisitionLinks[0].fileExtension << "' expected='"
                  << fileExtension << "'";
    return false;
  }
  return true;
}
}  // namespace

TEST(OpdsParser, EpubExtension) {
  OpdsEntry entry;
  ASSERT_TRUE(parseSingleBookEntry(entry, "/books/example.epub"));
  ASSERT_TRUE(assertSingleFormat(entry, "epub", ".epub"));
}

TEST(OpdsParser, KepubDoubleExtension) {
  OpdsEntry entry;
  ASSERT_TRUE(parseSingleBookEntry(entry, "/books/example.kepub.epub"));
  ASSERT_TRUE(assertSingleFormat(entry, "kepub", ".kepub.epub"));
}

TEST(OpdsParser, BareKepubExtension) {
  OpdsEntry entry;
  ASSERT_TRUE(parseSingleBookEntry(entry, "/books/example.kepub"));
  ASSERT_TRUE(assertSingleFormat(entry, "kepub", ".kepub.epub"));
}

TEST(OpdsParser, SlashTerminatedKepubPath) {
  OpdsEntry entry;
  ASSERT_TRUE(parseSingleBookEntry(entry, "/opds/download/6516/kepub/"));
  ASSERT_TRUE(assertSingleFormat(entry, "kepub", ".kepub.epub"));
}

TEST(OpdsParser, SlashTerminatedEpubPath) {
  OpdsEntry entry;
  ASSERT_TRUE(parseSingleBookEntry(entry, "/opds/download/6516/epub/"));
  ASSERT_TRUE(assertSingleFormat(entry, "epub", ".epub"));
}

TEST(OpdsParser, DistinctAcquisitionFormatsRemainSeparate) {
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-2</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub"/>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.kepub.epub"/>
    <link rel="http://opds-spec.org/acquisition" type="application/vnd.xteink.xtc" href="/books/example.xtc"/>
  </entry>
</feed>)";

  std::vector<OpdsEntry> entries;
  OpdsParser parser;
  parser.onEntryParsed = [&](OpdsEntry e) { entries.push_back(std::move(e)); };
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  ASSERT_EQ(entries.size(), static_cast<size_t>(1));
  const auto& links = entries.front().acquisitionLinks;
  ASSERT_EQ(links.size(), static_cast<size_t>(3));
  ASSERT_EQ(links[0].formatKey, "epub");
  ASSERT_EQ(links[1].formatKey, "kepub");
  ASSERT_EQ(links[2].formatKey, "xtc");
}

TEST(OpdsParser, UnsupportedMimeTypeEntriesAreSurfaced) {
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-3</id>
    <link rel="http://opds-spec.org/acquisition" type="application/x-mobipocket-ebook" href="/books/example.mobi"/>
  </entry>
</feed>)";

  std::vector<OpdsEntry> entries;
  OpdsParser parser;
  parser.onEntryParsed = [&](OpdsEntry e) { entries.push_back(std::move(e)); };
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  ASSERT_EQ(entries.size(), static_cast<size_t>(1));
  // The entry is kept as a BOOK with no supported acquisition links — the UI's
  // signal that the catalog only offers formats this reader cannot open.
  EXPECT_EQ(entries.front().type, OpdsEntryType::BOOK);
  EXPECT_EQ(entries.front().href, "/books/example.mobi");
  EXPECT_TRUE(entries.front().acquisitionLinks.empty());
}

TEST(OpdsParser, MixedSupportedAndUnsupportedFormatsKeepSupported) {
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Unsupported First</title>
    <author><name>Example Author</name></author>
    <id>book-mixed-1</id>
    <link rel="http://opds-spec.org/acquisition" type="application/pdf" href="/books/mixed-1.pdf"/>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/mixed-1.epub"/>
  </entry>
  <entry>
    <title>Supported First</title>
    <author><name>Example Author</name></author>
    <id>book-mixed-2</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/mixed-2.epub"/>
    <link rel="http://opds-spec.org/acquisition" type="application/pdf" href="/books/mixed-2.pdf"/>
  </entry>
</feed>)";

  std::vector<OpdsEntry> entries;
  OpdsParser parser;
  parser.onEntryParsed = [&](OpdsEntry e) { entries.push_back(std::move(e)); };
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  ASSERT_EQ(entries.size(), static_cast<size_t>(2));
  // The unsupported link contributes nothing; the entry points at the
  // supported download in either link order.
  EXPECT_EQ(entries[0].href, "/books/mixed-1.epub");
  ASSERT_EQ(entries[0].acquisitionLinks.size(), static_cast<size_t>(1));
  EXPECT_EQ(entries[0].acquisitionLinks[0].formatKey, "epub");
  EXPECT_EQ(entries[1].href, "/books/mixed-2.epub");
  ASSERT_EQ(entries[1].acquisitionLinks.size(), static_cast<size_t>(1));
  EXPECT_EQ(entries[1].acquisitionLinks[0].formatKey, "epub");
}

TEST(OpdsParser, CoverImageLinkWithoutTypeIsAccepted) {
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-cover-1</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub"/>
    <link rel="http://opds-spec.org/image" href="/covers/1.jpg"/>
  </entry>
  <entry>
    <title>Typed Cover</title>
    <author><name>Example Author</name></author>
    <id>book-cover-2</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example2.epub"/>
    <link rel="http://opds-spec.org/image" type="image/png" href="/covers/2.png"/>
  </entry>
</feed>)";

  std::vector<OpdsEntry> entries;
  OpdsParser parser;
  parser.onEntryParsed = [&](OpdsEntry e) { entries.push_back(std::move(e)); };
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  ASSERT_EQ(entries.size(), static_cast<size_t>(2));
  EXPECT_EQ(entries[0].imageHref, "/covers/1.jpg");
  EXPECT_EQ(entries[1].imageHref, "/covers/2.png");
}

TEST(OpdsParser, CoverLinkRejectsThumbnailAndNonImageType) {
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Thumbnail Only</title>
    <author><name>Example Author</name></author>
    <id>book-cover-3</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub"/>
    <link rel="http://opds-spec.org/image/thumbnail" href="/covers/thumb.jpg"/>
  </entry>
  <entry>
    <title>Non-Image Cover</title>
    <author><name>Example Author</name></author>
    <id>book-cover-4</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example2.epub"/>
    <link rel="http://opds-spec.org/image" type="text/html" href="/covers/page.html"/>
  </entry>
</feed>)";

  std::vector<OpdsEntry> entries;
  OpdsParser parser;
  parser.onEntryParsed = [&](OpdsEntry e) { entries.push_back(std::move(e)); };
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  ASSERT_EQ(entries.size(), static_cast<size_t>(2));
  EXPECT_TRUE(entries[0].imageHref.empty());
  EXPECT_TRUE(entries[1].imageHref.empty());
}

TEST(OpdsParser, EmptyHrefOrType) {
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Empty Href</title>
    <author><name>Example Author</name></author>
    <id>book-4</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href=""/>
  </entry>
  <entry>
    <title>Empty Type</title>
    <author><name>Example Author</name></author>
    <id>book-5</id>
    <link rel="http://opds-spec.org/acquisition" type="" href="/books/example.epub"/>
  </entry>
  <entry>
    <title>Missing Href</title>
    <author><name>Example Author</name></author>
    <id>book-6</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip"/>
  </entry>
  <entry>
    <title>Missing Type</title>
    <author><name>Example Author</name></author>
    <id>book-7</id>
    <link rel="http://opds-spec.org/acquisition" href="/books/example.epub"/>
  </entry>
</feed>)";

  std::vector<OpdsEntry> entries;
  OpdsParser parser;
  parser.onEntryParsed = [&](OpdsEntry e) { entries.push_back(std::move(e)); };
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  // Empty/missing href gives nowhere to point, so those two entries drop. A
  // missing or empty type with a usable href is surfaced as an unsupported
  // BOOK instead of vanishing.
  ASSERT_EQ(entries.size(), static_cast<size_t>(2));
  EXPECT_EQ(entries[0].title, "Empty Type");
  EXPECT_EQ(entries[0].type, OpdsEntryType::BOOK);
  EXPECT_EQ(entries[0].href, "/books/example.epub");
  EXPECT_TRUE(entries[0].acquisitionLinks.empty());
  EXPECT_EQ(entries[1].title, "Missing Type");
  EXPECT_EQ(entries[1].type, OpdsEntryType::BOOK);
  EXPECT_EQ(entries[1].href, "/books/example.epub");
  EXPECT_TRUE(entries[1].acquisitionLinks.empty());
}

TEST(OpdsParser, DuplicateAcquisitionLinks) {
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-8</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub"/>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example-copy.epub"/>
  </entry>
</feed>)";

  std::vector<OpdsEntry> entries;
  OpdsParser parser;
  parser.onEntryParsed = [&](OpdsEntry e) { entries.push_back(std::move(e)); };
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  ASSERT_EQ(entries.size(), static_cast<size_t>(1));
  const auto& links = entries.front().acquisitionLinks;
  ASSERT_EQ(links.size(), static_cast<size_t>(2));
  ASSERT_EQ(links[0].formatKey, "epub");
  ASSERT_EQ(links[0].href, "/books/example.epub");
  ASSERT_EQ(links[1].formatKey, "epub");
  ASSERT_EQ(links[1].href, "/books/example-copy.epub");
}

TEST(OpdsParser, IdenticalHrefAcquisitionLinksAreDeduplicated) {
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-9</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub"/>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub"/>
  </entry>
</feed>)";

  std::vector<OpdsEntry> entries;
  OpdsParser parser;
  parser.onEntryParsed = [&](OpdsEntry e) { entries.push_back(std::move(e)); };
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  ASSERT_EQ(entries.size(), static_cast<size_t>(1));
  const auto& links = entries.front().acquisitionLinks;
  ASSERT_EQ(links.size(), static_cast<size_t>(1));
  ASSERT_EQ(links[0].formatKey, "epub");
  ASSERT_EQ(links[0].href, "/books/example.epub");
}

TEST(OpdsParser, SlashVariantHrefAcquisitionLinksAreDeduplicated) {
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-10</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub"/>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub/"/>
  </entry>
</feed>)";

  std::vector<OpdsEntry> entries;
  OpdsParser parser;
  parser.onEntryParsed = [&](OpdsEntry e) { entries.push_back(std::move(e)); };
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  ASSERT_EQ(entries.size(), static_cast<size_t>(1));
  const auto& links = entries.front().acquisitionLinks;
  ASSERT_EQ(links.size(), static_cast<size_t>(1));
  ASSERT_EQ(links[0].formatKey, "epub");
  ASSERT_EQ(links[0].href, "/books/example.epub");
}

TEST(OpdsParser, FeedPaginationLinksNextAndPrevious) {
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <link rel="previous" href="/opds/books?page=1" type="application/atom+xml;profile=opds-catalog"/>
  <link rel="next" href="/opds/books?page=3" type="application/atom+xml;profile=opds-catalog"/>
  <entry>
    <title>Page 2 Book</title>
    <author><name>Author</name></author>
    <id>book-page-2</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/p2.epub"/>
  </entry>
</feed>)";

  std::vector<OpdsEntry> entries;
  OpdsParser parser;
  parser.onEntryParsed = [&](OpdsEntry e) { entries.push_back(std::move(e)); };
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  ASSERT_EQ(entries.size(), static_cast<size_t>(1));
  EXPECT_EQ(parser.getPrevPageUrl(), "/opds/books?page=1");
  EXPECT_EQ(parser.getNextPageUrl(), "/opds/books?page=3");
}

TEST(OpdsParser, FeedPaginationLinksPrevVariant) {
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <link rel="prev" href="/opds/catalog?p=2" type="application/atom+xml;profile=opds-catalog"/>
  <link rel="next" href="/opds/catalog?p=4" type="application/atom+xml;profile=opds-catalog"/>
</feed>)";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  EXPECT_EQ(parser.getPrevPageUrl(), "/opds/catalog?p=2");
  EXPECT_EQ(parser.getNextPageUrl(), "/opds/catalog?p=4");
}

