#include "ui/account_view.hpp"

#include "ui/face_picker.hpp"
#include "ui/faces.hpp"

#include "brain/tools.hpp"
#include "ui/theme.hpp"

#include <QCloseEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace mimi::ui {
namespace {

// One column, centred, nothing else on screen. Every step shares it, so moving
// between questions does not move the furniture.
constexpr int kColumnWidth = 380;

// Sizes go through the stylesheet, not QFont: the application sheet sets a
// font-size on QWidget, and a stylesheet rule beats anything setFont() does --
// which is why every heading here first came out at body size.
QLabel* heading(const QString& text) {
    auto* label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: %1; font-size: 36px; font-weight: 700;"
                                       "letter-spacing: -0.4px;")
                             .arg(theme::kInk.name()));
    return label;
}

QLabel* subdued(const QString& text) {
    auto* label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: %1; font-size: 17px;")
                             .arg(theme::kDim.name()));
    return label;
}

// The face, masked to a circle. The asset is square with its own backdrop, and
// dropped in raw it reads as a photo in a box rather than an avatar.
QPixmap round_avatar(const QPixmap& source, int size) {
    const qreal dpr = 2.0;  // drawn for retina, so the edge stays crisp
    const int px = static_cast<int>(size * dpr);
    QPixmap out(px, px);
    out.fill(Qt::transparent);

    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath clip;
    clip.addEllipse(0, 0, px, px);
    painter.setClipPath(clip);

    // Whole, not cropped: KeepAspectRatioByExpanding filled the circle by
    // cutting the top of her head off.
    const QPixmap scaled =
        source.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter.drawPixmap((px - scaled.width()) / 2, (px - scaled.height()) / 2, scaled);

    // A hairline ring, so the circle reads as deliberate against the backdrop.
    painter.setClipping(false);
    QColor ring = theme::kAccent;
    ring.setAlphaF(0.55);
    painter.setPen(QPen(ring, 2.0 * dpr));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(dpr, dpr, px - 2 * dpr, px - 2 * dpr));

    painter.end();
    out.setDevicePixelRatio(dpr);
    return out;
}

QLineEdit* field(const QString& placeholder, bool secret = false) {
    auto* edit = new QLineEdit;
    edit->setPlaceholderText(placeholder);
    edit->setFixedHeight(42);
    edit->setAlignment(Qt::AlignCenter);
    if (secret) edit->setEchoMode(QLineEdit::Password);
    edit->setStyleSheet(
        QStringLiteral("QLineEdit { background: %1; border: 1px solid rgba(255,255,255,0.07);"
                       "  border-radius: 9px; color: %2; padding: 0 14px; font-size: 16px; }"
                       "QLineEdit:focus { border: 1px solid %3; }")
            .arg(theme::kLayer1.name(), theme::kInk.name(), theme::kAccentDeep.name()));
    return edit;
}

void style_primary(QPushButton* button) {
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(42);
    button->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; border: none; border-radius: 9px;"
                       "  color: #ffffff; font-size: 16px; font-weight: 600; }"
                       "QPushButton:hover { background: %2; }")
            .arg(theme::kAccentDeep.name(), theme::kAccent.name()));
}

void style_quiet(QPushButton* button) {
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(42);
    button->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; border: 1px solid rgba(255,255,255,0.07);"
                       "  border-radius: 9px; color: %2; font-size: 16px; font-weight: 500; }"
                       "QPushButton:hover { border: 1px solid %3; color: %4; }")
            .arg(theme::kLayer1.name(), theme::kDim.name(), theme::kAccentDeep.name(),
                 theme::kInk.name()));
}

// A way out of every screen. Choosing the wrong door on the first screen is the
// easiest mistake to make here, and without this the only way back was to quit
// the application.
QPushButton* back_button() {
    auto* button = new QPushButton(QStringLiteral("←  Back"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(32);
    button->setStyleSheet(
        QStringLiteral("QPushButton { background: transparent; border: none; color: %1;"
                       "  font-size: 16px; font-weight: 500; text-align: left;"
                       "  padding: 0 8px; }"
                       "QPushButton:hover { color: %2; }")
            .arg(theme::kFaint.name(), theme::kInk.name()));
    return button;
}

}  // namespace

AccountView::AccountView(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QStringLiteral("background: %1;").arg(theme::kLayer0.name()));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    screens_ = new QStackedWidget;
    outer->addWidget(screens_);

    buildWelcome();
    buildSignUp();
    buildSignIn();

    // Someone who has already signed up is asked to sign in, not to start over.
    screens_->setCurrentIndex(accounts_.exists() ? 2 : 0);
}

void AccountView::closeEvent(QCloseEvent* event) {
    // Closing the gate is a decision, not a hang: whoever is waiting on it has
    // to be told, or the process sits on an event loop nothing will ever quit.
    Q_EMIT abandoned();
    QWidget::closeEvent(event);
}

// --- welcome ----------------------------------------------------------------

void AccountView::buildWelcome() {
    auto* page = new QWidget;
    auto* centre = new QVBoxLayout(page);
    centre->addStretch(1);

    auto* column = new QVBoxLayout;
    column->setSpacing(0);
    column->setAlignment(Qt::AlignHCenter);

    auto* face = new QLabel;
    QPixmap art(QStringLiteral(":/mimi_face.png"));
    if (!art.isNull()) face->setPixmap(round_avatar(art, 116));
    face->setAlignment(Qt::AlignCenter);
    column->addWidget(face);
    column->addSpacing(26);

    column->addWidget(heading(QStringLiteral("Mimi")));
    column->addSpacing(10);
    column->addWidget(subdued(QStringLiteral(
        "A voice assistant that runs entirely on this Mac.")));
    column->addSpacing(34);

    auto* start = new QPushButton(QStringLiteral("Create an account"));
    style_primary(start);
    start->setFixedWidth(kColumnWidth);
    connect(start, &QPushButton::clicked, this, [this] {
        screens_->setCurrentIndex(1);
        showStep(StepUsername);
    });
    column->addWidget(start, 0, Qt::AlignHCenter);
    column->addSpacing(10);

    auto* existing = new QPushButton(QStringLiteral("I already have one"));
    style_quiet(existing);
    existing->setFixedWidth(kColumnWidth);
    connect(existing, &QPushButton::clicked, this,
            [this] { screens_->setCurrentIndex(2); });
    column->addWidget(existing, 0, Qt::AlignHCenter);
    column->addSpacing(30);

    // Specific, checkable claims rather than a reassuring adjective. Anyone
    // deciding whether this is allowed on a work machine is asking exactly
    // these three questions, and "we take privacy seriously" answers none of
    // them. Each line here is a fact about the build that can be verified.
    auto* assurances = new QVBoxLayout;
    assurances->setSpacing(7);
    for (const QString& line : {
             QStringLiteral("Speech and answers never leave this Mac"),
             QStringLiteral("No account server, telemetry or network calls"),
             QStringLiteral("Passwords hashed with PBKDF2-SHA256"),
         }) {
        auto* row = new QHBoxLayout;
        row->setSpacing(9);
        auto* tick = new QLabel(QStringLiteral("✓"));
        tick->setStyleSheet(
            QStringLiteral("color: %1; font-size: 15px;").arg(theme::kAccent.name()));
        row->addWidget(tick);
        auto* text = new QLabel(line);
        text->setStyleSheet(
            QStringLiteral("color: %1; font-size: 15px;").arg(theme::kFaint.name()));
        row->addWidget(text);
        row->addStretch(1);
        auto* holder = new QWidget;
        holder->setFixedWidth(kColumnWidth + 20);
        holder->setLayout(row);
        assurances->addWidget(holder, 0, Qt::AlignHCenter);
    }
    column->addLayout(assurances);

    centre->addLayout(column);
    centre->addStretch(1);
    screens_->addWidget(page);
}

// --- sign up ----------------------------------------------------------------

void AccountView::buildSignUp() {
    auto* page = new QWidget;
    auto* centre = new QVBoxLayout(page);
    centre->addStretch(1);

    auto* column = new QVBoxLayout;
    column->setSpacing(0);
    column->setAlignment(Qt::AlignHCenter);

    auto* backRowSignUp = new QHBoxLayout;
    back_ = back_button();
    connect(back_, &QPushButton::clicked, this, &AccountView::goBack);
    backRowSignUp->addWidget(back_);
    backRowSignUp->addStretch(1);
    auto* backHolder = new QWidget;
    backHolder->setFixedWidth(kColumnWidth);
    backHolder->setLayout(backRowSignUp);
    column->addWidget(backHolder, 0, Qt::AlignHCenter);
    column->addSpacing(18);

    progress_ = subdued(QString());
    progress_->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px; font-weight: 600; letter-spacing: 1.4px;")
            .arg(theme::kFaint.name()));
    column->addWidget(progress_);
    column->addSpacing(14);

    question_ = heading(QString());
    // The heading paints at 36px (its inline stylesheet) but the application
    // stylesheet's `QWidget { font-size: 19px }` makes the label size itself
    // from the 19px base font. So a question that wraps to two lines at 36px --
    // "What should she call you?" -- is laid out only one line tall and paints
    // straight over the step counter above it and the hint below, which is why
    // the question was unreadable. Reserve two rendered lines up front. It also
    // stops the column jumping as the steps change between one- and two-liners.
    {
        QFont rendered = question_->font();
        rendered.setPixelSize(36);
        question_->setMinimumHeight(QFontMetrics(rendered).lineSpacing() * 2 + 8);
    }
    column->addWidget(question_);
    column->addSpacing(8);

    hint_ = subdued(QString());
    column->addWidget(hint_);
    column->addSpacing(26);

    password_ = field(QStringLiteral("At least 8 characters"), true);
    username_ = field(QStringLiteral("username"));
    name_ = field(QStringLiteral("Your name"));

    steps_ = new QStackedWidget;
    steps_->setMinimumWidth(kColumnWidth);
    for (QLineEdit* edit : {username_, password_, name_}) {
        auto* holder = new QWidget;
        auto* row = new QVBoxLayout(holder);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(edit);
        steps_->addWidget(holder);
        connect(edit, &QLineEdit::returnPressed, this, &AccountView::advance);
    }

    // Choosing her face. A page of its own because it is the one question with
    // a visual answer, and seeing the six side by side is the whole decision.
    {
        auto* holder = new QWidget;
        auto* row = new QVBoxLayout(holder);
        row->setContentsMargins(0, 0, 0, 0);
        facePicker_ = new FacePicker;
        face_ = facePicker_->selected();
        connect(facePicker_, &FacePicker::chosen, this, [this](const QString& id) {
            // Held until the account exists; sign_up() writes it.
            face_ = id;
        });
        row->addWidget(facePicker_, 0, Qt::AlignHCenter);
        steps_->addWidget(holder);
    }

    // The last question is a choice, not a field: it can only be asked once the
    // name and username exist, and its two answers are those two words.
    {
        auto* holder = new QWidget;
        auto* row = new QHBoxLayout(holder);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(10);
        callName_ = new QPushButton;
        callUsername_ = new QPushButton;
        for (QPushButton* button : {callName_, callUsername_}) {
            style_quiet(button);
            row->addWidget(button);
        }
        connect(callName_, &QPushButton::clicked, this, [this] {
            preferred_ = name_->text().trimmed();
            finishSignUp();
        });
        connect(callUsername_, &QPushButton::clicked, this, [this] {
            preferred_ = username_->text().trimmed();
            finishSignUp();
        });
        steps_->addWidget(holder);
    }
    column->addWidget(steps_, 0, Qt::AlignHCenter);
    column->addSpacing(12);

    error_ = subdued(QString());
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(theme::kError.name()));
    column->addWidget(error_);
    column->addSpacing(6);

    next_ = new QPushButton(QStringLiteral("Continue"));
    style_primary(next_);
    next_->setFixedWidth(kColumnWidth);
    connect(next_, &QPushButton::clicked, this, &AccountView::advance);
    column->addWidget(next_, 0, Qt::AlignHCenter);

    centre->addLayout(column);
    centre->addStretch(1);
    screens_->addWidget(page);
}

void AccountView::goBack() {
    // Step by step on the way back, exactly as it went forward. Answers already
    // given are left in their fields, so returning to fix one does not discard
    // the rest.
    if (step_ == StepUsername) {
        screens_->setCurrentIndex(0);
        return;
    }
    showStep(step_ - 1);
}

void AccountView::showStep(int step) {
    step_ = step;
    error_->clear();
    steps_->setCurrentIndex(step);
    progress_->setText(QStringLiteral("STEP %1 OF 5").arg(step + 1));
    next_->setVisible(step != StepCallYou);

    switch (step) {
        case StepUsername:
            question_->setText(QStringLiteral("Pick a username"));
            hint_->setText(QStringLiteral("This is how you sign in. Short, and yours."));
            username_->setFocus();
            break;
        case StepPassword:
            question_->setText(QStringLiteral("Choose a password"));
            hint_->setText(QStringLiteral(
                "Stored only as a hash — never as the password itself."));
            password_->setFocus();
            break;
        case StepName:
            question_->setText(QStringLiteral("What's your name?"));
            hint_->setText(QStringLiteral("So she has something to call you."));
            name_->setFocus();
            break;
        case StepFace:
            question_->setText(QStringLiteral("Which one is her?"));
            hint_->setText(QStringLiteral("You can change this later in Settings."));
            break;
        case StepCallYou:
            question_->setText(QStringLiteral("What should she call you?"));
            hint_->setText(QStringLiteral("She says this out loud, so pick what "
                                          "sounds right."));
            callName_->setText(name_->text().trimmed());
            callUsername_->setText(username_->text().trimmed());
            // A name that is also the username makes the choice meaningless.
            callUsername_->setVisible(name_->text().trimmed().compare(
                                          username_->text().trimmed(),
                                          Qt::CaseInsensitive) != 0);
            break;
        default:
            break;
    }
}

void AccountView::fail(const QString& message) { error_->setText(message); }

void AccountView::advance() {
    error_->clear();
    switch (step_) {
        case StepUsername:
            if (username_->text().trimmed().isEmpty()) {
                fail(QStringLiteral("Pick something."));
                return;
            }
            break;
        case StepPassword:
            if (password_->text().size() < 8) {
                fail(QStringLiteral("Use at least 8 characters."));
                return;
            }
            break;
        case StepName:
            if (name_->text().trimmed().isEmpty()) {
                fail(QStringLiteral("She needs something to call you."));
                return;
            }
            break;
        default:
            break;
    }
    showStep(step_ + 1);
}

void AccountView::finishSignUp() {
    const bool created =
        accounts_.sign_up(username_->text().toStdString(), password_->text().toStdString(),
                          name_->text().toStdString(), preferred_.toStdString(),
                          face_.toStdString());
    if (!created) {
        fail(QStringLiteral("Could not create the account."));
        return;
    }
    // Record her greeting now, while the name is fresh, so the first reminder
    // already has a voice to announce it with.
    brain::tools::record_notification_cue(preferred_.toStdString());
    Q_EMIT authenticated(preferred_);
}

// --- sign in ----------------------------------------------------------------

void AccountView::buildSignIn() {
    auto* page = new QWidget;
    auto* centre = new QVBoxLayout(page);
    centre->addStretch(1);

    auto* column = new QVBoxLayout;
    column->setSpacing(0);
    column->setAlignment(Qt::AlignHCenter);

    auto* backRowSignIn = new QHBoxLayout;
    auto* backIn = back_button();
    connect(backIn, &QPushButton::clicked, this, [this] {
        signInError_->clear();
        signInPassword_->clear();
        screens_->setCurrentIndex(0);
    });
    backRowSignIn->addWidget(backIn);
    backRowSignIn->addStretch(1);
    auto* backHolderIn = new QWidget;
    backHolderIn->setFixedWidth(kColumnWidth);
    backHolderIn->setLayout(backRowSignIn);
    column->addWidget(backHolderIn, 0, Qt::AlignHCenter);
    column->addSpacing(18);

    column->addWidget(heading(QStringLiteral("Welcome back")));
    column->addSpacing(10);
    column->addWidget(subdued(QStringLiteral("Sign in to unlock Mimi on this Mac.")));
    column->addSpacing(30);

    signInUser_ = field(QStringLiteral("Username"));
    signInUser_->setFixedWidth(kColumnWidth);
    column->addWidget(signInUser_, 0, Qt::AlignHCenter);
    column->addSpacing(10);

    signInPassword_ = field(QStringLiteral("Password"), true);
    signInPassword_->setFixedWidth(kColumnWidth);
    column->addWidget(signInPassword_, 0, Qt::AlignHCenter);
    column->addSpacing(12);

    signInError_ = subdued(QString());
    signInError_->setStyleSheet(QStringLiteral("color: %1;").arg(theme::kError.name()));
    column->addWidget(signInError_);
    column->addSpacing(6);

    auto* go = new QPushButton(QStringLiteral("Sign in"));
    style_primary(go);
    go->setFixedWidth(kColumnWidth);
    column->addWidget(go, 0, Qt::AlignHCenter);

    connect(go, &QPushButton::clicked, this, &AccountView::attemptSignIn);
    connect(signInPassword_, &QLineEdit::returnPressed, this, &AccountView::attemptSignIn);
    connect(signInUser_, &QLineEdit::returnPressed, this, &AccountView::attemptSignIn);

    centre->addLayout(column);
    centre->addStretch(1);
    screens_->addWidget(page);

    // Prefill the username: it is not a secret, and the only thing worth
    // typing here is the password.
    signInUser_->setText(QString::fromStdString(accounts_.load().username));
}

void AccountView::attemptSignIn() {
    if (!accounts_.verify(signInUser_->text().toStdString(),
                          signInPassword_->text().toStdString())) {
        // One message for both causes: saying which half was wrong tells an
        // unwanted guest whether the username is the right one.
        signInError_->setText(QStringLiteral("That username and password don't match."));
        signInPassword_->clear();
        signInPassword_->setFocus();
        return;
    }
    const auto account = accounts_.load();
    // A cue can go missing -- a half-restored data directory, an upgrade. Cheap
    // to check, and a reminder with no voice is the failure it prevents.
    if (brain::tools::notification_cue_path().empty()) {
        brain::tools::record_notification_cue(account.preferred);
    }
    Q_EMIT authenticated(QString::fromStdString(account.preferred));
}

}  // namespace mimi::ui
