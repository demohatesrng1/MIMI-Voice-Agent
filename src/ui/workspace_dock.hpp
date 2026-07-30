#pragma once

#include <QString>
#include <QWidget>

class QGraphicsOpacityEffect;
class QHBoxLayout;
class QLabel;
class QPropertyAnimation;

namespace mimi::ui {

// The workspace, rearranging itself around what you are doing.
//
// A voice product that shows the same four suggestions forever teaches you its
// range once and then wastes the space. This dock instead reads the room --
// are you coding, writing, in a meeting -- and swaps in the tools that context
// wants, fading the old set out and the new one in so the change is noticed
// without being loud. Context is inferred from what you ask; the tag can also
// be tapped to move through the sets by hand.
class WorkspaceDock : public QWidget {
    Q_OBJECT

public:
    enum class Context { General, Coding, Writing, Meeting };

    explicit WorkspaceDock(QWidget* parent = nullptr);

    void setContext(Context context);
    Context context() const noexcept { return context_; }

    // Guess a context from an utterance. Returns the current context when
    // nothing about the request suggests a change, so a plain question does not
    // knock the workspace back to General.
    Context inferred(const QString& utterance) const;

Q_SIGNALS:
    // A tool was invoked; the text is fed through the router exactly as if
    // spoken, like the home suggestions.
    void commandRequested(QString utterance);
    void contextChanged(Context context);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild(Context context);
    void crossfadeTo(Context context);

    QLabel* tag_ = nullptr;
    QWidget* tools_ = nullptr;
    QHBoxLayout* toolsRow_ = nullptr;
    QGraphicsOpacityEffect* fade_ = nullptr;
    QPropertyAnimation* anim_ = nullptr;

    Context context_ = Context::General;
    Context pending_ = Context::General;
};

}  // namespace mimi::ui
