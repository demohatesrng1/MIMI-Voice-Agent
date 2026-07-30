#pragma once

#include <QWidget>

class QLineEdit;
class QListWidget;

namespace mimi::ui {

// Neural search: search that understands, summoned with ⌘F.
//
// You do not type a filename, you describe the thing -- "the presentation John
// liked" -- and it surfaces, because each item carries meaning, not just a
// name. This first pass ranks a seeded corpus by matched meaning-tokens; the
// surface and the interaction are the point, and real embeddings drop in behind
// the same ranker later.
class NeuralSearch : public QWidget {
    Q_OBJECT

public:
    explicit NeuralSearch(QWidget* parent = nullptr);

    void open();
    void dismiss();

Q_SIGNALS:
    void navigateChosen(int page);  // jump to where the result lives (Page enum)

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rank(const QString& query);
    void activateCurrent();

    QWidget* panel_ = nullptr;
    QLineEdit* field_ = nullptr;
    QListWidget* list_ = nullptr;
};

}  // namespace mimi::ui
