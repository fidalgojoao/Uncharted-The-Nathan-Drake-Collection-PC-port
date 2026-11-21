#include "navigator.h"

#include "backend.h"

#include <QCoreApplication>
#include <QCursor>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QString navIdOf(const QQuickItem *item)
{
    return item ? item->property("navId").toString() : QString();
}

bool hasProperty(const QObject *object, const char *name)
{
    return object->metaObject()->indexOfProperty(name) >= 0;
}

// where an item is in the window
QRectF sceneRect(const QQuickItem *item)
{
    return QRectF(item->mapToScene(QPointF(0, 0)), QSizeF(item->width(), item->height()));
}

// the key of the selected one in a list of maps (chrome's games or tabs), and the one `step` further, the list in
// the order given; empty with fewer than two shown
QString cycled(const QVariantList &list, int step, const char *key)
{
    QVariantList shown;
    for (const QVariant &v : list)
        if (v.toMap().value("visible", true).toBool())
            shown.append(v);
    if (shown.size() < 2)
        return {};
    int at = 0;
    for (int i = 0; i < shown.size(); ++i)
        if (shown.at(i).toMap().value("selected").toBool()) {
            at = i;
            break;
        }
    const int n = int(shown.size());
    return shown.at(((at + step) % n + n) % n).toMap().value(key).toString();
}

QString selectedGame(const QVariantMap &chrome)
{
    for (const QVariant &g : chrome.value("games").toList())
        if (g.toMap().value("selected").toBool())
            return g.toMap().value("key").toString();
    return {};
}

} // namespace

Navigator::Navigator(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend)
{
    connect(backend, &Backend::padNav, this, &Navigator::act);
    // a rebuilt page or header: the focus goes to the same control again, once the new controls exist
    connect(backend, &Backend::pageChanged, this, [this] { if (m_shown) restoreLater(); });
    connect(backend, &Backend::chromeChanged, this, [this] { if (m_shown) restoreLater(); });
    QCoreApplication::instance()->installEventFilter(this);
}

void Navigator::setup(QQuickItem *keyRoot, QObject *dialog)
{
    m_keyRoot = keyRoot;
    m_dialog = dialog;
    QQuickWindow *window = keyRoot ? keyRoot->window() : nullptr;
    if (!window)
        return;
    // the keys come back to the window after a text box, a list or the message box let them go
    auto lost = [window] { return !window->activeFocusItem() || window->activeFocusItem() == window->contentItem(); };
    connect(window, &QQuickWindow::activeFocusItemChanged, this, [this, lost] {
        if (lost())
            QTimer::singleShot(0, this, [this, lost] {
                if (lost() && m_keyRoot)
                    m_keyRoot->forceActiveFocus();
            });
    });
}

bool Navigator::key(int key, int modifiers)
{
    if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    QString action;
    switch (key) {
    case Qt::Key_Up: action = "up"; break;
    case Qt::Key_Down: action = "down"; break;
    case Qt::Key_Left: action = "left"; break;
    case Qt::Key_Right: action = "right"; break;
    case Qt::Key_Tab: action = "next"; break;
    case Qt::Key_Backtab: action = "prev"; break;
    case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space: action = "accept"; break;
    case Qt::Key_Escape: action = "back"; break;
    case Qt::Key_Delete: action = "secondary"; break;
    default: return false;
    }
    act(action);
    return true;
}

void Navigator::show()
{
    if (m_shown)
        return;
    m_shown = true;
    m_mouseAt = QCursor::pos();
    emit shownChanged();
}

bool Navigator::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (m_shown && (type == QEvent::MouseMove || type == QEvent::MouseButtonPress || type == QEvent::Wheel)) {
        // a real move from where the outline appeared (not the same spot again when a window opens)
        const bool used = type != QEvent::MouseMove
                          || (static_cast<QMouseEvent *>(event)->globalPosition() - m_mouseAt).manhattanLength() > 3;
        if (used) {
            m_shown = false;
            emit shownChanged();
        }
    }
    return QObject::eventFilter(watched, event);
}

bool Navigator::dialogOpen() const
{
    return m_dialog && m_dialog->property("visible").toBool();
}

// the message box while it is open, else the window
QQuickItem *Navigator::scope() const
{
    if (dialogOpen())
        return m_dialog->property("contentItem").value<QQuickItem *>();
    return m_keyRoot;
}

void Navigator::collect(QQuickItem *item, int zone, QQuickItem *column, QVector<Target> *out) const
{
    if (!item->isVisible() || item->opacity() == 0 || (item->clip() && (item->width() <= 0 || item->height() <= 0)))
        return;   // hidden, or folded away (a closed group)
    if (const QVariant z = item->property("navZone"); z.isValid())
        zone = z.toInt();
    if (item->property("navColumn").toBool())
        column = item;
    if (item->property("navTarget").toBool())
        out->append({item, zone, column});
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        collect(child, zone, column, out);
}

// every focusable control in the scope, in page order
QVector<Navigator::Target> Navigator::targets() const
{
    QVector<Target> out;
    if (QQuickItem *root = scope())
        collect(root, 0, nullptr, &out);
    return out;
}

// the focused control still on screen in the current scope (a rebuilt page makes new controls)
bool Navigator::valid(QQuickItem *item) const
{
    if (!item || !item->property("navTarget").toBool())
        return false;
    const QQuickItem *root = scope();
    for (const QQuickItem *p = item; p; p = p->parentItem()) {
        if (!p->isVisible())
            return false;
        if (p == root)
            return true;
    }
    return false;
}

// a text box has the keys
bool Navigator::typing() const
{
    const QQuickWindow *window = m_keyRoot ? m_keyRoot->window() : nullptr;
    const QQuickItem *focus = window ? window->activeFocusItem() : nullptr;
    return focus && hasProperty(focus, "cursorPosition");
}

void Navigator::setCurrent(QQuickItem *item, const QString &id)
{
    m_currentId = id;
    if (m_current == item)
        return;
    disconnect(m_currentGone);
    m_current = item;
    if (item)
        m_currentGone = connect(item, &QObject::destroyed, this, &Navigator::currentChanged);
    emit currentChanged();
}

void Navigator::focus(QQuickItem *item)
{
    setCurrent(item, navIdOf(item));
    if (item)
        reveal(item);
}

// the page (the nearest Flickable) scrolls to the focused control
void Navigator::reveal(QQuickItem *item)
{
    for (QQuickItem *p = item->parentItem(); p; p = p->parentItem()) {
        if (!hasProperty(p, "contentY") || !hasProperty(p, "contentItem") || !hasProperty(p, "flickableDirection"))
            continue;
        const QQuickItem *content = p->property("contentItem").value<QQuickItem *>();
        if (!content)
            return;
        const qreal y = item->mapToItem(content, QPointF(0, 0)).y(), margin = 24, height = p->height();
        qreal top = p->property("contentY").toReal();
        if (y < top + margin)
            top = y - margin;
        else if (y + item->height() > top + height - margin)
            top = y + item->height() - height + margin;
        p->setProperty("contentY", std::max<qreal>(0, std::min(top, p->property("contentHeight").toReal() - height)));
        return;
    }
}

// the control with the remembered navId, else the selected game card, else the first one
void Navigator::restore()
{
    if (valid(m_current))
        return;
    const QVector<Target> list = targets();
    if (list.isEmpty()) {
        setCurrent(nullptr, m_currentId);
        return;
    }
    const QString want = m_currentId, fallback = "game:" + selectedGame(m_backend->chrome());
    auto find = [&list](const QString &id) -> QQuickItem * {
        for (const Target &t : list)
            if (navIdOf(t.item) == id)
                return t.item;
        return nullptr;
    };
    QQuickItem *pick = want.isEmpty() ? nullptr : find(want);
    if (!pick)
        pick = find(fallback);
    if (!pick)
        pick = list.first().item;
    setCurrent(pick, want.isEmpty() ? navIdOf(pick) : want);
}

void Navigator::restoreLater()
{
    QMetaObject::invokeMethod(this, [this] { restore(); }, Qt::QueuedConnection);
}

// A move: left / right go to the nearest control that way in the same band; up / down stay in the page column
// (then a full-width section over or under it), else go to the next band: the selected game card, the shown tab,
// the nearest page control, or PLAY.
void Navigator::move(const QString &dir)
{
    const QVector<Target> all = targets();
    const Target *me = nullptr;
    for (const Target &t : all)
        if (t.item == m_current)
            me = &t;
    if (!me)
        return;
    const QRectF c = sceneRect(m_current);
    const bool vertical = dir == "up" || dir == "down";
    const int sign = dir == "down" || dir == "right" ? 1 : -1;
    // the nearest one ahead: the gap along the move, plus `sideways` times the gap to the side (entering a band:
    // the closest row first)
    auto nearest = [&](const QVector<Target> &list, qreal sideways) -> QQuickItem * {
        QQuickItem *best = nullptr;
        qreal bestScore = std::numeric_limits<qreal>::infinity();
        for (const Target &e : list) {
            if (e.item == m_current)
                continue;
            const QRectF r = sceneRect(e.item);
            // ahead: its centre past the focused control's edge that way (or past its centre for a control inside
            // it, the text box's button)
            const qreal centre = vertical ? r.center().y() : r.center().x();
            const bool inside = r.left() >= c.left() && r.top() >= c.top() && r.right() <= c.right() && r.bottom() <= c.bottom();
            const qreal edge = inside ? (vertical ? c.center().y() : c.center().x())
                               : sign > 0 ? (vertical ? c.bottom() : c.right()) : (vertical ? c.top() : c.left());
            if ((centre - edge) * sign <= 0)
                continue;
            const qreal gap = vertical ? std::max<qreal>(0, sign > 0 ? r.top() - c.bottom() : c.top() - r.bottom())
                                       : std::max<qreal>(0, sign > 0 ? r.left() - c.right() : c.left() - r.right());
            const qreal side = vertical ? std::max<qreal>({0, r.left() - c.right(), c.left() - r.right()})
                                        : std::max<qreal>({0, r.top() - c.bottom(), c.top() - r.bottom()});
            const qreal across = vertical ? std::abs(r.center().x() - c.center().x()) : std::abs(r.center().y() - c.center().y());
            const qreal score = gap + sideways * side + 0.05 * across;
            if (score < bestScore) {
                best = e.item;
                bestScore = score;
            }
        }
        return best;
    };
    auto filtered = [&all](auto keep) {
        QVector<Target> out;
        for (const Target &t : all)
            if (keep(t))
                out.append(t);
        return out;
    };
    const int zone = me->zone;
    QQuickItem *column = me->column;
    QQuickItem *pick = nullptr;
    if (!vertical) {
        pick = nearest(filtered([zone](const Target &t) { return t.zone == zone; }), 3);
    } else {
        pick = nearest(filtered([zone, column](const Target &t) { return t.zone == zone && t.column == column; }), 3);
        if (!pick && column) {   // a full-width section over or under the column
            const QRectF col = sceneRect(column);
            pick = nearest(filtered([zone, column, col](const Target &t) {
                if (t.zone != zone || !t.column || t.column == column)
                    return false;
                const QRectF r = sceneRect(t.column);
                return r.left() < col.right() && col.left() < r.right();
            }), 3);
        }
        for (int z = zone + sign; !pick && zone > 0 && z >= 1 && z <= 4; z += sign) {
            const QVector<Target> next = filtered([z](const Target &t) { return t.zone == z; });
            if (next.isEmpty())
                continue;
            const QString want = z == 1 ? "game:" + selectedGame(m_backend->chrome())
                               : z == 2 ? "tab:" + m_backend->tab() : z == 4 ? QStringLiteral("play") : QString();
            for (const Target &t : next)
                if (!want.isEmpty() && navIdOf(t.item) == want) {
                    pick = t.item;
                    break;
                }
            if (!pick)
                pick = nearest(next, 0.3);
            if (!pick)
                pick = next.first().item;
        }
    }
    if (pick)
        focus(pick);
}

// LB / RB: the tabs as the strip shows them (LAUNCHER at the right end); the focus goes to the new tab
void Navigator::cycleTab(int step)
{
    QVariantList ordered, launcher;
    for (const QVariant &t : m_backend->chrome().value("tabs").toList())
        (t.toMap().value("name").toString() == "launcher" ? launcher : ordered).append(t);
    const QString name = cycled(ordered + launcher, step, "name");
    if (name.isEmpty())
        return;
    m_backend->showTab(name);
    setCurrent(nullptr, "tab:" + name);
    restoreLater();
}

// LT / RT: the games; the focus stays where it was (on the new game's card when it was on a card)
void Navigator::cycleGame(int step)
{
    const QString key = cycled(m_backend->chrome().value("games").toList(), step, "key");
    if (key.isEmpty())
        return;
    const bool onCard = m_currentId.startsWith("game:");
    m_backend->selectGame(key);
    if (onCard)
        setCurrent(nullptr, "game:" + key);
    restoreLater();
}

void Navigator::listOpened(QQuickItem *box)
{
    const int at = std::max(0, box->property("navChosen").toInt());
    box->setProperty("navIndex", at);
    QMetaObject::invokeMethod(box, "navShow", Q_ARG(QVariant, at));
    m_popup = box;
}

void Navigator::listClosed(QQuickItem *box)
{
    if (m_popup == box)
        m_popup = nullptr;
}

// an open dropdown list: up / down choose, A picks, B closes
void Navigator::listKey(const QString &action)
{
    QQuickItem *box = m_popup;
    const int count = box->property("navCount").toInt();
    int at = box->property("navIndex").toInt();
    if (action == "up" || action == "prev" || action == "left") {
        at = std::max(0, at - 1);
    } else if (action == "down" || action == "next" || action == "right") {
        at = std::min(count - 1, at + 1);
    } else if (action == "accept" && at >= 0 && at < count) {
        QMetaObject::invokeMethod(box, "navPick", Q_ARG(QVariant, at));
        return;
    } else if (action == "back") {
        QMetaObject::invokeMethod(box, "navClose");
    }
    box->setProperty("navIndex", at);
    if (at >= 0)
        QMetaObject::invokeMethod(box, "navShow", Q_ARG(QVariant, at));
}

void Navigator::dialogOpened(QQuickItem *main)
{
    m_before = m_current;
    m_beforeId = m_currentId;
    setCurrent(main, navIdOf(main));
}

void Navigator::dialogClosed()
{
    setCurrent(m_before, m_beforeId);
}

void Navigator::act(const QString &action)
{
    if (!m_backend->ready() || m_backend->chrome().value("capturing").toBool() || !m_keyRoot)
        return;
    const bool wasShown = m_shown;
    show();
    if (m_popup) {   // an open dropdown list takes the moves
        listKey(action);
        return;
    }
    const bool isTyping = typing();
    const bool had = valid(m_current);
    restore();
    const bool anywhere = action == "play" || action == "tabPrev" || action == "tabNext" || action == "gamePrev"
                          || action == "gameNext";
    if (!m_current && !anywhere)
        return;
    const bool fresh = !wasShown || !had;   // the first press only shows where the focus is
    const bool dialog = dialogOpen();
    if (action == "up" || action == "down" || action == "left" || action == "right") {
        if (isTyping)
            m_keyRoot->forceActiveFocus();   // leave the text box
        if (fresh)
            reveal(m_current);
        else
            move(action);
    } else if (action == "next" || action == "prev") {
        if (isTyping)
            m_keyRoot->forceActiveFocus();
        const QVector<Target> list = targets();
        int at = -1;
        for (int i = 0; i < list.size(); ++i)
            if (list.at(i).item == m_current)
                at = i;
        if (!fresh && at >= 0) {
            const int n = int(list.size());
            focus(list.at(((at + (action == "next" ? 1 : -1)) % n + n) % n).item);
        } else {
            reveal(m_current);
        }
    } else if (action == "accept") {
        if (!fresh || isTyping)
            QMetaObject::invokeMethod(m_current, "navActivate");
    } else if (action == "secondary") {
        if (!fresh && m_current->metaObject()->indexOfMethod("navSecondary()") >= 0)
            QMetaObject::invokeMethod(m_current, "navSecondary");
    } else if (action == "back") {
        if (dialog) {
            QMetaObject::invokeMethod(m_dialog, "cancel");   // Cancel / No, or OK for a plain message
        } else if (isTyping) {
            m_keyRoot->forceActiveFocus();
        } else if (!m_currentId.startsWith("tab:")) {   // up to the tabs
            setCurrent(nullptr, "tab:" + m_backend->tab());
            restore();
            if (m_current)
                reveal(m_current);
        }
    } else if (action == "tabPrev" || action == "tabNext") {
        if (!dialog)
            cycleTab(action == "tabNext" ? 1 : -1);
    } else if (action == "gamePrev" || action == "gameNext") {
        if (!dialog)
            cycleGame(action == "gameNext" ? 1 : -1);
    } else if (action == "play") {   // Start: PLAY when it says so (never STOP or a blocked button)
        const QString label = m_backend->chrome().value("play").toString();
        if (!dialog && label != "STOP" && label != "UPDATING...")
            m_backend->play();
    }
}
