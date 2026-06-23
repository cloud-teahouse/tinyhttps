module;

#include <cstdio>

export module mcpplibs.tinyhttps:ca_bundle;

import std;

namespace mcpplibs::tinyhttps {

namespace {

auto read_file(const char* path) -> std::string {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return {};
    }
    std::string result;
    char buf[4096];
    while (auto n = std::fread(buf, 1, sizeof(buf), f)) {
        result.append(buf, n);
    }
    std::fclose(f);
    return result;
}

} // anonymous namespace

export auto load_ca_certs() -> std::string {
    // 1. SSL_CERT_FILE — the OpenSSL/curl convention and an explicit escape
    //    hatch. Relocatable distros (Termux, Nix, conda) set it because their
    //    bundle isn't under /etc.
    if (const char* env = std::getenv("SSL_CERT_FILE"); env && *env) {
        auto pem = read_file(env);
        if (!pem.empty()) {
            return pem;
        }
    }

    std::vector<std::string> ca_paths;

    // 2. Non-FHS prefixes (Termux et al. ship the bundle under $PREFIX). Probed
    //    before /etc so a Termux session — where /etc/ssl doesn't exist and TLS
    //    otherwise fails with "Connection failed" on every HTTPS fetch — works.
    if (const char* prefix = std::getenv("PREFIX"); prefix && *prefix) {
        ca_paths.emplace_back(std::string(prefix) + "/etc/tls/cert.pem");
        ca_paths.emplace_back(std::string(prefix) + "/etc/ssl/cert.pem");
    }
    // Default Termux prefix, in case PREFIX is unset (e.g. under su / cron).
    ca_paths.emplace_back("/data/data/com.termux/files/usr/etc/tls/cert.pem");

    // 3. Known system CA bundle locations.
    ca_paths.emplace_back("/etc/ssl/certs/ca-certificates.crt"); // Debian/Ubuntu
    ca_paths.emplace_back("/etc/pki/tls/certs/ca-bundle.crt");   // RHEL/CentOS
    ca_paths.emplace_back("/etc/ssl/cert.pem");                  // macOS / Alpine

    for (auto& path : ca_paths) {
        auto pem = read_file(path.c_str());
        if (!pem.empty()) {
            return pem;
        }
    }

    // No system certs found — return empty.
    // A production build could embed a Mozilla CA root bundle here.
    return {};
}

} // namespace mcpplibs::tinyhttps
