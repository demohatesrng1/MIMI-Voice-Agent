#pragma once

#include <string>

namespace mimi::brain {

// Who is using this copy of Mimi.
//
// The account is local. Nothing is registered anywhere, no server is contacted,
// and the file never leaves the machine -- it exists so she knows what to call
// you and so the window is not open to whoever walks past.
//
// The password is still never stored. It is run through PBKDF2-HMAC-SHA256 with
// a per-account random salt and only the digest is written, exactly as it would
// be for a hosted account. A local file is not a reason to keep a password in
// plain text: people reuse passwords, and a plain-text one here is a plain-text
// one for their email too.
struct Account {
    std::string email;
    std::string username;
    std::string name;       // their real name
    std::string preferred;  // what she should actually call them
    bool valid() const noexcept { return !email.empty(); }
};

class Accounts {
public:
    Accounts();

    // True once somebody has signed up on this machine.
    bool exists() const;

    // The stored account, without any password material.
    Account load() const;

    // Creates the account. False if one already exists, or a field is empty.
    bool sign_up(const std::string& email, const std::string& password,
                 const std::string& username, const std::string& name,
                 const std::string& preferred);

    // Checks a password against the stored digest. Always false when no account
    // exists, so a missing file cannot be treated as a successful login.
    bool verify(const std::string& email, const std::string& password) const;

    // Changes what she calls them, and re-records the spoken cue.
    bool set_preferred(const std::string& preferred);

    // Deletes the account and everything derived from it.
    bool forget();

private:
    std::string path_;
};

}  // namespace mimi::brain
