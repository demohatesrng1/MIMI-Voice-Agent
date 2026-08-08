#include "brain/account.hpp"

#include "core/log.hpp"
#include "core/paths.hpp"

#include <nlohmann/json.hpp>

#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#include <Security/SecRandom.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

namespace mimi::brain {
namespace {

constexpr std::string_view kTag = "account";
using json = nlohmann::json;

// Cost of a single guess. High enough that a stolen file is not worth grinding
// through, low enough that signing in stays instant.
constexpr int kIterations = 210000;
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kKeyBytes = 32;

std::string to_hex(const unsigned char* bytes, std::size_t length) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        out.push_back(kDigits[bytes[i] >> 4]);
        out.push_back(kDigits[bytes[i] & 0x0F]);
    }
    return out;
}

std::vector<unsigned char> from_hex(const std::string& text) {
    std::vector<unsigned char> out;
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        out.push_back(static_cast<unsigned char>(
            std::strtoul(text.substr(i, 2).c_str(), nullptr, 16)));
    }
    return out;
}

std::string derive(const std::string& password, const std::vector<unsigned char>& salt) {
    std::array<unsigned char, kKeyBytes> key{};
    const int status = CCKeyDerivationPBKDF(
        kCCPBKDF2, password.data(), password.size(), salt.data(), salt.size(),
        kCCPRFHmacAlgSHA256, kIterations, key.data(), key.size());
    if (status != kCCSuccess) {
        log::warn(kTag, "could not derive a key");
        return {};
    }
    return to_hex(key.data(), key.size());
}

std::string trim(std::string text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Constant-time compare, so the time taken cannot narrow down the digest.
bool same_digest(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

json read(const std::string& path) {
    std::ifstream in(path);
    if (!in) return json::object();
    try {
        return json::parse(in);
    } catch (const std::exception&) {
        return json::object();
    }
}

}  // namespace

Accounts::Accounts() { path_ = paths::data_file("account.json").string(); }

bool Accounts::exists() const {
    const auto stored = read(path_);
    return !stored.value("username", std::string{}).empty();
}

Account Accounts::load() const {
    const auto stored = read(path_);
    Account account;
    account.username = stored.value("username", "");
    account.name = stored.value("name", "");
    account.preferred = stored.value("preferred", "");
    if (account.preferred.empty()) {
        account.preferred = account.name.empty() ? account.username : account.name;
    }
    return account;
}

bool Accounts::sign_up(const std::string& username, const std::string& password,
                       const std::string& name, const std::string& preferred) {
    if (exists()) return false;
    if (trim(username).empty() || password.empty()) return false;

    std::vector<unsigned char> salt(kSaltBytes);
    if (SecRandomCopyBytes(kSecRandomDefault, salt.size(), salt.data()) != errSecSuccess) {
        log::warn(kTag, "no secure randomness available");
        return false;
    }
    const std::string digest = derive(password, salt);
    if (digest.empty()) return false;

    const json stored{
        {"username", trim(username)},
        {"name", trim(name)},
        {"preferred", trim(preferred).empty() ? trim(name) : trim(preferred)},
        {"salt", to_hex(salt.data(), salt.size())},
        {"digest", digest},
        {"iterations", kIterations},
    };
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return false;
    out << stored.dump(2) << "\n";
    log::info(kTag, "account created");
    return true;
}

bool Accounts::verify(const std::string& username, const std::string& password) const {
    const auto stored = read(path_);
    const std::string known = stored.value("username", "");
    if (known.empty()) return false;  // no account is not a free pass

    // Case-insensitive, because a username you typed months ago is remembered
    // as a word, not as a capitalisation.
    if (lowercase(trim(username)) != lowercase(known)) return false;

    const auto salt = from_hex(stored.value("salt", ""));
    const std::string digest = stored.value("digest", "");
    if (salt.empty() || digest.empty()) return false;
    return same_digest(derive(password, salt), digest);
}

bool Accounts::set_password(const std::string& password) {
    auto stored = read(path_);
    if (stored.value("username", std::string{}).empty()) return false;
    if (password.empty()) return false;

    // A new salt as well as a new digest: reusing the old salt would leak that
    // the password changed to anyone who had seen the file before.
    std::vector<unsigned char> salt(kSaltBytes);
    if (SecRandomCopyBytes(kSecRandomDefault, salt.size(), salt.data()) != errSecSuccess) {
        return false;
    }
    const std::string digest = derive(password, salt);
    if (digest.empty()) return false;

    stored["salt"] = to_hex(salt.data(), salt.size());
    stored["digest"] = digest;
    stored["iterations"] = kIterations;
    // Accounts created before the switch carry an email. Drop it the first time
    // the file is rewritten -- nothing reads it any more, and leaving personal
    // data lying in a file because it is merely unused is how it ends up
    // somewhere it should not be.
    stored.erase("email");

    std::ofstream out(path_, std::ios::trunc);
    if (!out) return false;
    out << stored.dump(2) << "\n";
    log::info(kTag, "password changed");
    return true;
}

bool Accounts::set_preferred(const std::string& preferred) {
    auto stored = read(path_);
    if (stored.value("username", std::string{}).empty()) return false;
    stored["preferred"] = trim(preferred);
    stored.erase("email");
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return false;
    out << stored.dump(2) << "\n";
    return true;
}

bool Accounts::forget() {
    std::error_code ec;
    return std::filesystem::remove(path_, ec);
}

}  // namespace mimi::brain
