#pragma once

#include "brain/journal.hpp"

#include <QDate>
#include <QScrollArea>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;

namespace mimi::ui {

// One remembered thing: what kind of event it was, when, and a line about it.
struct Memory {
    enum class Kind { Meeting, Report, Chat, Note, Action, File };
    Kind kind;
    QString time;
    QString title;
    QString detail;
    QString day;  // the group it falls under -- "Today", "Yesterday"
};

// The scrolling body of the timeline: a spine threading every memory in order,
// painted as one piece so the connections between them are the structure rather
// than a decoration bolted on.
class TimelineStrip : public QWidget {
public:
    explicit TimelineStrip(QWidget* parent = nullptr);

    void setMemories(QVector<Memory> memories);
    void prepend(const Memory& memory);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent*) override;

private:
    int contentHeight() const;
    void relayout();

    QVector<Memory> memories_;
    QVector<int> centres_;  // y of each node, for hit-testing the hover
    int hover_ = -1;
};

// Memory, as a place instead of a scrollback.
//
// Chat history is a wall of turns you never re-read. This is the alternative:
// the things Mimi has done, threaded in time and connected -- a meeting became
// a report became a proposal became a final version. It also fills itself in
// live: every exchange this session lands here the moment it completes, which
// is exactly when a real assistant would write it to memory.
class TimelineView : public QScrollArea {
    Q_OBJECT

public:
    explicit TimelineView(QWidget* parent = nullptr);

    // Record an exchange as it happens. Lands at the top of Today.
    void remember(const QString& said, const QString& replied);
    // Re-reads the journal from disk. Called when the page is shown.
    void reload();

protected:
    void showEvent(QShowEvent* event) override;

private:
    QString dayLabel(const QString& day) const;
    static Memory::Kind kindFor(const QString& action);

    brain::Journal journal_;
    QLabel* empty_ = nullptr;

private:
    TimelineStrip* strip_ = nullptr;
};

}  // namespace mimi::ui
