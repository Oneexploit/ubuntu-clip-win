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
class QObject;
class QFrame;

class PopupWindow : public QWidget {
    Q_OBJECT

public:
    explicit PopupWindow(ClipboardStore *store, QWidget *parent = nullptr);

public slots:
    void showPopup();
    void showPopupForWindow(const QString &targetWindowId);
    void refreshItems();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void activateCurrentItem();
    void activateListItem(QListWidgetItem *item);
    void showItemMenu(const QPoint &pos);
    void updateSelectionStyles();
    void clearHistoryWithConfirmation();

private:
    QWidget *createItemWidget(const ClipItem &item);
    QString typeTitle(const ClipItem &item) const;
    QString relativeTime(const QDateTime &utcDate) const;
    QString emptyStateText(bool searchIsActive) const;
    void activateById(int id);
    void selectItemById(int id);
    void setSelectedItemToClipboardOnly();
    void updateStatusForSelection();
    void repolish(QWidget *widget) const;

    bool handleKeyboardEvent(QKeyEvent *event, QObject *source = nullptr);
    bool handleDragEvent(QObject *watched, QEvent *event);
    bool watchedObjectCanStartDrag(QObject *watched) const;
    void beginPossibleDrag(const QPoint &globalPos, int clipId = -1);
    bool tryStartSystemMove();
    bool updateDrag(const QPoint &globalPos);
    void finishDrag();
    QPoint defaultPopupPosition() const;
    QPoint clampedPopupPosition(const QPoint &position) const;

    ClipboardStore *store_ = nullptr;
    QFrame *panel_ = nullptr;
    QLineEdit *search_ = nullptr;
    QListWidget *list_ = nullptr;
    QLabel *emptyLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPushButton *clearButton_ = nullptr;
    QString previousActiveWindowId_;

    bool pasteInProgress_ = false;
    bool dragCandidate_ = false;
    bool dragging_ = false;
    bool hasUserPosition_ = false;
    bool targetWindowProvided_ = false;
    int pressedClipId_ = -1;
    QPoint dragStartGlobal_;
    QPoint dragWindowStart_;
    QPoint userPosition_;
};
