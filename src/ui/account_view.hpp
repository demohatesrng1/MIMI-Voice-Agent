#pragma once

#include "brain/account.hpp"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace mimi::ui {

// The first thing anyone sees: signing up, or signing back in.
//
// One field at a time rather than a form. A wall of six inputs on first launch
// reads as a registration desk; a single question with one answer reads as
// being asked something. It is also the only honest way to ask "what should I
// call you" -- that question needs the name already given to offer a choice.
//
// Nothing here reaches a network, and nothing asks for an email: the identity
// is a username, because an app that promises to keep everything on the machine
// has no business holding the one field that identifies you off it.
class FacePicker;

class AccountView : public QWidget {
    Q_OBJECT

public:
    explicit AccountView(QWidget* parent = nullptr);

Q_SIGNALS:
    // The user is in. Carries what she should call them.
    void authenticated(const QString& preferred);
    // The window was closed without signing in.
    void abandoned();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    // The sign-up questions, in order.
    enum Step { StepUsername = 0, StepPassword, StepName, StepFace, StepCallYou };

    void buildWelcome();
    void buildSignUp();
    void buildSignIn();

    QWidget* backRow(int destination);
    void goBack();
    void showStep(int step);
    void advance();          // validates the current answer, then moves on
    void finishSignUp();
    void attemptSignIn();
    void fail(const QString& message);

    brain::Accounts accounts_;
    QStackedWidget* screens_ = nullptr;

    // Sign-up
    QStackedWidget* steps_ = nullptr;
    QLabel* question_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* error_ = nullptr;
    QLineEdit* password_ = nullptr;
    QLineEdit* username_ = nullptr;
    QLineEdit* name_ = nullptr;
    QPushButton* callName_ = nullptr;
    QPushButton* callUsername_ = nullptr;
    QPushButton* next_ = nullptr;
    QLabel* progress_ = nullptr;
    int step_ = StepUsername;
    QPushButton* back_ = nullptr;
    QString preferred_;

    // Sign-in
    FacePicker* facePicker_ = nullptr;
    QString face_;
    QLineEdit* signInUser_ = nullptr;
    QLineEdit* signInPassword_ = nullptr;
    QLabel* signInError_ = nullptr;
};

}  // namespace mimi::ui
