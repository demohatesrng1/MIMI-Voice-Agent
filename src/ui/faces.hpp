#pragma once

#include <QPixmap>
#include <QString>
#include <QVector>

namespace mimi::ui::faces {

// The faces Mimi can wear.
//
// The artwork is six portraits of the same character the VRM is -- same hair,
// same blazer, same bow tie -- each with a different expression. The 3D avatar
// is her body; this is her face wherever the app is too small to draw a body:
// the orb, the always-on-top puck, the picker.
//
// Deliberately a fixed catalogue rather than "choose any image file". Every one
// of these is cropped square with her head in the right place, and an arbitrary
// photograph dropped into a 64px circle is how a considered interface starts
// looking like a placeholder.
struct Face {
    QString id;       // stored in the account; stable across releases
    QString label;    // what the picker calls it
    QString resource; // qrc path
};

// All of them, in picker order.
const QVector<Face>& all();

// The id saved on the account, falling back to the first face when nothing has
// been chosen or the saved id no longer exists.
QString currentId();

// Remembers the choice on the account. Returns false if the id is unknown.
bool choose(const QString& id);

// A circular portrait at `diameter` device-independent pixels, cached. Circular
// because every surface that draws her masks it to a circle anyway, and doing
// it once here keeps the orb from masking on every frame.
QPixmap circular(const QString& id, int diameter);

// The current face, same treatment.
QPixmap current(int diameter);

// Square, unmasked, for the picker's own tiles.
QPixmap square(const QString& id, int size);

}  // namespace mimi::ui::faces
