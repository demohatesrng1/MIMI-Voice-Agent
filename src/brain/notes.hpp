#pragma once

#include <string>
#include <vector>

namespace mimi::brain {

// Notes Mimi keeps for you, in her own app.
//
// Deliberately not "type into whatever text editor is frontmost". Driving
// another app's text field means synthetic keystrokes, which need Accessibility
// permission, land in whatever happens to have focus, and fail silently when
// the target app drops them. A note taken by voice should not depend on any of
// that. These are plain Markdown files under <data>/notes/, so they stay
// readable and greppable without Mimi, and they never leave the machine.
struct Note {
    std::string id;       // the filename stem, "2026-07-30-143015"
    std::string title;    // first line, or the opening words of the body
    std::string body;
    std::string created;  // ISO-8601, local
    bool valid() const noexcept { return !id.empty(); }
};

class Notes {
public:
    Notes();

    // Writes a new note and returns it. `title` may be empty, in which case it
    // is taken from the opening words of the body.
    Note add(const std::string& body, const std::string& title = {});

    // Appends to the most recent note, so "add to that" works after dictating
    // one. False when there is nothing to append to.
    bool append_to_latest(const std::string& text);

    // Newest first. `limit` of 0 means all of them.
    std::vector<Note> all(int limit = 0) const;
    Note latest() const;
    Note get(const std::string& id) const;

    // Case-insensitive substring match over title and body, newest first.
    std::vector<Note> search(const std::string& query, int limit = 20) const;

    // Notes that look like they bear on `question`, best first.
    //
    // Substring search cannot answer this: "会議は何時からだっけ" appears
    // verbatim in no note, yet the note saying 会議は午後3時から is exactly what
    // it is asking about. Scored on shared character sequences instead, which
    // works without a tokeniser -- Japanese has no spaces to split on, so word
    // matching would need a dictionary that this does not carry.
    std::vector<Note> relevant(const std::string& question, int limit = 6) const;

    // Rewrites a note's body in place, keeping its id and creation time. The
    // title is re-derived from the first line unless one is given.
    bool update(const std::string& id, const std::string& body,
                const std::string& title = {});

    bool remove(const std::string& id);

    // Where the files live, for the UI to show and for Finder to open.
    std::string directory() const;

private:
    std::string dir_;
};

}  // namespace mimi::brain
