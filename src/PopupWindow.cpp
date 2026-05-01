#include "PopupWindow.h"
#include "PasteController.h"

#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QColor>
#include <QCursor>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QStyle>
#include <QTimer>
#include <QWindow>

namespace {
constexpr int kPopupWidth = 456;
constexpr int kPopupHeight = 610;
constexpr int kBottomMargin = 26;
constexpr int kSideMargin = 18;
constexpr int kPasteDelayMs = 180;

QString pluralizedItems(int count) {
    return count == 1 ? QStringLiteral("1 item") : QStringLiteral("%1 items").arg(count);
}

bool isPlainLeftClick(const QMouseEvent *event) {
    return event && event->button() == Qt::LeftButton;
}
} // namespace

PopupWindow::PopupWindow(ClipboardStore *store, QWidget *parent)
    : QWidget(parent), store_(store) {
    setWindowTitle(QStringLiteral("Clipboard History"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setFixedSize(kPopupWidth, kPopupHeight);

    QSettings settings;
    if (settings.contains(QStringLiteral("popup/position"))) {
        userPosition_ = settings.value(QStringLiteral("popup/position")).toPoint();
        hasUserPosition_ = true;
    }

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(kSideMargin, kSideMargin, kSideMargin, kSideMargin);

    panel_ = new QFrame(this);
    panel_->setObjectName(QStringLiteral("panel"));
    panel_->installEventFilter(this);
    outerLayout->addWidget(panel_);

    auto *layout = new QVBoxLayout(panel_);
    layout->setContentsMargins(16, 14, 16, 13);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout();
    header->setSpacing(10);

    auto *logo = new QLabel(panel_);
    logo->setObjectName(QStringLiteral("appLogo"));
    logo->setFixedSize(36, 36);
    logo->setScaledContents(true);
    logo->setPixmap(QPixmap(QStringLiteral(":/icons/ubuntu-clip-win.png")).scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setCursor(Qt::OpenHandCursor);
    logo->installEventFilter(this);

    auto *titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("Clipboard History"), panel_);
    title->setObjectName(QStringLiteral("title"));
    title->setCursor(Qt::OpenHandCursor);
    title->installEventFilter(this);
    statusLabel_ = new QLabel(QStringLiteral("Ready"), panel_);
    statusLabel_->setObjectName(QStringLiteral("subtitle"));
    statusLabel_->setCursor(Qt::OpenHandCursor);
    statusLabel_->installEventFilter(this);
    titleBlock->addWidget(title);
    titleBlock->addWidget(statusLabel_);

    auto *shortcutHint = new QLabel(QStringLiteral("Ctrl + Super + V"), panel_);
    shortcutHint->setObjectName(QStringLiteral("keyCap"));
    shortcutHint->setAlignment(Qt::AlignCenter);
    shortcutHint->setCursor(Qt::OpenHandCursor);
    shortcutHint->installEventFilter(this);

    auto *closeButton = new QPushButton(QStringLiteral("×"), panel_);
    closeButton->setObjectName(QStringLiteral("closeButton"));
    closeButton->setFixedSize(30, 30);
    closeButton->setToolTip(QStringLiteral("Close"));
    closeButton->setCursor(Qt::PointingHandCursor);

    header->addWidget(logo);
    header->addLayout(titleBlock, 1);
    header->addWidget(shortcutHint);
    header->addWidget(closeButton);
    layout->addLayout(header);

    search_ = new QLineEdit(panel_);
    search_->setObjectName(QStringLiteral("searchBox"));
    search_->setPlaceholderText(QStringLiteral("Search text history"));
    search_->setClearButtonEnabled(true);
    search_->setMinimumHeight(38);
    search_->installEventFilter(this);
    layout->addWidget(search_);

    list_ = new QListWidget(panel_);
    list_->setObjectName(QStringLiteral("clipList"));
    list_->setFocusPolicy(Qt::StrongFocus);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setSpacing(7);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setUniformItemSizes(false);
    list_->installEventFilter(this);
    list_->viewport()->installEventFilter(this);
    layout->addWidget(list_, 1);

    emptyLabel_ = new QLabel(panel_);
    emptyLabel_->setObjectName(QStringLiteral("emptyLabel"));
    emptyLabel_->setTextFormat(Qt::RichText);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setCursor(Qt::OpenHandCursor);
    emptyLabel_->installEventFilter(this);
    emptyLabel_->hide();
    layout->addWidget(emptyLabel_, 1);

    auto *footer = new QHBoxLayout();
    footer->setSpacing(8);
    clearButton_ = new QPushButton(QStringLiteral("Clear"), panel_);
    clearButton_->setObjectName(QStringLiteral("footerButton"));
    clearButton_->setToolTip(QStringLiteral("Clear text history for this session"));
    clearButton_->setCursor(Qt::PointingHandCursor);
    auto *pasteHint = new QLabel(QStringLiteral("↑↓ select  •  Enter paste  •  drag header to move"), panel_);
    pasteHint->setObjectName(QStringLiteral("footerHint"));
    pasteHint->setCursor(Qt::OpenHandCursor);
    pasteHint->installEventFilter(this);
    footer->addWidget(clearButton_);
    footer->addStretch();
    footer->addWidget(pasteHint);
    layout->addLayout(footer);

    setStyleSheet(QStringLiteral(R"CSS(
        * {
            font-family: "Segoe UI", "Ubuntu", "Cantarell", sans-serif;
        }
        #panel {
            background-color: rgba(32, 32, 32, 248);
            border: 1px solid rgba(255, 255, 255, 46);
            border-radius: 18px;
        }
        #appLogo {
            border-radius: 9px;
        }
        #title {
            color: #ffffff;
            font-size: 21px;
            font-weight: 650;
        }
        #subtitle, #footerHint {
            color: rgba(255, 255, 255, 132);
            font-size: 12px;
        }
        #keyCap {
            background-color: rgba(255, 255, 255, 22);
            color: rgba(255, 255, 255, 190);
            border: 1px solid rgba(255, 255, 255, 35);
            border-radius: 12px;
            padding: 4px 10px;
            font-size: 12px;
            font-weight: 600;
        }
        #closeButton {
            background-color: transparent;
            color: rgba(255, 255, 255, 210);
            border: none;
            border-radius: 8px;
            font-size: 19px;
        }
        #closeButton:hover {
            background-color: rgba(255, 255, 255, 30);
        }
        #footerButton {
            background-color: rgba(255, 255, 255, 22);
            color: rgba(255, 255, 255, 222);
            border: 1px solid rgba(255, 255, 255, 32);
            border-radius: 8px;
            padding: 6px 11px;
            font-size: 12px;
        }
        #footerButton:hover {
            background-color: rgba(255, 255, 255, 40);
        }
        #footerButton:disabled {
            color: rgba(255, 255, 255, 70);
            background-color: rgba(255, 255, 255, 10);
            border: 1px solid rgba(255, 255, 255, 16);
        }
        #searchBox {
            background-color: rgba(45, 45, 45, 252);
            border: 1px solid rgba(255, 255, 255, 38);
            border-radius: 9px;
            padding: 8px 11px;
            color: #ffffff;
            font-size: 13px;
            selection-background-color: #4f8cff;
        }
        #searchBox:focus {
            border: 1px solid rgba(96, 165, 250, 170);
            background-color: rgba(50, 50, 50, 252);
        }
        #clipList {
            background-color: transparent;
            color: #ffffff;
            outline: none;
        }
        #clipList::item {
            border: none;
            margin: 0;
            padding: 0;
        }
        #clipList::item:selected {
            background-color: transparent;
        }
        QWidget#clipCard {
            background-color: rgba(255, 255, 255, 16);
            border: 1px solid rgba(255, 255, 255, 24);
            border-radius: 12px;
        }
        QWidget#clipCard:hover {
            background-color: rgba(255, 255, 255, 30);
            border: 1px solid rgba(255, 255, 255, 38);
        }
        QWidget#clipCard[selected="true"] {
            background-color: rgba(255, 255, 255, 42);
            border: 1px solid rgba(96, 165, 250, 180);
        }
        QWidget#clipCard[pinned="true"] {
            border: 1px solid rgba(255, 255, 255, 60);
        }
        QLabel#clipType {
            color: rgba(255, 255, 255, 158);
            font-size: 11px;
            font-weight: 600;
        }
        QLabel#clipPreview {
            color: rgba(255, 255, 255, 238);
            font-size: 13px;
            line-height: 18px;
        }
        QLabel#clipIcon {
            background-color: rgba(255, 255, 255, 20);
            border: 1px solid rgba(255, 255, 255, 20);
            border-radius: 9px;
            color: rgba(255, 255, 255, 205);
            font-size: 20px;
            font-weight: 650;
        }
        QLabel#pinTag {
            background-color: rgba(255, 255, 255, 24);
            color: rgba(255, 255, 255, 178);
            border-radius: 7px;
            padding: 2px 6px;
            font-size: 10px;
            font-weight: 650;
        }
        QPushButton#pinButton, QPushButton#deleteButton {
            background-color: transparent;
            color: rgba(255, 255, 255, 205);
            border: none;
            border-radius: 8px;
            font-size: 16px;
        }
        QPushButton#pinButton:hover, QPushButton#deleteButton:hover {
            background-color: rgba(255, 255, 255, 32);
        }
        QPushButton#deleteButton:hover {
            color: #ffffff;
        }
        #emptyLabel {
            color: rgba(255, 255, 255, 155);
            padding: 34px 22px;
            font-size: 13px;
        }
        QMenu {
            background-color: #2b2b2b;
            color: #ffffff;
            border: 1px solid rgba(255, 255, 255, 55);
            border-radius: 8px;
            padding: 5px;
        }
        QMenu::item {
            padding: 7px 30px 7px 12px;
            border-radius: 6px;
        }
        QMenu::item:selected {
            background-color: rgba(255, 255, 255, 35);
        }
        QScrollBar:vertical {
            background-color: transparent;
            width: 8px;
            margin: 3px 0 3px 0;
        }
        QScrollBar::handle:vertical {
            background-color: rgba(255, 255, 255, 48);
            min-height: 28px;
            border-radius: 4px;
        }
        QScrollBar::handle:vertical:hover {
            background-color: rgba(255, 255, 255, 72);
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
    )CSS"));

    connect(closeButton, &QPushButton::clicked, this, &QWidget::hide);
    connect(clearButton_, &QPushButton::clicked, this, &PopupWindow::clearHistoryWithConfirmation);
    connect(search_, &QLineEdit::textChanged, this, &PopupWindow::refreshItems);
    connect(list_, &QListWidget::itemActivated, this, &PopupWindow::activateListItem);
    connect(list_, &QListWidget::customContextMenuRequested, this, &PopupWindow::showItemMenu);
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) {
        updateSelectionStyles();
        updateStatusForSelection();
    });
    connect(store_, &ClipboardStore::changed, this, &PopupWindow::refreshItems);
}

void PopupWindow::showPopupForWindow(const QString &targetWindowId) {
    const QString trimmed = targetWindowId.trimmed();
    if (!trimmed.isEmpty()) {
        previousActiveWindowId_ = trimmed;
        targetWindowProvided_ = true;
    }
    showPopup();
}

void PopupWindow::showPopup() {
    if (targetWindowProvided_) {
        targetWindowProvided_ = false;
    } else {
        previousActiveWindowId_ = PasteController::activeWindowId();
    }
    pasteInProgress_ = false;

    if (search_) {
        search_->clear();
    }
    refreshItems();

    const QPoint target = hasUserPosition_ ? clampedPopupPosition(userPosition_) : defaultPopupPosition();
    move(target);

    setWindowOpacity(0.0);
    show();
    raise();
    activateWindow();

    auto *animation = new QPropertyAnimation(this, "windowOpacity", this);
    animation->setDuration(110);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->start(QAbstractAnimation::DeleteWhenStopped);

    if (list_->count() > 0) {
        list_->setCurrentRow(0);
        list_->scrollToItem(list_->currentItem(), QAbstractItemView::PositionAtTop);
    }
    if (list_ && list_->count() > 0) {
        list_->setFocus(Qt::ShortcutFocusReason);
    } else if (search_) {
        search_->setFocus(Qt::ShortcutFocusReason);
    }
    updateSelectionStyles();
    updateStatusForSelection();
}

QPoint PopupWindow::defaultPopupPosition() const {
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return QPoint(100, 100);
    }

    const QRect available = screen->availableGeometry();
    const int x = available.x() + (available.width() - width()) / 2;
    const int preferredY = available.y() + available.height() - height() - kBottomMargin;
    const int y = qMax(available.y() + kBottomMargin, preferredY);
    return QPoint(x, y);
}

QPoint PopupWindow::clampedPopupPosition(const QPoint &position) const {
    QScreen *screen = QGuiApplication::screenAt(position + QPoint(width() / 2, height() / 2));
    if (!screen) {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return position;
    }

    const QRect available = screen->availableGeometry().adjusted(8, 8, -8, -8);
    const int x = qBound(available.left(), position.x(), qMax(available.left(), available.right() - width() + 1));
    const int y = qBound(available.top(), position.y(), qMax(available.top(), available.bottom() - height() + 1));
    return QPoint(x, y);
}

void PopupWindow::refreshItems() {
    if (!store_) {
        return;
    }

    const int previousId = list_ && list_->currentItem()
        ? list_->currentItem()->data(Qt::UserRole).toInt()
        : -1;
    const QString query = search_ ? search_->text() : QString();
    const bool searchIsActive = !query.trimmed().isEmpty();
    const auto items = store_->recentItems(query, 80);
    const bool hasAnyItems = !store_->recentItems(QString(), 1).isEmpty();

    list_->clear();
    list_->setVisible(!items.isEmpty());
    emptyLabel_->setVisible(items.isEmpty());
    emptyLabel_->setText(emptyStateText(searchIsActive));
    statusLabel_->setText(searchIsActive
        ? QStringLiteral("%1 found").arg(pluralizedItems(items.size()))
        : pluralizedItems(items.size()));
    if (clearButton_) {
        clearButton_->setEnabled(hasAnyItems);
    }

    int rowToSelect = -1;
    int rowIndex = 0;
    for (const ClipItem &item : items) {
        auto *row = new QListWidgetItem();
        row->setData(Qt::UserRole, item.id);
        row->setSizeHint(QSize(list_->viewport()->width() - 8, 82));
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        list_->addItem(row);
        list_->setItemWidget(row, createItemWidget(item));

        if (item.id == previousId) {
            rowToSelect = rowIndex;
        }
        ++rowIndex;
    }

    if (list_->count() > 0) {
        list_->setCurrentRow(rowToSelect >= 0 ? rowToSelect : 0);
    }
    updateSelectionStyles();
    updateStatusForSelection();
}

QWidget *PopupWindow::createItemWidget(const ClipItem &item) {
    auto *card = new QWidget(list_);
    card->setObjectName(QStringLiteral("clipCard"));
    card->setProperty("clipId", item.id);
    card->setProperty("selected", false);
    card->setProperty("pinned", item.pinned);
    card->setCursor(Qt::PointingHandCursor);
    card->installEventFilter(this);

    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(9, 8, 8, 8);
    layout->setSpacing(10);

    auto *visual = new QLabel(card);
    visual->setObjectName(QStringLiteral("clipIcon"));
    visual->setAlignment(Qt::AlignCenter);
    visual->setFixedSize(48, 48);
    visual->setAttribute(Qt::WA_TransparentForMouseEvents);

    visual->setText(QStringLiteral("T"));
    layout->addWidget(visual);

    auto *textLayout = new QVBoxLayout();
    textLayout->setSpacing(4);

    auto *topLine = new QHBoxLayout();
    topLine->setSpacing(6);
    auto *meta = new QLabel(typeTitle(item) + QStringLiteral("  •  ") + relativeTime(item.updatedAt), card);
    meta->setObjectName(QStringLiteral("clipType"));
    meta->setAttribute(Qt::WA_TransparentForMouseEvents);
    topLine->addWidget(meta, 1);
    if (item.pinned) {
        auto *pinTag = new QLabel(QStringLiteral("PINNED"), card);
        pinTag->setObjectName(QStringLiteral("pinTag"));
        pinTag->setAttribute(Qt::WA_TransparentForMouseEvents);
        topLine->addWidget(pinTag);
    }
    textLayout->addLayout(topLine);

    auto *preview = new QLabel(item.previewText(), card);
    preview->setObjectName(QStringLiteral("clipPreview"));
    preview->setWordWrap(true);
    preview->setMaximumHeight(39);
    preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    textLayout->addWidget(preview);
    layout->addLayout(textLayout, 1);

    auto *buttons = new QVBoxLayout();
    buttons->setSpacing(3);
    auto *pin = new QPushButton(item.pinned ? QStringLiteral("★") : QStringLiteral("☆"), card);
    pin->setObjectName(QStringLiteral("pinButton"));
    pin->setFixedSize(30, 27);
    pin->setToolTip(item.pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin"));
    pin->setCursor(Qt::PointingHandCursor);
    auto *remove = new QPushButton(QStringLiteral("×"), card);
    remove->setObjectName(QStringLiteral("deleteButton"));
    remove->setFixedSize(30, 27);
    remove->setToolTip(QStringLiteral("Delete"));
    remove->setCursor(Qt::PointingHandCursor);
    buttons->addWidget(pin);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(pin, &QPushButton::clicked, this, [this, id = item.id]() {
        if (store_) {
            store_->togglePinned(id);
        }
    });
    connect(remove, &QPushButton::clicked, this, [this, id = item.id]() {
        if (store_) {
            store_->deleteItem(id);
        }
    });

    return card;
}

QString PopupWindow::typeTitle(const ClipItem &item) const {
    const QString title = QStringLiteral("Text");
    if (item.pinned) {
        return QStringLiteral("Pinned • %1").arg(title);
    }
    return title;
}

QString PopupWindow::relativeTime(const QDateTime &utcDate) const {
    if (!utcDate.isValid()) {
        return QStringLiteral("Just now");
    }

    const QDateTime localDate = utcDate.toLocalTime();
    const qint64 secs = localDate.secsTo(QDateTime::currentDateTime());
    if (secs < 60) {
        return QStringLiteral("Just now");
    }
    if (secs < 3600) {
        return QStringLiteral("%1m ago").arg(secs / 60);
    }
    if (secs < 86400) {
        return QStringLiteral("%1h ago").arg(secs / 3600);
    }
    if (secs < 604800) {
        return QStringLiteral("%1d ago").arg(secs / 86400);
    }
    return localDate.toString(QStringLiteral("MMM d"));
}

QString PopupWindow::emptyStateText(bool searchIsActive) const {
    if (searchIsActive) {
        return QStringLiteral(
            "<div style='font-size:32px; margin-bottom:10px;'>⌕</div>"
            "<div style='font-size:15px; color:#ffffff;'>No results found</div>"
            "<div style='margin-top:6px;'>Try a different word or clear the search box.</div>");
    }

    return QStringLiteral(
        "<div style='font-size:34px; margin-bottom:10px;'>▣</div>"
        "<div style='font-size:15px; color:#ffffff;'>Your clipboard history is empty</div>"
        "<div style='margin-top:6px;'>Copy any text, code, token, or terminal output. Then press <b>Ctrl + Super + V</b>.</div>");
}

void PopupWindow::activateListItem(QListWidgetItem *item) {
    if (!item) {
        return;
    }
    activateById(item->data(Qt::UserRole).toInt());
}

void PopupWindow::activateCurrentItem() {
    activateListItem(list_->currentItem());
}

void PopupWindow::activateById(int id) {
    if (!store_ || pasteInProgress_) {
        return;
    }

    const auto item = store_->itemById(id);
    if (!item.has_value()) {
        return;
    }

    pasteInProgress_ = true;
    statusLabel_->setText(QStringLiteral("Pasting…"));
    store_->setToClipboard(*item);
    hide();

    const QString targetWindowId = previousActiveWindowId_;
    QTimer::singleShot(kPasteDelayMs, this, [this, targetWindowId]() {
        PasteController::tryPasteToWindow(targetWindowId);
        pasteInProgress_ = false;
    });
}

void PopupWindow::selectItemById(int id) {
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem *item = list_->item(row);
        if (item && item->data(Qt::UserRole).toInt() == id) {
            list_->setCurrentRow(row);
            list_->scrollToItem(item, QAbstractItemView::EnsureVisible);
            return;
        }
    }
}

void PopupWindow::setSelectedItemToClipboardOnly() {
    if (!store_ || !list_->currentItem()) {
        return;
    }

    const int id = list_->currentItem()->data(Qt::UserRole).toInt();
    const auto item = store_->itemById(id);
    if (!item.has_value()) {
        return;
    }

    store_->setToClipboard(*item);
    statusLabel_->setText(QStringLiteral("Copied selected item. Press Ctrl+V to paste."));
}

void PopupWindow::showItemMenu(const QPoint &pos) {
    QListWidgetItem *row = list_->itemAt(pos);
    if (!row || !store_) {
        return;
    }

    const int id = row->data(Qt::UserRole).toInt();
    const auto item = store_->itemById(id);
    if (!item.has_value()) {
        return;
    }

    selectItemById(id);

    QMenu menu(this);
    QAction *pasteAction = menu.addAction(QStringLiteral("Paste"));
    QAction *copyAction = menu.addAction(QStringLiteral("Copy only"));
    menu.addSeparator();
    QAction *pinAction = menu.addAction(item->pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin"));
    QAction *deleteAction = menu.addAction(QStringLiteral("Delete"));

    QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    }

    if (chosen == pasteAction) {
        activateById(id);
    } else if (chosen == copyAction) {
        setSelectedItemToClipboardOnly();
    } else if (chosen == pinAction) {
        store_->togglePinned(id);
    } else if (chosen == deleteAction) {
        store_->deleteItem(id);
    }
}

void PopupWindow::updateSelectionStyles() {
    if (!list_) {
        return;
    }

    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem *item = list_->item(row);
        QWidget *widget = list_->itemWidget(item);
        if (!widget) {
            continue;
        }
        widget->setProperty("selected", item == list_->currentItem());
        repolish(widget);
    }
}

void PopupWindow::updateStatusForSelection() {
    if (!statusLabel_ || !store_) {
        return;
    }

    if (!list_ || list_->count() == 0 || !list_->currentItem()) {
        statusLabel_->setText(QStringLiteral("Ready"));
        return;
    }

    const int id = list_->currentItem()->data(Qt::UserRole).toInt();
    const auto item = store_->itemById(id);
    if (!item.has_value()) {
        statusLabel_->setText(pluralizedItems(list_->count()));
        return;
    }

    statusLabel_->setText(QStringLiteral("%1 selected • Enter to paste").arg(typeTitle(*item)));
}

void PopupWindow::clearHistoryWithConfirmation() {
    if (!store_) {
        return;
    }

    const QMessageBox::StandardButton result = QMessageBox::question(
        this,
        QStringLiteral("Clear clipboard history"),
        QStringLiteral("Delete all text history items for this session?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (result == QMessageBox::Yes) {
        store_->clearUnpinned();
    }
}

void PopupWindow::repolish(QWidget *widget) const {
    if (!widget) {
        return;
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

bool PopupWindow::handleKeyboardEvent(QKeyEvent *event, QObject *source) {
    if (!event) {
        return false;
    }

    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    const bool noModifier = event->modifiers() == Qt::NoModifier;
    const bool fromSearch = source == search_;

    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return true;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        activateCurrentItem();
        event->accept();
        return true;
    }

    if (ctrl && event->key() == Qt::Key_F) {
        search_->setFocus(Qt::ShortcutFocusReason);
        search_->selectAll();
        event->accept();
        return true;
    }

    if (ctrl && event->key() == Qt::Key_C && !(fromSearch && search_->hasSelectedText())) {
        setSelectedItemToClipboardOnly();
        event->accept();
        return true;
    }

    if (ctrl && event->key() == Qt::Key_P && list_->currentItem() && store_) {
        store_->togglePinned(list_->currentItem()->data(Qt::UserRole).toInt());
        event->accept();
        return true;
    }

    if (!fromSearch && ctrl && event->key() == Qt::Key_Delete) {
        clearHistoryWithConfirmation();
        event->accept();
        return true;
    }

    if (!fromSearch && noModifier && event->key() == Qt::Key_Delete && list_->currentItem()) {
        if (store_) {
            store_->deleteItem(list_->currentItem()->data(Qt::UserRole).toInt());
        }
        event->accept();
        return true;
    }

    if (!fromSearch && noModifier && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) {
        const int row = event->key() - Qt::Key_1;
        if (row >= 0 && row < list_->count()) {
            list_->setCurrentRow(row);
            list_->scrollToItem(list_->currentItem(), QAbstractItemView::EnsureVisible);
            activateCurrentItem();
            event->accept();
            return true;
        }
    }

    if (noModifier && list_->count() > 0) {
        int row = list_->currentRow();
        if (row < 0) {
            row = 0;
        }

        bool handledNavigation = true;
        switch (event->key()) {
        case Qt::Key_Down:
            row = qMin(row + 1, list_->count() - 1);
            break;
        case Qt::Key_Up:
            row = qMax(row - 1, 0);
            break;
        case Qt::Key_PageDown:
            row = qMin(row + 6, list_->count() - 1);
            break;
        case Qt::Key_PageUp:
            row = qMax(row - 6, 0);
            break;
        case Qt::Key_Home:
            row = 0;
            break;
        case Qt::Key_End:
            row = list_->count() - 1;
            break;
        default:
            handledNavigation = false;
            break;
        }

        if (handledNavigation) {
            list_->setCurrentRow(row);
            list_->scrollToItem(list_->currentItem(), QAbstractItemView::EnsureVisible);
            updateSelectionStyles();
            updateStatusForSelection();
            event->accept();
            return true;
        }
    }

    return false;
}

bool PopupWindow::watchedObjectCanStartDrag(QObject *watched) const {
    if (watched == this || watched == panel_ || watched == emptyLabel_ || watched == statusLabel_) {
        return true;
    }

    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget) {
        return false;
    }

    if (widget->property("clipId").isValid()) {
        return true;
    }

    return widget->objectName() == QStringLiteral("title")
        || widget->objectName() == QStringLiteral("appLogo")
        || widget->objectName() == QStringLiteral("keyCap")
        || widget->objectName() == QStringLiteral("footerHint");
}

void PopupWindow::beginPossibleDrag(const QPoint &globalPos, int clipId) {
    dragCandidate_ = true;
    dragging_ = false;
    pressedClipId_ = clipId;
    dragStartGlobal_ = globalPos;
    dragWindowStart_ = pos();
}

bool PopupWindow::tryStartSystemMove() {
    if (!windowHandle()) {
        winId(); // ensure a native window exists before requesting compositor move
    }
    QWindow *window = windowHandle();
    if (!window) {
        return false;
    }

    const bool started = window->startSystemMove();
    if (started) {
        dragging_ = true;
        setCursor(Qt::ClosedHandCursor);
    }
    return started;
}

bool PopupWindow::updateDrag(const QPoint &globalPos) {
    if (!dragCandidate_) {
        return false;
    }

    const QPoint delta = globalPos - dragStartGlobal_;
    if (!dragging_ && delta.manhattanLength() < QApplication::startDragDistance()) {
        return false;
    }

    dragging_ = true;
    move(clampedPopupPosition(dragWindowStart_ + delta));
    userPosition_ = pos();
    hasUserPosition_ = true;
    return true;
}

void PopupWindow::finishDrag() {
    if (dragging_) {
        userPosition_ = pos();
        hasUserPosition_ = true;
        QSettings settings;
        settings.setValue(QStringLiteral("popup/position"), userPosition_);
    }
    dragCandidate_ = false;
    dragging_ = false;
    pressedClipId_ = -1;
}

bool PopupWindow::handleDragEvent(QObject *watched, QEvent *event) {
    if (!event) {
        return false;
    }

    const bool hasListViewport = list_ && list_->viewport();

    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            return false;
        }

        int clipId = -1;
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && widget->property("clipId").isValid()) {
            clipId = widget->property("clipId").toInt();
        }

        const bool emptyListArea = hasListViewport && watched == list_->viewport() && !list_->itemAt(mouseEvent->pos());
        const bool dragZone = emptyListArea || watchedObjectCanStartDrag(watched);
        if (!dragZone) {
            return false;
        }

        beginPossibleDrag(mouseEvent->globalPosition().toPoint(), clipId);
        setCursor(Qt::OpenHandCursor);

        // Header/empty-space drag should use the compositor-native move API.
        // This is required on Wayland, where normal QWidget::move() can be ignored.
        if (clipId < 0 && tryStartSystemMove()) {
            event->accept();
            return true;
        }

        return false;
    }

    if (event->type() == QEvent::MouseMove) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (!mouseEvent->buttons().testFlag(Qt::LeftButton) || !dragCandidate_) {
            return false;
        }

        const QPoint delta = mouseEvent->globalPosition().toPoint() - dragStartGlobal_;
        if (!dragging_ && delta.manhattanLength() >= QApplication::startDragDistance()) {
            // For card drag on Wayland, try native move after the drag threshold.
            // A simple click on the card still pastes normally.
            if (tryStartSystemMove()) {
                event->accept();
                return true;
            }
        }

        if (updateDrag(mouseEvent->globalPosition().toPoint())) {
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        if (!dragCandidate_) {
            return false;
        }
        const bool wasDragging = dragging_;
        unsetCursor();
        finishDrag();
        if (wasDragging) {
            event->accept();
            return true;
        }
    }

    return false;
}

bool PopupWindow::eventFilter(QObject *watched, QEvent *event) {
    const bool watchedSearch = search_ && watched == search_;
    const bool watchedList = list_ && watched == list_;
    const bool watchedListViewport = list_ && list_->viewport() && watched == list_->viewport();

    if ((watchedSearch || watchedList || watchedListViewport) && event->type() == QEvent::KeyPress) {
        if (handleKeyboardEvent(static_cast<QKeyEvent *>(event), watched)) {
            return true;
        }
    }

    if (handleDragEvent(watched, event)) {
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && widget->property("clipId").isValid()) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (isPlainLeftClick(mouseEvent)) {
                const int id = widget->property("clipId").toInt();
                selectItemById(id);
                activateById(id);
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void PopupWindow::keyPressEvent(QKeyEvent *event) {
    if (handleKeyboardEvent(event)) {
        return;
    }

    QWidget::keyPressEvent(event);
}
