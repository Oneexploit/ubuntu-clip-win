#include "PopupWindow.h"

#include "AppIntegration.h"
#include "AppSettings.h"
#include "PasteController.h"
#include "RuntimeLog.h"

#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QColor>
#include <QCursor>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QStyle>
#include <QTimer>
#include <QWindow>

namespace {
constexpr int kPopupWidth = 500;
constexpr int kPopupHeight = 620;
constexpr int kBottomMargin = 26;
constexpr int kSideMargin = 18;
constexpr int kPasteDelayMs = 180;

QString pluralizedItems(int count) {
    return count == 1 ? QStringLiteral("1 item") : QStringLiteral("%1 items").arg(count);
}

QString iconLabelForItem(const ClipItem &item) {
    Q_UNUSED(item);
    return QStringLiteral("TXT");
}

bool isPlainLeftClick(const QMouseEvent *event) {
    return event && event->button() == Qt::LeftButton;
}
} // namespace

PopupWindow::PopupWindow(ClipboardStore *store, QWidget *parent)
    : QWidget(parent), store_(store) {
    RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("constructed"));
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
    outerLayout->addWidget(panel_);

    auto *layout = new QVBoxLayout(panel_);
    layout->setContentsMargins(16, 14, 16, 13);
    layout->setSpacing(10);

    headerBar_ = new QWidget(panel_);
    headerBar_->setObjectName(QStringLiteral("headerBar"));
    headerBar_->installEventFilter(this);

    auto *header = new QHBoxLayout(headerBar_);
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(10);

    auto *logo = new QLabel(headerBar_);
    logo->setObjectName(QStringLiteral("appLogo"));
    logo->setFixedSize(36, 36);
    logo->setAlignment(Qt::AlignCenter);
    logo->setScaledContents(true);
    logo->setPixmap(QPixmap(QStringLiteral(":/icons/ubuntu-clip-win.png")).scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->installEventFilter(this);

    auto *titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("Clipboard History"), headerBar_);
    title->setObjectName(QStringLiteral("title"));
    title->installEventFilter(this);

    statusLabel_ = new QLabel(QStringLiteral("Ready"), headerBar_);
    statusLabel_->setObjectName(QStringLiteral("subtitle"));
    statusLabel_->installEventFilter(this);
    titleBlock->addWidget(title);
    titleBlock->addWidget(statusLabel_);

    shortcutHint_ = new QLabel(headerBar_);
    shortcutHint_->setObjectName(QStringLiteral("keyCap"));
    shortcutHint_->setAlignment(Qt::AlignCenter);
    shortcutHint_->installEventFilter(this);

    auto *closeButton = new QPushButton(QStringLiteral("x"), headerBar_);
    closeButton->setObjectName(QStringLiteral("closeButton"));
    closeButton->setFixedSize(30, 30);
    closeButton->setToolTip(QStringLiteral("Close"));
    closeButton->setCursor(Qt::PointingHandCursor);

    header->addWidget(logo);
    header->addLayout(titleBlock, 1);
    header->addWidget(shortcutHint_);
    header->addWidget(closeButton);
    layout->addWidget(headerBar_);

    search_ = new QLineEdit(panel_);
    search_->setObjectName(QStringLiteral("searchBox"));
    search_->setPlaceholderText(QStringLiteral("Search copied text"));
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
    emptyLabel_->hide();
    layout->addWidget(emptyLabel_, 1);

    auto *footer = new QHBoxLayout();
    footer->setSpacing(8);

    settingsButton_ = new QPushButton(QStringLiteral("Settings"), panel_);
    settingsButton_->setObjectName(QStringLiteral("footerButton"));
    settingsButton_->setCursor(Qt::PointingHandCursor);

    clearButton_ = new QPushButton(QStringLiteral("Clear"), panel_);
    clearButton_->setObjectName(QStringLiteral("footerButton"));
    clearButton_->setToolTip(QStringLiteral("Clear unpinned history"));
    clearButton_->setCursor(Qt::PointingHandCursor);

    footerHint_ = new QLabel(panel_);
    footerHint_->setObjectName(QStringLiteral("footerHint"));

    footer->addWidget(settingsButton_);
    footer->addWidget(clearButton_);
    footer->addStretch();
    footer->addWidget(footerHint_);
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
            font-size: 16px;
            font-weight: 700;
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
            font-size: 12px;
            font-weight: 700;
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
            font-size: 12px;
            font-weight: 700;
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

    connect(closeButton, &QPushButton::clicked, this, [this]() {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("close-button-clicked"));
        hide();
    });
    connect(settingsButton_, &QPushButton::clicked, this, [this]() {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("settings-button-clicked"));
        emit settingsRequested();
    });
    connect(clearButton_, &QPushButton::clicked, this, [this]() {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("clear-button-clicked"));
        clearHistoryWithConfirmation();
    });
    connect(search_, &QLineEdit::textChanged, this, [this](const QString &text) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("search-text-changed text=%1").arg(text));
        refreshItems();
    });
    connect(list_, &QListWidget::itemActivated, this, &PopupWindow::activateListItem);
    connect(list_, &QListWidget::customContextMenuRequested, this, &PopupWindow::showItemMenu);
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) {
        RuntimeLog::write(QStringLiteral("PopupWindow"),
                          QStringLiteral("current-item-changed id=%1 row=%2")
                              .arg(list_ && list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toInt() : -1)
                              .arg(list_ ? list_->currentRow() : -1));
        updateSelectionStyles();
        updateStatusForSelection();
    });
    connect(store_, &ClipboardStore::changed, this, &PopupWindow::refreshItems);

    updateChromeText();
}

void PopupWindow::showPopupForWindow(const QString &targetWindowId) {
    const QString trimmed = targetWindowId.trimmed();
    RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("show-popup-for-window targetWindowId=%1").arg(trimmed));
    if (!trimmed.isEmpty()) {
        previousActiveWindowId_ = trimmed;
        targetWindowProvided_ = true;
    }
    showPopup();
}

void PopupWindow::showPopup() {
    const bool hadExplicitTargetWindow = targetWindowProvided_;
    if (targetWindowProvided_) {
        targetWindowProvided_ = false;
    } else {
        previousActiveWindowId_ = PasteController::activeWindowId();
    }
    previousPointerGlobalPos_ = QCursor::pos();
    pasteInProgress_ = false;
    RuntimeLog::write(QStringLiteral("PopupWindow"),
                      QStringLiteral("show-popup targetWindowProvided=%1 previousActiveWindowId=%2 pointer=%3,%4")
                          .arg(hadExplicitTargetWindow ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(previousActiveWindowId_)
                          .arg(previousPointerGlobalPos_.x())
                          .arg(previousPointerGlobalPos_.y()));

    if (search_) {
        search_->clear();
    }
    updateChromeText();
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
    RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("show-popup completed itemCount=%1").arg(list_ ? list_->count() : 0));
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
    const auto items = store_->recentItems(query, qMax(80, AppSettings::historyLimit()));
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
        row->setSizeHint(QSize(qMax(320, list_->viewport()->width() - 8), 84));
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
    RuntimeLog::write(QStringLiteral("PopupWindow"),
                      QStringLiteral("refresh-items query=%1 searchIsActive=%2 visibleItems=%3 hasAnyItems=%4 selectedRow=%5")
                          .arg(query)
                          .arg(searchIsActive ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(items.size())
                          .arg(hasAnyItems ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(list_->currentRow()));
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
    visual->setFixedSize(QSize(48, 48));
    visual->setAttribute(Qt::WA_TransparentForMouseEvents);
    visual->setText(iconLabelForItem(item));
    layout->addWidget(visual);

    auto *textLayout = new QVBoxLayout();
    textLayout->setSpacing(4);

    auto *topLine = new QHBoxLayout();
    topLine->setSpacing(6);
    auto *meta = new QLabel(typeTitle(item) + QStringLiteral(" | ") + relativeTime(item.updatedAt), card);
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
    preview->setMaximumHeight(42);
    preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    textLayout->addWidget(preview);
    layout->addLayout(textLayout, 1);

    auto *buttons = new QVBoxLayout();
    buttons->setSpacing(3);

    auto *pin = new QPushButton(item.pinned ? QStringLiteral("UNPIN") : QStringLiteral("PIN"), card);
    pin->setObjectName(QStringLiteral("pinButton"));
    pin->setFixedSize(52, 26);
    pin->setToolTip(item.pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin"));
    pin->setCursor(Qt::PointingHandCursor);

    auto *remove = new QPushButton(QStringLiteral("DEL"), card);
    remove->setObjectName(QStringLiteral("deleteButton"));
    remove->setFixedSize(52, 26);
    remove->setToolTip(QStringLiteral("Delete"));
    remove->setCursor(Qt::PointingHandCursor);

    buttons->addWidget(pin);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(pin, &QPushButton::clicked, this, [this, id = item.id]() {
        if (store_) {
            RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("pin-button-clicked id=%1").arg(id));
            store_->togglePinned(id);
        }
    });
    connect(remove, &QPushButton::clicked, this, [this, id = item.id]() {
        if (store_) {
            RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("delete-button-clicked id=%1").arg(id));
            store_->deleteItem(id);
        }
    });

    return card;
}

QString PopupWindow::typeTitle(const ClipItem &item) const {
    return item.typeLabel();
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
            "<div style='font-size:15px; color:#ffffff;'>No results found</div>"
            "<div style='margin-top:6px;'>Try a different word or clear the search box.</div>");
    }

    return QStringLiteral(
        "<div style='font-size:15px; color:#ffffff;'>Your clipboard history is empty</div>"
        "<div style='margin-top:6px;'>Copy text in any app, then press <b>%1</b>.</div>")
        .arg(AppIntegration::configuredShortcutDisplay());
}

void PopupWindow::activateListItem(QListWidgetItem *item) {
    if (!item) {
        return;
    }
    activateById(item->data(Qt::UserRole).toInt(), ActivationSource::Keyboard);
}

void PopupWindow::activateCurrentItem() {
    activateListItem(list_->currentItem());
}

void PopupWindow::activateById(int id, ActivationSource source) {
    const auto sourceName = [source]() {
        switch (source) {
        case ActivationSource::Keyboard:
            return QStringLiteral("keyboard");
        case ActivationSource::Mouse:
            return QStringLiteral("mouse");
        case ActivationSource::Menu:
            return QStringLiteral("menu");
        }

        return QStringLiteral("unknown");
    }();

    RuntimeLog::write(QStringLiteral("PopupWindow"),
                      QStringLiteral("activate-by-id begin id=%1 source=%2 pasteInProgress=%3")
                          .arg(id)
                          .arg(sourceName)
                          .arg(pasteInProgress_ ? QStringLiteral("true") : QStringLiteral("false")));
    if (!store_ || pasteInProgress_) {
        return;
    }

    const auto item = store_->itemById(id);
    if (!item.has_value()) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("activate-by-id missing-item id=%1").arg(id));
        return;
    }

    if (!store_->copyToClipboard(*item)) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("activate-by-id copy-to-clipboard-failed id=%1").arg(id));
        return;
    }

    if (!PasteController::canAutoPaste()) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("activate-by-id copied-only id=%1 source=%2").arg(id).arg(sourceName));
        hide();
        emit notificationRequested(QStringLiteral("Clipboard History"),
                                   QStringLiteral("Copied selected item. Press Ctrl+V in the target app."),
                                   false);
        return;
    }

    pasteInProgress_ = true;
    statusLabel_->setText(QStringLiteral("Pasting..."));
    hide();

    const QString targetWindowId = previousActiveWindowId_;
    const QPoint targetPoint = previousPointerGlobalPos_;
    const bool strictMouseTarget = source == ActivationSource::Mouse;
    QTimer::singleShot(kPasteDelayMs, this, [this, targetWindowId, targetPoint, strictMouseTarget, source]() {
        const QString sourceName = source == ActivationSource::Keyboard
            ? QStringLiteral("keyboard")
            : (source == ActivationSource::Mouse ? QStringLiteral("mouse") : QStringLiteral("menu"));
        const bool pasted = PasteController::tryPasteToWindow(targetWindowId, targetPoint, strictMouseTarget);
        pasteInProgress_ = false;
        RuntimeLog::write(QStringLiteral("PopupWindow"),
                          QStringLiteral("activate-by-id paste-finished source=%1 pasted=%2 targetWindowId=%3 point=%4,%5 strictMouseTarget=%6")
                              .arg(sourceName)
                              .arg(pasted ? QStringLiteral("true") : QStringLiteral("false"))
                              .arg(targetWindowId)
                              .arg(targetPoint.x())
                              .arg(targetPoint.y())
                              .arg(strictMouseTarget ? QStringLiteral("true") : QStringLiteral("false")));
        if (!pasted && source != ActivationSource::Mouse) {
            emit notificationRequested(QStringLiteral("Clipboard History"),
                                       QStringLiteral("The item was copied, but auto-paste was unavailable. Press Ctrl+V in the target app."),
                                       true);
        }
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

    if (store_->copyToClipboard(*item)) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("copy-only selected id=%1").arg(id));
        statusLabel_->setText(QStringLiteral("Copied selected item. Paste it with Ctrl+V."));
    }
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
    QAction *pasteAction = menu.addAction(PasteController::canAutoPaste() ? QStringLiteral("Paste") : QStringLiteral("Copy and paste manually"));
    QAction *copyAction = menu.addAction(QStringLiteral("Copy only"));
    menu.addSeparator();
    QAction *pinAction = menu.addAction(item->pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin"));
    QAction *deleteAction = menu.addAction(QStringLiteral("Delete"));

    QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(pos));
    if (!chosen) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("item-menu dismissed id=%1").arg(id));
        return;
    }

    if (chosen == pasteAction) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("item-menu action=paste id=%1").arg(id));
        activateById(id, ActivationSource::Menu);
    } else if (chosen == copyAction) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("item-menu action=copy id=%1").arg(id));
        setSelectedItemToClipboardOnly();
    } else if (chosen == pinAction) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("item-menu action=toggle-pin id=%1").arg(id));
        store_->togglePinned(id);
    } else if (chosen == deleteAction) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("item-menu action=delete id=%1").arg(id));
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
        statusLabel_->setText(AppIntegration::environmentSummary());
        return;
    }

    const int id = list_->currentItem()->data(Qt::UserRole).toInt();
    const auto item = store_->itemById(id);
    if (!item.has_value()) {
        statusLabel_->setText(pluralizedItems(list_->count()));
        return;
    }

    if (PasteController::canAutoPaste()) {
        statusLabel_->setText(QStringLiteral("%1 selected | Enter pastes").arg(typeTitle(*item)));
    } else {
        statusLabel_->setText(QStringLiteral("%1 selected | Enter copies").arg(typeTitle(*item)));
    }
}

void PopupWindow::updateChromeText() {
    if (shortcutHint_) {
        shortcutHint_->setText(AppIntegration::configuredShortcutDisplay());
    }
    if (footerHint_) {
        footerHint_->setText(PasteController::canAutoPaste()
            ? QStringLiteral("Up/Down select | Enter paste | Ctrl+C copy only")
            : QStringLiteral("Up/Down select | Enter copy | Ctrl+V paste in app"));
    }
}

void PopupWindow::clearHistoryWithConfirmation() {
    if (!store_) {
        return;
    }
    RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("clear-history requested confirmBeforeClear=%1").arg(AppSettings::confirmBeforeClear() ? QStringLiteral("true") : QStringLiteral("false")));

    if (AppSettings::confirmBeforeClear()) {
        const QMessageBox::StandardButton result = QMessageBox::question(
            this,
            QStringLiteral("Clear clipboard history"),
            QStringLiteral("Delete all unpinned clipboard items?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (result != QMessageBox::Yes) {
            RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("clear-history cancelled"));
            return;
        }
    }

    RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("clear-history confirmed"));
    store_->clearUnpinned();
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
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("keyboard action=escape-hide"));
        hide();
        event->accept();
        return true;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("keyboard action=activate-current"));
        activateCurrentItem();
        event->accept();
        return true;
    }

    if (ctrl && event->key() == Qt::Key_F) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("keyboard action=focus-search"));
        search_->setFocus(Qt::ShortcutFocusReason);
        search_->selectAll();
        event->accept();
        return true;
    }

    if (ctrl && event->key() == Qt::Key_Comma) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("keyboard action=open-settings"));
        emit settingsRequested();
        event->accept();
        return true;
    }

    if (ctrl && event->key() == Qt::Key_C && !(fromSearch && search_->hasSelectedText())) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("keyboard action=copy-only"));
        setSelectedItemToClipboardOnly();
        event->accept();
        return true;
    }

    if (ctrl && event->key() == Qt::Key_P && list_->currentItem() && store_) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("keyboard action=toggle-pin id=%1").arg(list_->currentItem()->data(Qt::UserRole).toInt()));
        store_->togglePinned(list_->currentItem()->data(Qt::UserRole).toInt());
        event->accept();
        return true;
    }

    if (!fromSearch && ctrl && event->key() == Qt::Key_Delete) {
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("keyboard action=clear-history"));
        clearHistoryWithConfirmation();
        event->accept();
        return true;
    }

    if (!fromSearch && noModifier && event->key() == Qt::Key_Delete && list_->currentItem()) {
        if (store_) {
            RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("keyboard action=delete-item id=%1").arg(list_->currentItem()->data(Qt::UserRole).toInt()));
            store_->deleteItem(list_->currentItem()->data(Qt::UserRole).toInt());
        }
        event->accept();
        return true;
    }

    if (!fromSearch && noModifier && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) {
        const int row = event->key() - Qt::Key_1;
        if (row >= 0 && row < list_->count()) {
            RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("keyboard action=quick-select row=%1").arg(row));
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
            RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("keyboard action=navigate row=%1 key=%2").arg(row).arg(event->key()));
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
    if (watched == headerBar_) {
        return true;
    }

    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget) {
        return false;
    }

    return widget->objectName() == QStringLiteral("title")
        || widget->objectName() == QStringLiteral("subtitle")
        || widget->objectName() == QStringLiteral("appLogo")
        || widget->objectName() == QStringLiteral("keyCap");
}

void PopupWindow::beginPossibleDrag(const QPoint &globalPos) {
    dragCandidate_ = true;
    dragging_ = false;
    dragStartGlobal_ = globalPos;
    dragWindowStart_ = pos();
}

void PopupWindow::watchForDragRelease() {
    if (dragReleaseWatchScheduled_) {
        return;
    }

    dragReleaseWatchScheduled_ = true;
    QTimer::singleShot(16, this, [this]() {
        dragReleaseWatchScheduled_ = false;
        if (!dragCandidate_ && !dragging_) {
            return;
        }

        if (!QGuiApplication::mouseButtons().testFlag(Qt::LeftButton)) {
            finishDrag();
            return;
        }

        watchForDragRelease();
    });
}

bool PopupWindow::tryStartSystemMove() {
    if (!windowHandle()) {
        winId();
    }
    QWindow *window = windowHandle();
    if (!window) {
        return false;
    }

    const bool started = window->startSystemMove();
    if (started) {
        dragging_ = true;
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
        RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("drag-finished position=%1,%2").arg(userPosition_.x()).arg(userPosition_.y()));
    }
    dragCandidate_ = false;
    dragging_ = false;
}

bool PopupWindow::handleDragEvent(QObject *watched, QEvent *event) {
    if (!event) {
        return false;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            return false;
        }

        if (!watchedObjectCanStartDrag(watched)) {
            return false;
        }

        beginPossibleDrag(mouseEvent->globalPosition().toPoint());
        watchForDragRelease();

        if (tryStartSystemMove()) {
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
            if (tryStartSystemMove()) {
                event->accept();
                return true;
            }
        }

        if (updateDrag(mouseEvent->globalPosition().toPoint())) {
            event->accept();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        if (!dragCandidate_) {
            return false;
        }
        const bool wasDragging = dragging_;
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
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (watchedListViewport && isPlainLeftClick(mouseEvent)) {
            if (QListWidgetItem *row = list_->itemAt(mouseEvent->pos())) {
                const int id = row->data(Qt::UserRole).toInt();
                selectItemById(id);
                activateById(id, ActivationSource::Mouse);
                return true;
            }
        }

        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && widget->property("clipId").isValid()) {
            if (isPlainLeftClick(mouseEvent)) {
                const int id = widget->property("clipId").toInt();
                selectItemById(id);
                activateById(id, ActivationSource::Mouse);
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

void PopupWindow::hideEvent(QHideEvent *event) {
    finishDrag();
    RuntimeLog::write(QStringLiteral("PopupWindow"), QStringLiteral("hidden"));
    QWidget::hideEvent(event);
}
