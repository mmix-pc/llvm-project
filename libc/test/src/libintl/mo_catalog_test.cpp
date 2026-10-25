//===-- GNU MO catalog validation tests -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/libintl/mo_catalog.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::cpp::string_view;
using LIBC_NAMESPACE::internal::MoCatalog;
using LIBC_NAMESPACE::internal::MoCatalogView;

namespace {
constexpr char HEADER[] = "Content-Type: text/plain; charset=ISO-8859-1\n"
                          "Plural-Forms: nplurals=2; plural=(n != 1);\n";

// Independently assembled revision-0 container: deliberately unaligned origin,
// tables and payloads, with no native integer stores or external producer.
struct Fixture {
  static constexpr size_t COUNT = 5;
  static constexpr size_t ORIGINALS = 29;
  static constexpr size_t TRANSLATIONS = ORIGINALS + COUNT * 8;
  static constexpr size_t HASH = TRANSLATIONS + COUNT * 8;
  uint8_t allocation[1025]{};
  uint8_t *bytes = allocation + 1;
  size_t size = HASH + 7 * 4;
  uint32_t original_offsets[COUNT]{}, translation_offsets[COUNT]{};
  bool big;

  void put(size_t offset, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      bytes[offset + (big ? 3 - i : i)] = uint8_t(value >> (8 * i));
  }
  uint32_t append(string_view text) {
    uint32_t offset = size;
    for (char c : text)
      bytes[size++] = static_cast<uint8_t>(c);
    bytes[size++] = 0;
    return offset;
  }
  void descriptor(size_t table, size_t index, uint32_t length, uint32_t offset) {
    put(table + 8 * index, length);
    put(table + 8 * index + 4, offset);
  }
  template <size_t N, size_t M>
  void message(size_t index, const char (&key)[N], const char (&value)[M]) {
    original_offsets[index] = append({key, N - 1});
    translation_offsets[index] = append({value, M - 1});
    descriptor(ORIGINALS, index, N - 1, original_offsets[index]);
    descriptor(TRANSLATIONS, index, M - 1, translation_offsets[index]);
  }
  explicit Fixture(bool big = false,
                   string_view header = {HEADER, sizeof(HEADER) - 1})
      : big(big) {
    put(0, 0x950412de);
    put(8, COUNT);
    put(12, ORIGINALS);
    put(16, TRANSLATIONS);
    put(20, 7);
    put(24, HASH);
    // Lookup deliberately ignores the accelerator's placement/probe chains.
    for (size_t i = 0; i != COUNT; ++i)
      put(HASH + i * 4, i + 1);
    original_offsets[0] = append("");
    translation_offsets[0] = append(header);
    descriptor(ORIGINALS, 0, 0, original_offsets[0]);
    descriptor(TRANSLATIONS, 0, header.size(), translation_offsets[0]);
    message(1, "apple\0apples", "pomme\0pommes");
    message(2, "menu\4Open", "Ouvrir");
    message(3, "zebra", "");
    message(4, "\x80", "raw\xff");
  }
  auto parse() { return MoCatalogView::parse({bytes, size}); }
  auto copy() { return MoCatalog::copy({bytes, size}); }
};
} // namespace

TEST(LlvmLibcMoCatalogTest, BothByteOrdersPluralContextAndMetadata) {
  for (unsigned order = 0; order != 2; ++order) {
    Fixture f(order);
    auto view = f.parse();
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->size(), Fixture::COUNT);
    auto plural = view->find("apple");
    ASSERT_TRUE(plural.has_value());
    EXPECT_TRUE(plural->original == string_view("apple\0apples", 12));
    EXPECT_TRUE(plural->translation == string_view("pomme\0pommes", 12));
    auto context = view->find("menu\4Open");
    ASSERT_TRUE(context.has_value());
    EXPECT_TRUE(context->translation == "Ouvrir");
    auto empty = view->find("zebra");
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->translation.empty());
    auto raw = view->find("\x80");
    ASSERT_TRUE(raw.has_value());
    EXPECT_TRUE(raw->translation == string_view("raw\xff", 4));
    auto metadata = view->metadata();
    ASSERT_TRUE(metadata.has_value());
    EXPECT_TRUE(*metadata == HEADER);
    auto charset = view->charset();
    ASSERT_TRUE(charset.has_value());
    EXPECT_TRUE(*charset == "ISO-8859-1");
    auto forms = view->plural_forms();
    ASSERT_TRUE(forms.has_value());
    EXPECT_TRUE(*forms == "nplurals=2; plural=(n != 1);");
    EXPECT_EQ(view->find("Open").error(), ENOENT);
    EXPECT_EQ(view->find("apples").error(), ENOENT);
    EXPECT_EQ(view->find("absent").error(), ENOENT);
    EXPECT_EQ(view->find("zzzz").error(), ENOENT);
    EXPECT_EQ(view->find("\xff").error(), ENOENT);
    EXPECT_EQ(view->find({"apple\0apples", 12}).error(), EINVAL);
    EXPECT_EQ(view->find({nullptr, 1}).error(), EINVAL);
    EXPECT_EQ(view->at(Fixture::COUNT).error(), EINVAL);
    EXPECT_EQ(view->at(size_t(-1)).error(), EINVAL);
    EXPECT_EQ(view->header_field("missing").error(), ENOENT);
    EXPECT_EQ(view->header_field({}).error(), EINVAL);
    EXPECT_EQ(view->header_field("bad\nname").error(), EINVAL);
  }
}

TEST(LlvmLibcMoCatalogTest, OwnedCopyAndRetainedResults) {
  Fixture f(true);
  auto catalog = f.copy();
  ASSERT_TRUE(catalog.has_value());
  EXPECT_TRUE((*catalog)->retain());
  auto message = (*catalog)->view().find("apple");
  auto charset = (*catalog)->view().charset();
  for (size_t i = 0; i != f.size; ++i)
    f.bytes[i] = 0xff;
  (*catalog)->release();
  EXPECT_TRUE(message.has_value());
  EXPECT_TRUE(charset.has_value());
  if (message)
    EXPECT_TRUE(message->translation == string_view("pomme\0pommes", 12));
  if (charset)
    EXPECT_TRUE(*charset == "ISO-8859-1");
  EXPECT_TRUE((*catalog)->view().find("menu\4Open").has_value());
  (*catalog)->release();
}

TEST(LlvmLibcMoCatalogTest, TruncationIdentityRevisionAndSize) {
  for (unsigned order = 0; order != 2; ++order) {
    Fixture f(order);
    for (size_t length = 0; length < f.size; ++length)
      EXPECT_EQ(MoCatalogView::parse({f.bytes, length}).error(), EINVAL);
    f.put(0, 0x960408de); // gencat is not an MO container.
    EXPECT_EQ(f.parse().error(), EINVAL);
    f.put(0, 0x950412de);
    const uint32_t revisions[] = {1, 0x00010000, 0x00010001, 0xffffffff};
    for (uint32_t revision : revisions) {
      f.put(4, revision);
      EXPECT_EQ(f.parse().error(), ENOTSUP);
    }
  }
  EXPECT_EQ(MoCatalogView::parse({nullptr, size_t(0)}).error(), EINVAL);
  EXPECT_EQ(MoCatalogView::parse({nullptr, 28}).error(), EINVAL);
  uint8_t byte = 0;
  EXPECT_EQ(MoCatalogView::parse({&byte, MoCatalogView::MAX_SIZE + 1}).error(),
            EINVAL);
  EXPECT_EQ(MoCatalog::copy({&byte, MoCatalogView::MAX_SIZE + 1}).error(), EINVAL);
  EXPECT_EQ(MoCatalog::copy({nullptr, 28}).error(), EINVAL);
}

TEST(LlvmLibcMoCatalogTest, TablesOffsetsCountsAndHashReferences) {
  const size_t fields[] = {8, 12, 16, 20, 24};
  for (unsigned order = 0; order != 2; ++order) {
    for (size_t field : fields) {
      Fixture f(order);
      f.put(field, 0xffffffff);
      EXPECT_EQ(f.parse().error(), EINVAL);
    }
    const size_t offsets[] = {12, 16, 24};
    for (size_t field : offsets) {
      Fixture f(order);
      f.put(field, f.size - 1);
      EXPECT_EQ(f.parse().error(), EINVAL);
    }
    Fixture f(order);
    f.put(Fixture::HASH, Fixture::COUNT + 1);
    EXPECT_EQ(f.parse().error(), EINVAL);
    f.put(20, 0);
    f.put(24, 0xffffffff); // No hash table: its offset is unused.
    auto view = f.parse();
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->find("apple").has_value());
  }
}

TEST(LlvmLibcMoCatalogTest, DescriptorOverflowAndTerminatingNul) {
  const size_t tables[] = {Fixture::ORIGINALS, Fixture::TRANSLATIONS};
  for (unsigned order = 0; order != 2; ++order) {
    for (size_t table : tables) {
      for (size_t index = 0; index != Fixture::COUNT; ++index) {
        Fixture f(order);
        f.put(table + index * 8, 0xffffffff);
        EXPECT_EQ(f.parse().error(), EINVAL);
        Fixture g(order);
        g.put(table + index * 8 + 4, 0xffffffff);
        EXPECT_EQ(g.parse().error(), EINVAL);
        Fixture h(order);
        h.descriptor(table, index, 0, h.size);
        EXPECT_EQ(h.parse().error(), EINVAL);
      }
    }
    Fixture f(order);
    f.bytes[f.size - 1] = 'X';
    EXPECT_EQ(f.parse().error(), EINVAL);
    Fixture g(order);
    g.bytes[g.original_offsets[1] + 12] = 'X';
    EXPECT_EQ(g.parse().error(), EINVAL);
    // An earlier embedded NUL cannot replace the counted final terminator.
    Fixture h(order);
    h.bytes[h.translation_offsets[1] + 12] = 'X';
    EXPECT_EQ(h.parse().error(), EINVAL);
  }
}

TEST(LlvmLibcMoCatalogTest, SortedUniqueSingularKeys) {
  for (unsigned order = 0; order != 2; ++order) {
    Fixture f(order);
    for (size_t i = 0; i != 8; ++i) {
      uint8_t tmp = f.bytes[Fixture::ORIGINALS + 8 + i];
      f.bytes[Fixture::ORIGINALS + 8 + i] = f.bytes[Fixture::ORIGINALS + 16 + i];
      f.bytes[Fixture::ORIGINALS + 16 + i] = tmp;
    }
    EXPECT_EQ(f.parse().error(), EINVAL);
    Fixture g(order);
    g.descriptor(Fixture::ORIGINALS, 2, 12, g.original_offsets[1]);
    EXPECT_EQ(g.parse().error(), EINVAL);
    Fixture h(order);
    // Full records differ; their singular keys are nevertheless duplicates.
    uint32_t offset = h.append({"apple\0different", 15});
    h.descriptor(Fixture::ORIGINALS, 2, 15, offset);
    EXPECT_EQ(h.parse().error(), EINVAL);
    Fixture j(order);
    j.descriptor(Fixture::ORIGINALS, 2, 5, j.original_offsets[1]);
    EXPECT_EQ(j.parse().error(), EINVAL);
  }
}

TEST(LlvmLibcMoCatalogTest, EmptyContainerAndSharedPayloads) {
  for (unsigned order = 0; order != 2; ++order) {
    Fixture f(order);
    f.put(8, 0);
    f.put(12, 28);
    f.put(16, 28);
    f.put(20, 0);
    auto empty = MoCatalogView::parse({f.bytes, 28});
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(empty->size(), size_t(0));
    EXPECT_EQ(empty->find("").error(), ENOENT);
    EXPECT_EQ(empty->metadata().error(), ENOENT);
    EXPECT_EQ(empty->charset().error(), ENOENT);

    Fixture g(order);
    g.descriptor(Fixture::TRANSLATIONS, 2, 12, g.translation_offsets[1]);
    g.descriptor(Fixture::TRANSLATIONS, 3, 6, g.translation_offsets[1] + 6);
    // Translation and original may share the same bytes too.
    g.descriptor(Fixture::TRANSLATIONS, 4, 1, g.original_offsets[4]);
    auto shared = g.parse();
    ASSERT_TRUE(shared.has_value());
    auto suffix = shared->find("zebra");
    ASSERT_TRUE(suffix.has_value());
    EXPECT_TRUE(suffix->translation == "pommes");
    auto identical = shared->find("menu\4Open");
    ASSERT_TRUE(identical.has_value());
    EXPECT_TRUE(identical->translation == string_view("pomme\0pommes", 12));

    Fixture h(order);
    // Original suffix sharing still obeys singular-key ordering.
    h.descriptor(Fixture::ORIGINALS, 2, 10, h.original_offsets[1] + 2);
    auto originals = h.parse();
    ASSERT_TRUE(originals.has_value());
    EXPECT_TRUE(originals->find("ple").has_value());
  }
}

TEST(LlvmLibcMoCatalogTest, RawMetadataWithoutInventedSemantics) {
  Fixture f(false, "content-type: text/plain; note=\"x;charset=wrong\"; "
                   "CHARSET = \"UTF-8\"\r\n"
                   "plural-forms:\t nplurals=3; plural=unknown(n); \t");
  auto view = f.parse();
  ASSERT_TRUE(view.has_value());
  auto charset = view->charset();
  ASSERT_TRUE(charset.has_value());
  EXPECT_TRUE(*charset == "UTF-8");
  auto forms = view->plural_forms();
  ASSERT_TRUE(forms.has_value());
  EXPECT_TRUE(*forms == "nplurals=3; plural=unknown(n);");

  const char *missing[] = {"", "Content-Type: text/plain\n", "X-charset: UTF-8\n"};
  for (const char *header : missing) {
    Fixture g(false, header);
    auto parsed = g.parse();
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->charset().error(), ENOENT);
    EXPECT_EQ(parsed->plural_forms().error(), ENOENT);
  }
  const char *bad[] = {
      "Content-Type: text/plain; charset=\n",
      "Content-Type: text/plain; charset=\"\"\n",
      "Content-Type: text/plain; charset=\"UTF-8\n",
      "Content-Type: text/plain; charset=bad name\n",
      "Content-Type: text/plain; charset=ASCII; charset=UTF-8\n",
      "Content-Type: text/plain; charset=ASCII\nContent-Type: text/plain\n"};
  for (const char *header : bad) {
    Fixture g(false, header);
    auto parsed = g.parse();
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->charset().error(), EINVAL);
  }
  Fixture g(false, "Plural-Forms: first\nplural-forms: second\n");
  auto duplicate = g.parse();
  ASSERT_TRUE(duplicate.has_value());
  EXPECT_EQ(duplicate->plural_forms().error(), EINVAL);
  constexpr char EMBEDDED[] = "Content-Type: text/plain; charset=UTF-8\0hidden";
  Fixture h(false, {EMBEDDED, sizeof(EMBEDDED) - 1});
  auto embedded = h.parse();
  ASSERT_TRUE(embedded.has_value());
  EXPECT_TRUE(embedded->metadata().has_value());
  EXPECT_EQ(embedded->charset().error(), EINVAL);
}

#ifdef MO_CATALOG_ALLOCATION_TEST
namespace {
bool tracking = false;
unsigned calls = 0, fail_call = 0, live = 0;
void *allocations[8]{};
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++calls == fail_call)
    return nullptr;
  void *p = __real_malloc(size);
  if (tracking && p)
    for (void *&slot : allocations)
      if (!slot) {
        slot = p;
        ++live;
        break;
      }
  return p;
}
extern "C" void __wrap_free(void *p) {
  if (p)
    for (void *&slot : allocations)
      if (slot == p) {
        slot = nullptr;
        --live;
        break;
      }
  __real_free(p);
}

TEST(LlvmLibcMoCatalogTest, AllocationFailureAndValidationRollback) {
  for (unsigned failure = 1; failure != 3; ++failure) {
    Fixture f;
    calls = 0;
    fail_call = failure;
    tracking = true;
    auto catalog = f.copy();
    tracking = false;
    EXPECT_FALSE(catalog.has_value());
    if (!catalog)
      EXPECT_EQ(catalog.error(), ENOMEM);
    else
      (*catalog)->release();
    EXPECT_EQ(calls, failure);
    EXPECT_EQ(live, 0U);
  }
  Fixture f;
  calls = fail_call = 0;
  tracking = true;
  auto catalog = f.copy();
  tracking = false;
  ASSERT_TRUE(catalog.has_value());
  EXPECT_EQ(calls, 2U);
  EXPECT_EQ(live, 2U);
  EXPECT_TRUE((*catalog)->retain());
  (*catalog)->release();
  EXPECT_EQ(live, 2U);
  (*catalog)->release();
  EXPECT_EQ(live, 0U);

  f.bytes[f.size - 1] = 'X';
  calls = 0;
  tracking = true;
  catalog = f.copy();
  tracking = false;
  EXPECT_FALSE(catalog.has_value());
  if (!catalog)
    EXPECT_EQ(catalog.error(), EINVAL);
  else
    (*catalog)->release();
  EXPECT_EQ(calls, 1U);
  EXPECT_EQ(live, 0U);
  f.put(4, 1);
  tracking = true;
  catalog = f.copy();
  tracking = false;
  EXPECT_FALSE(catalog.has_value());
  if (!catalog)
    EXPECT_EQ(catalog.error(), ENOTSUP);
  else
    (*catalog)->release();
  EXPECT_EQ(live, 0U);
}
#endif
