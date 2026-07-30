#include "ui/neural_search.hpp"

#include "brain/journal.hpp"
#include "brain/notes.hpp"

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
    QString title;
    QString kind;
    QString keywords;  // the body or her reply -- what makes it findable
    int navPage;       // the surface it lives on (Page enum)
};

// Nothing is hardcoded here any more. This searched a fixed list of eight
// invented documents -- a Q3 proposal, a client called Acme, a deploy script --
// so the same eight results came back on every machine no matter what you had
// actually done. It now reads her notes and her journal.
std::vector<Doc> collect() {
    std::vector<Doc> docs;
    for (const auto& note : brain::Notes().all()) {
        docs.push_back({QString::fromStdString(note.title), QStringLiteral("Note"),
                        QString::fromStdString(note.body), 1});
    }
    brain::Journal journal;
    const auto days = journal.days();
    for (auto day = days.rbegin(); day != days.rend(); ++day) {
        for (const auto& event : journal.read_day(*day)) {
            const auto text = [&event](const char* key) {
                return event.record.contains(key) && event.record[key].is_string()
                           ? QString::fromStdString(event.record[key]).trimmed()
                           : QString();
            };
            const QString said = text("said");
            if (said.isEmpty()) continue;
            docs.push_back({said, QString::fromStdString(event.kind), text("replied"), 2});
        }
        if (docs.size() > 400) break;  // enough to search; the rest is history
    }
    return docs;
}

constexpr int kRoleNav = Qt::UserRole;

int score(const Doc& doc, const QStringList& tokens) {
    if (tokens.isEmpty()) return 1;  // no query: everything is a weak match
    const QString title = doc.title.toLower();
    const QString keys = doc.keywords;
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

    // Re-read each time it opens: a note taken by voice a moment ago has to be
    // findable without restarting the app.
    const std::vector<Doc> corpus = collect();

    std::vector<int> order(corpus.size());
    for (std::size_t i = 0; i < corpus.size(); ++i) order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return score(corpus[a], tokens) > score(corpus[b], tokens);
    });

    list_->clear();
    for (int i : order) {
        if (score(corpus[i], tokens) <= 0) continue;
        if (list_->count() >= 40) break;
        const Doc& d = corpus[i];
        auto* item = new QListWidgetItem(
            QStringLiteral("%1      %2").arg(d.title.left(70), d.kind));
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
