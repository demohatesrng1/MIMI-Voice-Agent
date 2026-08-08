#include "ui/workspace_dock.hpp"

#include "ui/controls.hpp"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QVBoxLayout>

#include <array>
#include <initializer_list>

namespace mimi::ui {
namespace {

// A tool: the label the user reads, and the utterance it stands for. Sent to
// the router in Japanese, because that is the language she runs in.
struct Tool {
    const char* label;
    const char* utterance;
};

struct ToolSet {
    const char* tag;
    std::array<Tool, 4> tools;
};

// One set per context. The utterances are illustrative of the range each
// context asks for; the point of the dock is that the *set* changes under you.
const ToolSet kGeneral{
    "SUGGESTED",
    // Things only she can do. Asking the time or the battery is a worse version
    // of glancing at the menu bar, so suggesting it wastes the one row that
    // teaches people what she is for.
    {{{"Take a note", "メモして"},
      {"What's in my notes?", "メモを要約して"},
      {"Remind me in 20 minutes", "20分後に教えて"},
      {"What's on screen?", "画面に何がある"}}}};

const ToolSet kCoding{
    "CODING",
    {{{"Explain this error", "このエラーメッセージの意味を教えて"},
      {"Regex for email", "メールアドレスの正規表現を教えて"},
      {"Undo last commit", "直前のGitコミットを取り消す方法を教えて"},
      {"Open Terminal", "ターミナルを開いて"}}}};

const ToolSet kWriting{
    "WRITING",
    {{{"Make it natural", "この文章をもっと自然にして"},
      {"Summarize this", "これを短く要約して"},
      {"More formal", "もっと丁寧な言い方にして"},
      {"Fix the grammar", "この文の文法を直して"}}}};

const ToolSet kMeeting{
    "MEETING",
    {{{"Summarize meeting", "会議の内容を要約して"},
      {"Action items", "アクションアイテムを挙げて"},
      {"Take a note", "メモを取って"},
      {"What time is it?", "今何時ですか"}}}};

const ToolSet& setFor(WorkspaceDock::Context context) {
    switch (context) {
        case WorkspaceDock::Context::Coding:  return kCoding;
        case WorkspaceDock::Context::Writing: return kWriting;
        case WorkspaceDock::Context::Meeting: return kMeeting;
        case WorkspaceDock::Context::General: break;
    }
    return kGeneral;
}

bool mentions(const QString& text, std::initializer_list<const char*> words) {
    for (const char* word : words)
        if (text.contains(QString::fromUtf8(word), Qt::CaseInsensitive)) return true;
    return false;
}

}  // namespace

WorkspaceDock::WorkspaceDock(QWidget* parent) : QWidget(parent) {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);

    tag_ = new QLabel(QString::fromUtf8(kGeneral.tag));
    tag_->setObjectName(QStringLiteral("workspaceTag"));
    tag_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    tag_->setCursor(Qt::PointingHandCursor);
    tag_->setToolTip(QStringLiteral("Adapts to what you're doing — tap to switch"));
    tag_->installEventFilter(this);
    column->addWidget(tag_, 0, Qt::AlignLeft);

    tools_ = new QWidget;
    toolsRow_ = new QHBoxLayout(tools_);
    toolsRow_->setContentsMargins(0, 0, 0, 0);
    toolsRow_->setSpacing(8);
    column->addWidget(tools_);

    // The whole tool strip fades as a unit when the context changes.
    //
    // The effect is left disabled while it is not animating. A permanently
    // installed QGraphicsOpacityEffect re-routes the strip through an offscreen
    // pixmap, and its child chips then fail to appear at all under
    // QWidget::grab() -- the row rendered as an empty gap below its heading.
    fade_ = new QGraphicsOpacityEffect(tools_);
    fade_->setOpacity(1.0);
    fade_->setEnabled(false);
    tools_->setGraphicsEffect(fade_);

    anim_ = new QPropertyAnimation(fade_, "opacity", this);
    anim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim_, &QPropertyAnimation::finished, this, [this] {
        // Handed over at the trough of the fade: swap the set, then rise again.
        if (fade_->opacity() >= 0.999) fade_->setEnabled(false);
        if (fade_->opacity() < 0.5 && context_ != pending_) {
            context_ = pending_;
            rebuild(context_);
            Q_EMIT contextChanged(context_);
            anim_->setStartValue(0.0);
            anim_->setEndValue(1.0);
            anim_->setDuration(220);
            anim_->start();
        }
    });

    rebuild(Context::General);
}

void WorkspaceDock::rebuild(Context context) {
    tag_->setText(QString::fromUtf8(setFor(context).tag));

    QLayoutItem* item = nullptr;
    while ((item = toolsRow_->takeAt(0)) != nullptr) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    for (const Tool& tool : setFor(context).tools) {
        auto* chip = new Chip(QString::fromUtf8(tool.label));
        const QString utterance = QString::fromUtf8(tool.utterance);
        connect(chip, &Chip::clicked, this,
                [this, utterance] { Q_EMIT commandRequested(utterance); });
        toolsRow_->addWidget(chip);
    }
    toolsRow_->addStretch(1);
    fitChips();
}

// The dock lives under the caption column now, not across the whole window, so
// a context with four tools can want more width than it has. A QHBoxLayout
// answers that by compressing everything past its minimum, which draws chips
// overlapping each other; dropping the ones that do not fit is the honest
// version -- the command palette still has all of them.
void WorkspaceDock::fitChips() {
    const int available = width();
    if (available <= 0) return;
    constexpr int kSpacing = 8;
    int used = 0;
    bool full = false;
    for (int i = 0; i < toolsRow_->count(); ++i) {
        QWidget* chip = toolsRow_->itemAt(i)->widget();
        if (chip == nullptr) continue;   // the trailing stretch
        const int w = chip->sizeHint().width();
        if (full || used + w > available) {
            full = true;
            chip->hide();
            continue;
        }
        chip->show();
        used += w + kSpacing;
    }
}

void WorkspaceDock::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    fitChips();
}

void WorkspaceDock::crossfadeTo(Context context) {
    pending_ = context;
    fade_->setEnabled(true);  // only while it is actually fading
    anim_->stop();
    anim_->setStartValue(fade_->opacity());
    anim_->setEndValue(0.0);
    anim_->setDuration(140);
    anim_->start();
}

void WorkspaceDock::setContext(Context context) {
    if (context == context_ && context == pending_) return;
    crossfadeTo(context);
}

WorkspaceDock::Context WorkspaceDock::inferred(const QString& utterance) const {
    const QString t = utterance;
    if (mentions(t, {"code", "function", "bug", "error", "git", "python", "java",
                     "c++", "compile", "regex", "terminal", "command", "script",
                     "エラー", "コード", "関数", "コマンド", "プログラム"}))
        return Context::Coding;
    if (mentions(t, {"write", "essay", "email", "paragraph", "sentence", "rewrite",
                     "rephrase", "summar", "tone", "draft", "grammar", "文章",
                     "要約", "メール", "書い", "文法"}))
        return Context::Writing;
    if (mentions(t, {"meeting", "transcript", "agenda", "action item", "minutes",
                     "standup", "会議", "議事", "メモ", "打ち合わせ"}))
        return Context::Meeting;
    // Nothing decisive: leave the workspace where it is rather than snapping
    // back to General on every plain question.
    return context_;
}

bool WorkspaceDock::eventFilter(QObject* watched, QEvent* event) {
    if (watched == tag_ && event->type() == QEvent::MouseButtonRelease) {
        const Context next = static_cast<Context>(
            (static_cast<int>(context_) + 1) % 4);
        setContext(next);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace mimi::ui
