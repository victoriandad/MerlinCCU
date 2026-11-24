// HA certificate pinning (#17): reading and printing SHA-256 fingerprints.

#include "cert_pin.h"

#include <cctype>
#include <cstring>
#include <string>

#include "test_framework.h"

namespace
{

constexpr char kColons[] = "AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89:"
                           "AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89";

} // namespace

HOST_TEST(cert_pin_reads_the_forms_browsers_and_openssl_print)
{
    const std::optional<cert_pin::Fingerprint> colons = cert_pin::parse(kColons);
    EXPECT_TRUE(colons.has_value());
    EXPECT_EQ((*colons)[0], uint8_t{0xABU});
    EXPECT_EQ((*colons)[31], uint8_t{0x89U});

    // openssl's prefix, lower case, spaces: all the same fingerprint.
    EXPECT_TRUE(cert_pin::parse(std::string("sha256 Fingerprint=") + kColons) == colons);
    std::string lower = kColons;
    for (char& c : lower)
    {
        c = (c == ':') ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    EXPECT_TRUE(cert_pin::parse(lower) == colons);
}

HOST_TEST(cert_pin_refuses_anything_but_32_bytes_of_hex)
{
    EXPECT_FALSE(cert_pin::parse("").has_value());
    EXPECT_FALSE(cert_pin::parse("AB:CD").has_value());
    EXPECT_FALSE(cert_pin::parse(std::string(kColons) + ":00").has_value());
    std::string bad = kColons;
    bad[0] = 'G';
    EXPECT_FALSE(cert_pin::parse(bad).has_value());
}

HOST_TEST(cert_pin_prints_what_it_reads)
{
    char text[cert_pin::kFormattedLength + 1U] = {};
    cert_pin::format(*cert_pin::parse(kColons), text, sizeof(text));
    EXPECT_TRUE(std::strcmp(text, kColons) == 0);
    EXPECT_EQ(std::strlen(text), cert_pin::kFormattedLength);

    // Too small a buffer gets nothing rather than half a fingerprint.
    char small[10] = "x";
    cert_pin::format(*cert_pin::parse(kColons), small, sizeof(small));
    EXPECT_EQ(small[0], '\0');
}
