#include "ui/home_view.hpp"

#include "ui/controls.hpp"
#include "ui/live_thinking.hpp"
#include "ui/predictive.hpp"
#include "ui/smart_voice.hpp"
#include "ui/voice_orb.hpp"
#include "ui/workspace_dock.hpp"

#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>

namespace mimi::ui {

HomeView::HomeView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("home"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(64, 24, 64, 20);
    layout->setSpacing(0);

    layout->addStretch(3);

    orb_ = new VoiceOrb;
    orb_->setFixedSize(200, 200);
    layout->addWidget(orb_, 0, Qt::AlignHCenter);

    layout->addSpacing(20);

    stateLabel_ = new QLabel(QStringLiteral("STARTING"));
    stateLabel_->setObjectName(QStringLiteral("heroState"));
    stateLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(stateLabel_);

    layout->addSpacing(8);
    layout->addWidget(new SmartVoiceBar, 0, Qt::AlignHCenter);

    layout->addSpacing(20);

    // The exchange in progress. What you said sits above, quiet and small;
    // her answer below, large. The eye should land on the answer.
    saidLabel_ = new QLabel;
    saidLabel_->setObjectName(QStringLiteral("heroSaid"));
    saidLabel_->setAlignment(Qt::AlignCenter);
    saidLabel_->setWordWrap(true);
    saidLabel_->setVisible(false);
    layout->addWidget(saidLabel_);

    layout->addSpacing(10);

    replyLabel_ = new QLabel(QStringLiteral("Ask anything — or just start talking."));
    replyLabel_->setObjectName(QStringLiteral("heroReply"));
    replyLabel_->setAlignment(Qt::AlignCenter);
    replyLabel_->setWordWrap(true);
    replyLabel_->setMinimumHeight(76);
    layout->addWidget(replyLabel_);

    // Live thinking occupies the same slot as the answer: shown while she works
    // the pipeline, hidden the instant there is a reply to read.
    live_ = new LiveThinking;
    live_->setVisible(false);
    layout->addWidget(live_, 0, Qt::AlignHCenter);

    layout->addSpacing(14);

    // How sure she is of the answer, under it. Hidden until there is one.
    confidence_ = new ConfidenceMeter;
    confidence_->setVisible(false);
    layout->addWidget(confidence_, 0, Qt::AlignHCenter);

    layout->addSpacing(18);

    // Predictive actions: proactive next steps, shown only while she is idle
    // and observing. Same command path as everything else.
    predictive_ = new PredictiveActions;
    connect(predictive_, &PredictiveActions::commandRequested, this,
            &HomeView::commandRequested);
    layout->addWidget(predictive_);

    layout->addStretch(4);

    // The workspace dock, in place of a fixed suggestion row: the tools follow
    // what you are doing. Its commands run through the same path as everything.
    dock_ = new WorkspaceDock;
    connect(dock_, &WorkspaceDock::commandRequested, this, &HomeView::commandRequested);
    layout->addWidget(dock_);
}

void HomeView::setPresence(Presence presence) {
    orb_->setPresence(presence);
    stateLabel_->setText(presence_headline(presence));
    stateLabel_->setProperty("mode", static_cast<int>(presence));
    // Re-polish so the stylesheet can pick up the changed property.
    stateLabel_->style()->unpolish(stateLabel_);
    stateLabel_->style()->polish(stateLabel_);
    // Anticipation belongs to the quiet moments: offer next steps only when she
    // is idle and watching, never mid-exchange.
    predictive_->setVisible(presence == Presence::Observing);
}

void HomeView::setLevel(float rms) { orb_->setLevel(rms); }

void HomeView::setExchange(const QString& said, const QString& replied) {
    saidLabel_->setText(said.isEmpty() ? QString() : QStringLiteral("「%1」").arg(said));
    saidLabel_->setVisible(!said.isEmpty());
    if (!replied.isEmpty()) {
        live_->finish();
        live_->setVisible(false);
        replyLabel_->setVisible(true);
        replyLabel_->setText(replied);
    }
}

void HomeView::setThinking() {
    // Swap the answer slot for the live-thinking pipeline.
    replyLabel_->setVisible(false);
    predictive_->setVisible(false);
    live_->setVisible(true);
    live_->start();
    setConfidence(-1.0);  // last answer's certainty no longer applies
}

void HomeView::setConfidence(qreal value) {
    confidence_->setConfidence(value);
    confidence_->setVisible(value >= 0.0);
}

}  // namespace mimi::ui
