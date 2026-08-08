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
// one somewhere that matters.
//
// The identity is a **username**, not an email. Nothing here ever sends mail or
// contacts a server, so an email address was a field that could only do harm --
// it is the one piece of personal data in the app that identifies you off this
// machine, and it sat in a file one careless `git add -f` away from being
// public. A local account needs a name to greet you by and a secret. Not that.
struct Account {
    std::string username;
    std::string name;       // their real name
    std::string preferred;  // what she should actually call them
    std::string face;       // which portrait she wears; see ui::faces
    bool valid() const noexcept { return !username.empty(); }
};

class Accounts {
public:
    Accounts();

    // True once somebody has signed up on this machine.
    bool exists() const;

    // The stored account, without any password material.
    Account load() const;

    // Creates the account. False if one already exists, or a field is empty.
    bool sign_up(const std::string& username, const std::string& password,
                 const std::string& name, const std::string& preferred,
                 const std::string& face = {});

    // Checks a password against the stored digest. Always false when no account
    // exists, so a missing file cannot be treated as a successful login.
    // The username is compared case-insensitively; the password is not.
    bool verify(const std::string& username, const std::string& password) const;

    // Sets a new password on the existing account, with a fresh salt. For
    // mimi_account --password, so a forgotten password does not mean losing the
    // journal along with it.
    bool set_password(const std::string& password);

    // Changes what she calls them, and re-records the spoken cue.
    bool set_preferred(const std::string& preferred);

    // Which of the bundled portraits she wears. Empty is valid and means "the
    // default one", so this never has to be set for the app to work.
    bool set_face(const std::string& face);

    // Deletes the account and everything derived from it.
    bool forget();

private:
    std::string path_;
};

}  // namespace mimi::brain
