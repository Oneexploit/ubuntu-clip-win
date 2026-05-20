#pragma once

#include "ClipboardStore.h"

#include <QDateTime>
#include <QLineEdit>
#include <QListWidget>
#include <QPoint>
#include <QPushButton>
#include <QWidget>

class QLabel;
class QKeyEvent;
class QEvent;
class QHideEvent;
class QObject;
class QFrame;
class QListWidgetItem;

class PopupWindow : public QWidget {
    Q_OBJECT

public:
    explicit PopupWindow(ClipboardStore *store, QWidget *parent = nullptr);

public slots:
    void showPopup();
    void showPopupForWindow(const QString &targetWindowId);
    void refreshItems();
    void clearHistoryWithConfirmation();

signals:
    void settingsRequested();
    void notificationRequested(const QString &title, const QString &message, bool warning);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void activateCurrentItem();
    void activateListItem(QListWidgetItem *item);
    void showItemMenu(const QPoint &pos);
    void updateSelectionStyles();

private:
    enum class ActivationSource {
        Keyboard,
        Mouse,
        Menu
    };

    QWidget *createItemWidget(const ClipItem &item);
    QString typeTitle(const ClipItem &item) const;
    QString relativeTime(const QDateTime &utcDate) const;
    QString emptyStateText(bool searchIsActive) const;
    void activateById(int id, ActivationSource source = ActivationSource::Keyboard);
    void selectItemById(int id);
    void setSelectedItemToClipboardOnly();
    void updateStatusForSelection();
    void updateChromeText();
    void repolish(QWidget *widget) const;

    bool handleKeyboardEvent(QKeyEvent *event, QObject *source = nullptr);
    bool handleDragEvent(QObject *watched, QEvent *event);
    bool watchedObjectCanStartDrag(QObject *watched) const;
    void beginPossibleDrag(const QPoint &globalPos);
    void watchForDragRelease();
    bool tryStartSystemMove();
    bool updateDrag(const QPoint &globalPos);
    void finishDrag();
    QPoint defaultPopupPosition() const;
    QPoint clampedPopupPosition(const QPoint &position) const;

    ClipboardStore *store_ = nullptr;
    QFrame *panel_ = nullptr;
    QWidget *headerBar_ = nullptr;
    QLineEdit *search_ = nullptr;
    QListWidget *list_ = nullptr;
    QLabel *emptyLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *shortcutHint_ = nullptr;
    QLabel *footerHint_ = nullptr;
    QPushButton *clearButton_ = nullptr;
    QPushButton *settingsButton_ = nullptr;
    QString previousActiveWindowId_;

    bool pasteInProgress_ = false;
    bool dragCandidate_ = false;
    bool dragging_ = false;
    bool dragReleaseWatchScheduled_ = false;
    bool hasUserPosition_ = false;
    bool targetWindowProvided_ = false;
    QPoint dragStartGlobal_;
    QPoint dragWindowStart_;
    QPoint previousPointerGlobalPos_;
    QPoint userPosition_;
};
