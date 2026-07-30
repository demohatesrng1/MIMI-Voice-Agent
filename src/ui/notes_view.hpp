#pragma once

#include "brain/notes.hpp"

#include <QWidget>

class QLabel;
class QListWidget;
class QTextEdit;

namespace mimi::ui {

// Mimi's own notes, as a place you can read and edit them.
//
// The list is the index, the pane on the right is the note. Editing writes
// straight back to the same Markdown file the voice path appends to, so a note
// dictated aloud and a note typed here are the same object.
class NotesView : public QWidget {
    Q_OBJECT

public:
    explicit NotesView(QWidget* parent = nullptr);

    // Re-reads from disk. Called when the view is shown and after Mimi takes a
    // note by voice, so a dictated note appears without a restart.
    void refresh();

Q_SIGNALS:
    void noteCountChanged(int count);

private:
    void showSelected();
    void saveCurrent();
    void addNote();
    void deleteSelected();

    brain::Notes notes_;
    QListWidget* list_ = nullptr;
    QTextEdit* body_ = nullptr;
    QLabel* stamp_ = nullptr;
    QLabel* empty_ = nullptr;
    std::string current_;  // id of the note in the editor
    bool loading_ = false;  // suppresses the save that setPlainText would cause
};

}  // namespace mimi::ui
