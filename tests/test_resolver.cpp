// Verifies the platform DNS fallback resolver (used on Termux/Android where
// libc's getaddrinfo can't read $PREFIX/etc/resolv.conf). On a normal Linux box
// resolve_fallback() reads /etc/resolv.conf and performs a real UDP DNS query.
#include <gtest/gtest.h>

import mcpplibs.tinyhttps;
import std;

namespace plat = mcpplibs::tinyhttps::platform;

TEST(Resolver, NumericHostPassthrough) {
    auto ips = plat::resolve_fallback("1.2.3.4", 2000);
    ASSERT_EQ(ips.size(), 1u);
    EXPECT_EQ(ips[0], "1.2.3.4");
}

TEST(Resolver, ResolvesKnownHost) {
    // one.one.one.one is Cloudflare's stable hostname → 1.1.1.1 / 1.0.0.1.
    auto ips = plat::resolve_fallback("one.one.one.one", 5000);
    ASSERT_FALSE(ips.empty()) << "manual DNS query returned no A records";
    bool found = false;
    for (const auto& ip : ips) {
        if (ip == "1.1.1.1" || ip == "1.0.0.1") found = true;
    }
    EXPECT_TRUE(found) << "expected 1.1.1.1/1.0.0.1, got first: " << ips.front();
}

TEST(Resolver, SystemResolverConfiguredOnLinux) {
    // This CI/dev host is a normal Linux with /etc/resolv.conf present.
    EXPECT_TRUE(plat::system_resolver_configured());
}
