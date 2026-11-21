// Controller and keyboard navigation of the launcher window. The QML controls only declare themselves (navTarget,
// navId, navActivate(), optionally navSecondary(); bands navZone and page columns navColumn); this object walks the
// item tree, moves the focus, scrolls the page to it, finds it again after a rebuilt page and runs the actions.
// The controller's presses come from Backend::padNav, the keys from the QML Keys handlers (key()).
#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <QVector>

class Backend;

class Navigator : public QObject {
    Q_OBJECT
    // the focused control, and whether its outline shows (a controller or the keyboard in use; the mouse hides it)
    Q_PROPERTY(QQuickItem *current READ current NOTIFY currentChanged)
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)

public:
    explicit Navigator(Backend *backend, QObject *parent = nullptr);

    QQuickItem *current() const { return m_current; }
    bool shown() const { return m_shown; }

    // the window's item that takes the keys, and the message box (a Popup: visible, contentItem, cancel())
    Q_INVOKABLE void setup(QQuickItem *keyRoot, QObject *dialog);
    // a key press: true when it is a navigation key (arrows, Tab, Enter / Space, Escape, Delete), then done
    Q_INVOKABLE bool key(int key, int modifiers);
    // up, down, left, right, next, prev, accept, back, secondary, tabPrev, tabNext, gamePrev, gameNext, play
    Q_INVOKABLE void act(const QString &action);
    // a DropdownBox's list opened / closed (navCount, navChosen, navIndex, navPick(int), navClose(), navShow(int))
    Q_INVOKABLE void listOpened(QQuickItem *box);
    Q_INVOKABLE void listClosed(QQuickItem *box);
    // the message box opened (its main button takes the focus) / closed (the focus goes back)
    Q_INVOKABLE void dialogOpened(QQuickItem *main);
    Q_INVOKABLE void dialogClosed();

signals:
    void currentChanged();
    void shownChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;   // any mouse use hides the outline

private:
    struct Target {
        QQuickItem *item;
        int zone;             // 1 game cards, 2 tabs, 3 page, 4 PLAY bar, 0 the message box
        QQuickItem *column;   // the page column it is in (null outside the page)
    };
    bool dialogOpen() const;
    QQuickItem *scope() const;
    void collect(QQuickItem *item, int zone, QQuickItem *column, QVector<Target> *out) const;
    QVector<Target> targets() const;
    bool valid(QQuickItem *item) const;
    bool typing() const;
    void setCurrent(QQuickItem *item, const QString &id);
    void focus(QQuickItem *item);
    void reveal(QQuickItem *item);
    void restore();
    void restoreLater();
    void move(const QString &dir);
    void cycleTab(int step);
    void cycleGame(int step);
    void listKey(const QString &action);
    void show();

    Backend *m_backend;
    QPointer<QQuickItem> m_keyRoot;
    QPointer<QObject> m_dialog;
    QPointer<QQuickItem> m_current;
    QMetaObject::Connection m_currentGone;
    QString m_currentId;            // its navId: the rebuilt control is found by it
    QPointer<QQuickItem> m_popup;   // an open dropdown list takes the moves
    QPointer<QQuickItem> m_before;  // the focus before the message box opened
    QString m_beforeId;
    bool m_shown = false;
    QPointF m_mouseAt;
};
