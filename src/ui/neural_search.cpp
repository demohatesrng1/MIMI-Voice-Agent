#include "ui/neural_search.hpp"

#include "ui/theme.hpp"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace mimi::ui {
namespace {

// A searchable thing. keywords carry the meaning that lets a description find
// it; navPage is the surface it lives on (Page enum).
struct Doc {
    const char* title;
    const char* kind;
    const char* keywords;
    int navPage;
};

constexpr std::array<Doc, 8> kCorpus{{
    {"Hero mockup", "Image", "presentation slides deck design mockup john liked visual", 1},
    {"Q3 proposal (final)", "File", "proposal document pricing final version export report", 2},
    {"Client kickoff notes", "Note", "meeting kickoff scope notes acme requirements", 2},
    {"deploy.sh", "Code", "script deploy build shell command release", 1},
    {"Voice memo · 0:42", "Voice", "recording idea onboarding walk audio note", 1},
    {"Generated summary", "Report", "meeting summary transcript auto draft recap", 2},
    {"Positioning", "Note", "positioning brand strategy calm private message", 1},
    {"Client feedback", "Action", "feedback pricing changes requested acme review", 2},
}};

constexpr int kRoleNav = Qt::UserRole;

int score(const Doc& doc, const QStringList& tokens) {
    if (tokens.isEmpty()) return 1;  // no query: everything is a weak match
    const QString title = QString::fromUtf8(doc.title).toLower();
    const QString keys = QString::fromUtf8(doc.keywords);
    int s = 0;
    for (const QString& t : tokens) {
        if (t.size() < 2) continue;
        if (title.contains(t)) s += 3;
        else if (keys.contains(t)) s += 1;
    }
    return s;
}

}  // namespace

NeuralSearch::NeuralSearch(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    hide();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addSpacing(120);
    auto* rowHolder = new QWidget;
    auto* row = new QHBoxLayout(rowHolder);
    row->setContentsMargins(0, 0, 0, 0);
    row->addStretch(1);

    panel_ = new QWidget;
    panel_->setObjectName(QStringLiteral("palettePanel"));  // shares the palette styling
    panel_->setFixedWidth(560);
    auto* col = new QVBoxLayout(panel_);
    col->setContentsMargins(0, 0, 0, 8);
    col->setSpacing(0);

    field_ = new QLineEdit;
    field_->setObjectName(QStringLiteral("paletteField"));
    field_->setPlaceholderText(QStringLiteral("Search by meaning…  e.g. “the deck John liked”"));
    field_->installEventFilter(this);
    col->addWidget(field_);

    list_ = new QListWidget;
    list_->setObjectName(QStringLiteral("paletteList"));
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setMaximumHeight(380);
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        activateCurrent();
    });
    col->addWidget(list_);

    row->addWidget(panel_);
    row->addStretch(1);
    outer->addWidget(rowHolder);
    outer->addStretch(1);

    connect(field_, &QLineEdit::textChanged, this, &NeuralSearch::rank);
}

void NeuralSearch::open() {
    if (parentWidget() != nullptr) {
        resize(parentWidget()->size());
        move(0, 0);
    }
    field_->clear();
    rank(QString());
    show();
    raise();
    field_->setFocus();
}

void NeuralSearch::dismiss() { hide(); }

void NeuralSearch::rank(const QString& query) {
    const QStringList tokens = query.toLower().split(' ', Qt::SkipEmptyParts);

    // Score, then present strongest first.
    std::array<int, kCorpus.size()> order{};
    for (int i = 0; i < static_cast<int>(kCorpus.size()); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return score(kCorpus[a], tokens) > score(kCorpus[b], tokens);
    });

    list_->clear();
    for (int i : order) {
        if (score(kCorpus[i], tokens) <= 0) continue;
        const Doc& d = kCorpus[i];
        auto* item = new QListWidgetItem(
            QStringLiteral("%1      %2").arg(QString::fromUtf8(d.title),
                                             QString::fromUtf8(d.kind)));
        item->setData(kRoleNav, d.navPage);
        list_->addItem(item);
    }
    if (list_->count() > 0) list_->setCurrentRow(0);
}

void NeuralSearch::activateCurrent() {
    QListWidgetItem* item = list_->currentItem();
    if (item == nullptr) return;
    const int nav = item->data(kRoleNav).toInt();
    dismiss();
    Q_EMIT navigateChosen(nav);
}

bool NeuralSearch::eventFilter(QObject* watched, QEvent* event) {
    if (watched == field_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
            case Qt::Key_Down:
                list_->setCurrentRow(qMin(list_->currentRow() + 1, list_->count() - 1));
                return true;
            case Qt::Key_Up:
                list_->setCurrentRow(qMax(list_->currentRow() - 1, 0));
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                activateCurrent();
                return true;
            case Qt::Key_Escape:
                dismiss();
                return true;
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NeuralSearch::mousePressEvent(QMouseEvent* event) {
    const QPoint inPanel = panel_->mapFrom(this, event->pos());
    if (!panel_->rect().contains(inPanel)) {
        dismiss();
        return;
    }
    QWidget::mousePressEvent(event);
}

void NeuralSearch::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    QColor scrim = theme::kVoid;
    scrim.setAlpha(150);
    painter.fillRect(rect(), scrim);
}

}  // namespace mimi::ui
