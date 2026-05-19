/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * Visual approach: palette-based colors so the dialog inherits the active
 * system theme (Modern / Fusion / Fusion Dark), mirroring the Kaillera
 * launcher in Source/RMG/UserInterface/Dialog/Kaillera/KailleraNetplayDialog.cpp.
 * The QSS in applyStylesheet() is intentionally minimal — primary buttons
 * pick up the same #0078D7 Windows blue used by KailleraPrimaryButton.
 *
 *   ┌────────────────────────────────────────────────────────────┐
 *   │ RMG-K · Rollback Netplay   ● Online   server · 3p · 1r  JD │ ← marquee
 *   ├────────────────────────────┬──────────────┬────────────────┤
 *   │ [Quick Match]  [Create…]   │  Lobby Room  │  Players       │
 *   │ ┌Active Rooms─────────┐    │  ┌────────┐  │ ┌─────────┐    │
 *   │ │ room rows...        │    │  │ chat   │  │ │ rows..  │    │
 *   │ └─────────────────────┘    │  │ stream │  │ └─────────┘    │
 *   │ ┌Ongoing Matches──────┐    │  └────────┘  │                │
 *   │ │ rows...             │    │  [input]     │                │
 *   │ └─────────────────────┘    │              │                │
 *   └────────────────────────────┴──────────────┴────────────────┘
 */
#ifdef NETPLAY

#include "RollbackLobbyDialog.hpp"
#include "LobbyConnectDialog.hpp"
#include "CreateRoomDialog.hpp"

#include <RMG-Core/Callback.hpp>

#include <QApplication>
#include <QPalette>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QTabWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QJsonArray>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QGroupBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>
#include <QShowEvent>
#include <QCloseEvent>
#include <QTimer>
#include <QFont>
#include <QUrl>
#include <cstdio>

using namespace UserInterface::Dialog;

// ──────────────────────────────────────────────────────────────────────
// Standardized sizes — keep all paddings/heights/font deltas going through
// these so the dialog visually balances and any future tweak only happens
// in one place.
// ──────────────────────────────────────────────────────────────────────
namespace
{
    const char* const CHANNEL_LOBBY = "lobby";
    const char* const CHANNEL_ROOM  = "room";

    constexpr int MARGIN_OUTER       = 8;   // dialog/column outside padding
    constexpr int MARGIN_GROUP       = 10;  // QGroupBox content inset
    constexpr int SPACING_DEFAULT    = 8;
    constexpr int SPACING_TIGHT      = 4;

    constexpr int MARQUEE_HEIGHT     = 44;
    constexpr int BUTTON_MIN_HEIGHT  = 28;
    constexpr int HERO_BUTTON_HEIGHT = 36;
    constexpr int SEAT_TILE_HEIGHT   = 84;
    constexpr int STATUS_DOT_PX      = 10;

    // Theme-aware status indicator colors. Material 500 on dark backgrounds
    // (readable against deep greys), Material 800 on light (readable against
    // white). Detection mirrors Kaillera's approach at
    // KailleraNetplayDialog.cpp:1303.
    bool isDarkTheme()
    {
        return QApplication::palette().window().color().value() < 128;
    }

    struct StatusColors { const char* ok; const char* wait; const char* fail; const char* idle; };
    StatusColors statusColors()
    {
        return isDarkTheme()
            ? StatusColors{ "#4CAF50", "#FFCA28", "#EF5350", "#BDBDBD" }
            : StatusColors{ "#2E7D32", "#F9A825", "#C62828", "#9E9E9E" };
    }

    QString humanState(LobbyClient::ConnectionState s)
    {
        switch (s)
        {
            case LobbyClient::ConnectionState::Disconnected:   return "Offline";
            case LobbyClient::ConnectionState::Connecting:     return "Connecting";
            case LobbyClient::ConnectionState::Authenticating: return "Authenticating";
            case LobbyClient::ConnectionState::Connected:      return "Online";
            case LobbyClient::ConnectionState::Failed:         return "Connection failed";
        }
        return "Unknown";
    }

    QString statusColor(LobbyClient::ConnectionState s)
    {
        const auto c = statusColors();
        switch (s)
        {
            case LobbyClient::ConnectionState::Connected:      return c.ok;
            case LobbyClient::ConnectionState::Connecting:
            case LobbyClient::ConnectionState::Authenticating: return c.wait;
            case LobbyClient::ConnectionState::Failed:         return c.fail;
            default:                                           return c.idle;
        }
    }

    // Apply a +N point delta to the widget's font. Used to bump section
    // titles / hero text consistently without hand-rolled QSS font sizes.
    void bumpFont(QWidget* w, int pointDelta, bool bold = false)
    {
        if (!w) return;
        QFont f = w->font();
        f.setPointSize(f.pointSize() + pointDelta);
        if (bold) f.setBold(true);
        w->setFont(f);
    }
} // namespace

// ──────────────────────────────────────────────────────────────────────
//  Construction
// ──────────────────────────────────────────────────────────────────────

RollbackLobbyDialog::RollbackLobbyDialog(QWidget* parent)
    // Pass Qt::Window flags directly to the base — calling setWindowFlags
    // after construction doesn't always reliably remove the default Qt::Dialog
    // behavior on Windows. We want this to be a normal top-level window:
    //   • appears in the taskbar with its own entry (groupable under RMG-K)
    //   • can be minimized independently of the main window
    //   • doesn't float on top of the emulator render
    : QDialog(parent,
              Qt::Window
              | Qt::WindowTitleHint
              | Qt::WindowSystemMenuHint
              | Qt::WindowMinMaxButtonsHint
              | Qt::WindowCloseButtonHint)
{
    setWindowTitle("RMG-K Lobby");
    setWindowModality(Qt::NonModal);
    resize(1180, 720);
    setObjectName("RollbackLobbyDialog");

    m_client = new LobbyClient(this);

    buildUi();
    applyStylesheet();

    connect(m_client, &LobbyClient::stateChanged,         this, &RollbackLobbyDialog::onClientStateChanged);
    connect(m_client, &LobbyClient::helloFailed,          this, &RollbackLobbyDialog::onHelloFailed);
    connect(m_client, &LobbyClient::connectError,         this, &RollbackLobbyDialog::onConnectError);
    connect(m_client, &LobbyClient::presenceFull,         this, &RollbackLobbyDialog::onPresenceFull);
    connect(m_client, &LobbyClient::userAdded,            this, &RollbackLobbyDialog::onUserAdded);
    connect(m_client, &LobbyClient::userRemoved,          this, &RollbackLobbyDialog::onUserRemoved);
    connect(m_client, &LobbyClient::userUpdated,          this, &RollbackLobbyDialog::onUserUpdated);
    connect(m_client, &LobbyClient::roomListChanged,      this, &RollbackLobbyDialog::onRoomListChanged);
    connect(m_client, &LobbyClient::roomCreated,          this, &RollbackLobbyDialog::onRoomCreated);
    connect(m_client, &LobbyClient::roomCreateFailed,     this, &RollbackLobbyDialog::onRoomCreateFailed);
    connect(m_client, &LobbyClient::roomJoinOk,           this, &RollbackLobbyDialog::onRoomJoinOk);
    connect(m_client, &LobbyClient::roomJoinFailed,       this, &RollbackLobbyDialog::onRoomJoinFailed);
    connect(m_client, &LobbyClient::roomLeft,             this, &RollbackLobbyDialog::onRoomLeft);
    connect(m_client, &LobbyClient::roomStateChanged,     this, &RollbackLobbyDialog::onRoomStateChanged);
    connect(m_client, &LobbyClient::quickMatchStatus,     this, &RollbackLobbyDialog::onQuickMatchStatusChanged);
    connect(m_client, &LobbyClient::chatMessageReceived,  this, &RollbackLobbyDialog::onChatMessageReceived);
    connect(m_client, &LobbyClient::matchBegin,           this, &RollbackLobbyDialog::onMatchBegin);
    connect(m_client, &LobbyClient::matchPeerLeft,        this, &RollbackLobbyDialog::onMatchPeerLeft);
}

RollbackLobbyDialog::~RollbackLobbyDialog()
{
    if (m_client)
        m_client->disconnectFromServer();
}

// ──────────────────────────────────────────────────────────────────────
//  UI construction
// ──────────────────────────────────────────────────────────────────────

void RollbackLobbyDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildMarquee());

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(1);
    m_splitter->setChildrenCollapsible(false);

    auto* leftCol = new QWidget(this);
    auto* leftLay = new QVBoxLayout(leftCol);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);

    m_roomsStack = new QStackedWidget(this);
    m_roomsStack->addWidget(buildBrowseView());   // index 0
    m_roomsStack->addWidget(buildInRoomView());   // index 1
    leftLay->addWidget(m_roomsStack, 1);
    m_splitter->addWidget(leftCol);

    m_splitter->addWidget(buildChatColumn());
    m_splitter->addWidget(buildPlayersColumn());

    m_splitter->setStretchFactor(0, 5);
    m_splitter->setStretchFactor(1, 3);
    m_splitter->setStretchFactor(2, 2);
    m_splitter->setSizes({600, 360, 280});

    root->addWidget(m_splitter, 1);
}

// ── Marquee ──────────────────────────────────────────────────────────

QWidget* RollbackLobbyDialog::buildMarquee()
{
    m_marquee = new QFrame(this);
    m_marquee->setObjectName("LobbyMarquee");
    m_marquee->setFixedHeight(MARQUEE_HEIGHT);

    auto* h = new QHBoxLayout(m_marquee);
    h->setContentsMargins(MARGIN_OUTER * 2, 0, MARGIN_OUTER * 2, 0);
    h->setSpacing(SPACING_DEFAULT * 2);

    m_brandLabel = new QLabel("RMG-K · Rollback Netplay", this);
    m_brandLabel->setObjectName("LobbyBrand");
    bumpFont(m_brandLabel, 1, /*bold=*/true);
    h->addWidget(m_brandLabel);

    h->addSpacing(SPACING_DEFAULT);

    m_statusLed = new QLabel(this);
    m_statusLed->setFixedSize(STATUS_DOT_PX, STATUS_DOT_PX);
    m_statusLed->setStyleSheet(
        QString("background-color: %1; border-radius: %2px;")
            .arg(statusColors().idle).arg(STATUS_DOT_PX / 2));
    h->addWidget(m_statusLed);

    m_statusText = new QLabel("Offline", this);
    h->addWidget(m_statusText);

    h->addSpacing(SPACING_DEFAULT * 2);

    m_serverMeta = new QLabel("Not connected", this);
    // PlaceholderText is the right role for "secondary body text" — Mid is
    // a border-tone and disappears on Fusion Dark. PlaceholderText derives
    // from palette(text) with reduced alpha, so it reads on every theme.
    m_serverMeta->setForegroundRole(QPalette::PlaceholderText);
    h->addWidget(m_serverMeta);

    h->addStretch(1);

    m_userLabel = new QLabel("User: —", this);
    m_userLabel->setObjectName("UserLabel");
    h->addWidget(m_userLabel);

    return m_marquee;
}

// ── Browse view ──────────────────────────────────────────────────────

QWidget* RollbackLobbyDialog::buildBrowseView()
{
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(MARGIN_OUTER, MARGIN_OUTER, MARGIN_OUTER, MARGIN_OUTER);
    lay->setSpacing(SPACING_DEFAULT);

    // ── "You're in a room" banner — shown only when m_currentRoomId != 0
    //    so the user can pop back into their seat after browsing other rooms. ──
    m_inRoomBanner = new QFrame(this);
    m_inRoomBanner->setObjectName("InRoomBanner");
    auto* bannerLay = new QHBoxLayout(m_inRoomBanner);
    bannerLay->setContentsMargins(MARGIN_GROUP, SPACING_TIGHT, MARGIN_GROUP, SPACING_TIGHT);
    bannerLay->setSpacing(SPACING_DEFAULT);

    auto* bannerIcon = new QLabel(QStringLiteral("★"), this);
    bumpFont(bannerIcon, 2, /*bold=*/true);
    bannerLay->addWidget(bannerIcon);

    m_bannerText = new QLabel("You're in a room", this);
    bumpFont(m_bannerText, 0, /*bold=*/true);
    bannerLay->addWidget(m_bannerText, 1);

    m_bannerReturn = new QPushButton(QStringLiteral("Return to Room  →"), this);
    m_bannerReturn->setObjectName("BannerReturnBtn");
    m_bannerReturn->setMinimumHeight(BUTTON_MIN_HEIGHT);
    m_bannerReturn->setCursor(Qt::PointingHandCursor);
    m_bannerReturn->setAutoDefault(false);
    m_bannerReturn->setDefault(false);
    connect(m_bannerReturn, &QPushButton::clicked, this, &RollbackLobbyDialog::switchToInRoomView);
    bannerLay->addWidget(m_bannerReturn);

    m_inRoomBanner->setVisible(false);
    lay->addWidget(m_inRoomBanner);

    // ── Hero row: Quick Match (primary blue) + Create Room (secondary) ──
    auto* heroRow = new QHBoxLayout;
    heroRow->setSpacing(SPACING_DEFAULT);

    m_quickMatchBtn = new QPushButton("⚡  Quick Match", this);
    m_quickMatchBtn->setObjectName("QuickMatchBtn");
    m_quickMatchBtn->setEnabled(false);
    m_quickMatchBtn->setAutoDefault(false);
    m_quickMatchBtn->setDefault(false);
    m_quickMatchBtn->setMinimumHeight(HERO_BUTTON_HEIGHT);
    m_quickMatchBtn->setCursor(Qt::PointingHandCursor);
    m_quickMatchBtn->setToolTip(
        "Auto-match with another player searching right now.\n"
        "Defaults to delay 2 / prediction 7.");

    m_createRoomBtn = new QPushButton("Create Room…", this);
    m_createRoomBtn->setObjectName("CreateRoomBtn");
    m_createRoomBtn->setAutoDefault(false);
    m_createRoomBtn->setDefault(false);
    m_createRoomBtn->setMinimumHeight(HERO_BUTTON_HEIGHT);
    m_createRoomBtn->setCursor(Qt::PointingHandCursor);

    connect(m_quickMatchBtn, &QPushButton::clicked, this, &RollbackLobbyDialog::onQuickMatchClicked);
    connect(m_createRoomBtn, &QPushButton::clicked, this, &RollbackLobbyDialog::onCreateRoomClicked);

    heroRow->addWidget(m_quickMatchBtn, 1);
    heroRow->addWidget(m_createRoomBtn, 0);
    lay->addLayout(heroRow);

    // ── Active Rooms ──
    auto* roomsBox = new QGroupBox("Active Rooms", this);
    auto* roomsLay = new QVBoxLayout(roomsBox);
    roomsLay->setContentsMargins(MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP);

    m_roomsTree = new QTreeWidget(this);
    m_roomsTree->setObjectName("RoomsTree");
    m_roomsTree->setHeaderLabels({ "Name", "Host", "ROM", "Seats", "State" });
    m_roomsTree->setRootIsDecorated(false);
    m_roomsTree->setSortingEnabled(true);
    m_roomsTree->setAlternatingRowColors(true);
    m_roomsTree->setFrameShape(QFrame::NoFrame);
    m_roomsTree->setUniformRowHeights(true);
    m_roomsTree->header()->setStretchLastSection(false);
    m_roomsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_roomsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_roomsTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_roomsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_roomsTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    connect(m_roomsTree, &QTreeWidget::itemDoubleClicked,
            this, &RollbackLobbyDialog::onRoomDoubleClicked);
    roomsLay->addWidget(m_roomsTree);
    lay->addWidget(roomsBox, 1);

    // ── Ongoing Matches ──
    auto* matchesBox = new QGroupBox("Ongoing Matches", this);
    auto* matchesLay = new QVBoxLayout(matchesBox);
    matchesLay->setContentsMargins(MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP);

    m_matchesTree = new QTreeWidget(this);
    m_matchesTree->setObjectName("MatchesTree");
    m_matchesTree->setHeaderLabels({ "Players", "Duration", "Settings" });
    m_matchesTree->setRootIsDecorated(false);
    m_matchesTree->setAlternatingRowColors(true);
    m_matchesTree->setFrameShape(QFrame::NoFrame);
    m_matchesTree->setFixedHeight(120);
    m_matchesTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_matchesTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_matchesTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    matchesLay->addWidget(m_matchesTree);
    lay->addWidget(matchesBox);

    return page;
}

// ── In-room view ─────────────────────────────────────────────────────

QWidget* RollbackLobbyDialog::buildInRoomView()
{
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(MARGIN_OUTER, MARGIN_OUTER, MARGIN_OUTER, MARGIN_OUTER);
    lay->setSpacing(SPACING_DEFAULT);

    // ── Header card (room info) ──
    auto* headerBox = new QGroupBox("Room", this);
    auto* headerLay = new QVBoxLayout(headerBox);
    headerLay->setContentsMargins(MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP);
    headerLay->setSpacing(SPACING_TIGHT);

    // Top row: breadcrumb + state label
    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(SPACING_DEFAULT);
    auto* breadcrumb = new QPushButton("←  Browse Rooms", this);
    breadcrumb->setFlat(true);
    breadcrumb->setCursor(Qt::PointingHandCursor);
    breadcrumb->setAutoDefault(false);
    breadcrumb->setToolTip("Switch to the room list. You stay in your seat — use Leave Room to actually exit.");
    connect(breadcrumb, &QPushButton::clicked, this, &RollbackLobbyDialog::switchToRoomsView);
    topRow->addWidget(breadcrumb);
    topRow->addStretch(1);

    m_roomStateLabel = new QLabel("Waiting", this);
    bumpFont(m_roomStateLabel, 0, /*bold=*/true);
    topRow->addWidget(m_roomStateLabel);
    headerLay->addLayout(topRow);

    // ROM title (large)
    m_roomTitle = new QLabel("—", this);
    bumpFont(m_roomTitle, 4, /*bold=*/true);
    headerLay->addWidget(m_roomTitle);

    // Subtitle (host / max players) — kept at default WindowText since this
    // is important info, not a caption. The bold ROM title above gives the
    // visual hierarchy without needing to dim this line.
    m_roomSubtitle = new QLabel("—", this);
    headerLay->addWidget(m_roomSubtitle);

    // Meta line (Delay / Prediction / Seats / Region) — plain text, single label
    m_roomMetaLabel = new QLabel("—", this);
    m_roomMetaLabel->setTextFormat(Qt::RichText);
    m_roomMetaLabel->setContentsMargins(0, SPACING_TIGHT, 0, 0);
    headerLay->addWidget(m_roomMetaLabel);

    lay->addWidget(headerBox);

    // ── Seats ──
    auto* seatsBox = new QGroupBox("Seats", this);
    auto* seatsLay = new QHBoxLayout(seatsBox);
    seatsLay->setContentsMargins(MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP);
    seatsLay->setSpacing(SPACING_DEFAULT);
    for (int i = 0; i < 4; ++i)
    {
        buildSeatTile(m_seats[i], i + 1, seatsBox);
        seatsLay->addWidget(m_seats[i].box, 1);
        renderSeatEmpty(m_seats[i]);
    }
    lay->addWidget(seatsBox);

    lay->addStretch(1);

    // ── Action bar ──
    auto* actionRow = new QHBoxLayout;
    actionRow->setSpacing(SPACING_DEFAULT);

    m_startBtn = new QPushButton("Start Game", this);
    m_startBtn->setObjectName("StartGameBtn");
    m_startBtn->setEnabled(false);
    m_startBtn->setMinimumHeight(HERO_BUTTON_HEIGHT);
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setAutoDefault(false);
    m_startBtn->setDefault(false);
    connect(m_startBtn, &QPushButton::clicked, this, &RollbackLobbyDialog::onStartGameClicked);

    m_dropBtn = new QPushButton("Drop Game", this);
    m_dropBtn->setObjectName("DropBtn");
    m_dropBtn->setEnabled(false);
    m_dropBtn->setMinimumHeight(BUTTON_MIN_HEIGHT);
    m_dropBtn->setAutoDefault(false);
    m_dropBtn->setDefault(false);
    m_dropBtn->setCursor(Qt::PointingHandCursor);
    m_dropBtn->setToolTip("Stop playing the current match. If you're the host, the match ends for everyone.");
    connect(m_dropBtn, &QPushButton::clicked, this, &RollbackLobbyDialog::onDropGameClicked);

    m_leaveBtn = new QPushButton("Leave Room", this);
    m_leaveBtn->setObjectName("LeaveBtn");
    m_leaveBtn->setMinimumHeight(BUTTON_MIN_HEIGHT);
    m_leaveBtn->setAutoDefault(false);
    m_leaveBtn->setDefault(false);
    m_leaveBtn->setCursor(Qt::PointingHandCursor);
    m_leaveBtn->setToolTip("Leave the room and return to the lobby.");
    connect(m_leaveBtn, &QPushButton::clicked, this, &RollbackLobbyDialog::onLeaveRoomClicked);

    actionRow->addWidget(m_startBtn, 1);
    actionRow->addStretch(0);
    actionRow->addWidget(m_dropBtn);
    actionRow->addWidget(m_leaveBtn);
    lay->addLayout(actionRow);

    return page;
}

// ── Seat tile ────────────────────────────────────────────────────────

void RollbackLobbyDialog::buildSeatTile(SeatTile& t, int slotIdx, QWidget* parent)
{
    t.box = new QGroupBox(QString("P%1").arg(slotIdx), parent);
    t.box->setMinimumHeight(SEAT_TILE_HEIGHT);
    t.box->setAlignment(Qt::AlignHCenter);

    auto* lay = new QVBoxLayout(t.box);
    lay->setContentsMargins(MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP);
    lay->setSpacing(SPACING_TIGHT);

    t.nameLabel = new QLabel("—", t.box);
    t.nameLabel->setAlignment(Qt::AlignHCenter);
    t.nameLabel->setTextFormat(Qt::RichText);
    bumpFont(t.nameLabel, 1, /*bold=*/true);
    lay->addWidget(t.nameLabel);

    t.metaLabel = new QLabel(QString(), t.box);
    t.metaLabel->setAlignment(Qt::AlignHCenter);
    // PlaceholderText derives from text + reduced alpha — visible on every
    // theme. Mid was rendering near-invisible on Fusion Dark.
    t.metaLabel->setForegroundRole(QPalette::PlaceholderText);
    lay->addWidget(t.metaLabel);

    lay->addStretch(1);
}

void RollbackLobbyDialog::renderSeatEmpty(SeatTile& t)
{
    t.isHost = false;
    if (t.nameLabel)
    {
        t.nameLabel->setText("<i>Open Seat</i>");
        // PlaceholderText is a derived palette role that adapts to every
        // theme — it's "muted body text" by design. Mid sat on the same
        // tone as the QGroupBox border on Fusion Dark and disappeared.
        t.nameLabel->setForegroundRole(QPalette::PlaceholderText);
    }
    if (t.metaLabel) t.metaLabel->setText(QString());
}

void RollbackLobbyDialog::renderSeatFilled(SeatTile& t, const QString& username, bool isHost,
                                           bool isSelf, int pingMs)
{
    t.isHost = isHost;
    if (t.nameLabel)
    {
        QString name = username;
        if (isSelf) name += " (you)";
        t.nameLabel->setText(name);
        // Reset to the default foreground so the previous "Open Seat" Mid
        // colour doesn't carry over.
        t.nameLabel->setForegroundRole(QPalette::WindowText);
    }
    if (t.metaLabel)
    {
        QStringList parts;
        if (isHost) parts << "host";
        else        parts << "ready";
        if (pingMs > 0) parts << QString("%1ms").arg(pingMs);
        t.metaLabel->setText(parts.join(" · "));
    }
}

// ── Chat column ──────────────────────────────────────────────────────

QWidget* RollbackLobbyDialog::buildChatColumn()
{
    auto* col = new QWidget(this);
    auto* lay = new QVBoxLayout(col);
    lay->setContentsMargins(0, MARGIN_OUTER, 0, MARGIN_OUTER);
    lay->setSpacing(SPACING_DEFAULT);

    auto* chatBox = new QGroupBox("Chat", this);
    auto* chatLay = new QVBoxLayout(chatBox);
    chatLay->setContentsMargins(MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP);
    chatLay->setSpacing(SPACING_DEFAULT);

    m_chatTabs = new QTabWidget(this);
    m_chatTabs->setDocumentMode(true);

    m_chatViewLobby = new QTextEdit(this);
    m_chatViewLobby->setReadOnly(true);
    m_chatViewLobby->setLineWrapMode(QTextEdit::WidgetWidth);
    m_chatTabs->addTab(m_chatViewLobby, "Lobby");

    chatLay->addWidget(m_chatTabs, 1);

    m_chatInput = new QLineEdit(this);
    m_chatInput->setPlaceholderText("Type a message and press Enter…");
    m_chatInput->setEnabled(false);
    m_chatInput->setMinimumHeight(BUTTON_MIN_HEIGHT);
    connect(m_chatInput, &QLineEdit::returnPressed, this, &RollbackLobbyDialog::onChatSendClicked);
    chatLay->addWidget(m_chatInput);

    lay->addWidget(chatBox, 1);
    return col;
}

// ── Players column ───────────────────────────────────────────────────

QWidget* RollbackLobbyDialog::buildPlayersColumn()
{
    auto* col = new QWidget(this);
    auto* lay = new QVBoxLayout(col);
    lay->setContentsMargins(0, MARGIN_OUTER, MARGIN_OUTER, MARGIN_OUTER);
    lay->setSpacing(SPACING_DEFAULT);

    auto* playersBox = new QGroupBox("Players", this);
    auto* playersLay = new QVBoxLayout(playersBox);
    playersLay->setContentsMargins(MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP, MARGIN_GROUP);

    m_playersTree = new QTreeWidget(this);
    m_playersTree->setObjectName("PlayersTree");
    m_playersTree->setHeaderLabels({ "Player", "State", "Ping" });
    m_playersTree->setRootIsDecorated(false);
    m_playersTree->setSortingEnabled(true);
    m_playersTree->setAlternatingRowColors(true);
    m_playersTree->setFrameShape(QFrame::NoFrame);
    m_playersTree->setUniformRowHeights(true);
    m_playersTree->header()->setStretchLastSection(false);
    m_playersTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_playersTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_playersTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    playersLay->addWidget(m_playersTree);

    lay->addWidget(playersBox, 1);
    return col;
}

// ── QSS (minimal, palette-based) ─────────────────────────────────────

void RollbackLobbyDialog::applyStylesheet()
{
    // Adapts to the system theme via palette() functions. Only the primary
    // CTA buttons (Quick Match / Start Game) are explicitly colored, matching
    // the Kaillera launcher's KailleraPrimaryButton (#0078D7 Windows blue).
    const QPalette pal = QApplication::palette();
    const bool dark = pal.window().color().value() < 128;
    const QString border = dark ? QStringLiteral("rgba(255,255,255,0.18)")
                                : QStringLiteral("palette(mid)");

    // Banner accent — soft blue tint that reads on both light and dark themes.
    const QString bannerBg = dark
        ? QStringLiteral("rgba(0, 120, 215, 0.18)")
        : QStringLiteral("rgba(0, 120, 215, 0.10)");
    const QString bannerBorder = QStringLiteral("rgba(0, 120, 215, 0.45)");

    const QString qss = QString(
        // Marquee — thin divider, no fancy gradient.
        "QFrame#LobbyMarquee {"
        "  background-color: palette(window);"
        "  border-bottom: 1px solid %1;"
        "}"

        // QGroupBox sections — match Kaillera launcher pane look.
        "QGroupBox {"
        "  border: 1px solid %1;"
        "  border-radius: 6px;"
        "  margin-top: 10px;"
        "  padding-top: 6px;"
        "  font-weight: 600;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 10px;"
        "  padding: 0 6px;"
        "  color: palette(text);"
        "}"

        // In-room banner — visible in browse view when the user is seated.
        "QFrame#InRoomBanner {"
        "  background-color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 6px;"
        "}"
        "QFrame#InRoomBanner QLabel {"
        "  color: palette(text);"
        "}"

        // Primary CTAs — same blue as KailleraPrimaryButton.
        "QPushButton#QuickMatchBtn, QPushButton#StartGameBtn,"
        " QPushButton#BannerReturnBtn {"
        "  border: 1px solid #0066b4;"
        "  border-radius: 4px;"
        "  padding: 4px 16px;"
        "  font-weight: 700;"
        "  color: white;"
        "  background-color: #0078D7;"
        "}"
        "QPushButton#QuickMatchBtn:hover, QPushButton#StartGameBtn:hover,"
        " QPushButton#BannerReturnBtn:hover {"
        "  background-color: #1584dd;"
        "}"
        "QPushButton#QuickMatchBtn:pressed, QPushButton#StartGameBtn:pressed,"
        " QPushButton#BannerReturnBtn:pressed {"
        "  background-color: #0063b1;"
        "}"
        "QPushButton#QuickMatchBtn:disabled, QPushButton#StartGameBtn:disabled {"
        "  background-color: palette(button);"
        "  color: palette(mid);"
        "  border-color: %1;"
        "}"
    ).arg(border, bannerBg, bannerBorder);

    setStyleSheet(qss);
}

// ──────────────────────────────────────────────────────────────────────
//  Show / close
// ──────────────────────────────────────────────────────────────────────

void RollbackLobbyDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (m_client->state() == LobbyClient::ConnectionState::Disconnected)
    {
        QTimer::singleShot(0, this, &RollbackLobbyDialog::promptConnect);
    }
}

void RollbackLobbyDialog::closeEvent(QCloseEvent* event)
{
    if (m_client)
        m_client->disconnectFromServer();
    QDialog::closeEvent(event);
}

void RollbackLobbyDialog::promptConnect()
{
    LobbyConnectDialog d(this);
    if (d.exec() != QDialog::Accepted)
    {
        close();
        return;
    }

    m_username = d.username();
    m_serverUrl = d.serverUrl();
    if (m_userLabel) m_userLabel->setText(QString("User: %1").arg(m_username));
    updateServerMeta();
    // TODO: collect ROM hashes from RomBrowser when integrating.
    m_client->connectToServer(d.serverUrl(), m_username, {}, QString());
}

// ──────────────────────────────────────────────────────────────────────
//  Connection events
// ──────────────────────────────────────────────────────────────────────

void RollbackLobbyDialog::onClientStateChanged(LobbyClient::ConnectionState s)
{
    updateStatusIndicator(s);

    const bool connected = (s == LobbyClient::ConnectionState::Connected);
    m_chatInput->setEnabled(connected);
    if (m_quickMatchBtn)
        m_quickMatchBtn->setEnabled(connected && m_currentRoomId == 0);
    if (m_createRoomBtn)
        m_createRoomBtn->setEnabled(connected && m_currentRoomId == 0);

    if (s == LobbyClient::ConnectionState::Disconnected)
    {
        m_playersTree->clear();
        m_roomsTree->clear();
        m_userItems.clear();
        m_roomItems.clear();
        m_currentRoomId = 0;
        m_currentRoomState.clear();
        m_currentMatchId = 0;
        m_awaitingEmulationStart = false;
        m_emulationActive = false;
        m_quickMatchActive = false;
        if (m_quickMatchBtn) m_quickMatchBtn->setText("⚡  Quick Match");

        if (m_chatViewRoom)
        {
            const int idx = m_chatTabs->indexOf(m_chatViewRoom);
            if (idx >= 0) m_chatTabs->removeTab(idx);
            m_chatViewRoom->deleteLater();
            m_chatViewRoom = nullptr;
        }
        m_chatTabs->setCurrentWidget(m_chatViewLobby);
        switchToRoomsView();
    }
    updateServerMeta();
    updateInRoomBanner();
}

void RollbackLobbyDialog::onHelloFailed(const QString& reason)
{
    QString human = reason;
    if (reason == "username_taken")    human = "That username is already in use.";
    else if (reason == "invalid_hello") human = "Server rejected the connection handshake.";
    else if (reason == "version_mismatch") human = "Client version is incompatible with this server.";

    QMessageBox::warning(this, "Connection refused", human);
    QTimer::singleShot(0, this, &RollbackLobbyDialog::promptConnect);
}

void RollbackLobbyDialog::onConnectError(const QString& msg)
{
    QMessageBox::warning(this, "Could not reach lobby", msg);
}

void RollbackLobbyDialog::updateStatusIndicator(LobbyClient::ConnectionState s)
{
    if (m_statusLed)
    {
        m_statusLed->setStyleSheet(
            QString("background-color: %1; border-radius: %2px;")
                .arg(statusColor(s)).arg(STATUS_DOT_PX / 2));
    }
    if (m_statusText) m_statusText->setText(humanState(s));
}

void RollbackLobbyDialog::updateInRoomBanner()
{
    if (!m_inRoomBanner) return;
    const bool inRoom = (m_currentRoomId != 0);
    m_inRoomBanner->setVisible(inRoom);
    if (!inRoom) return;

    // Prefer the live room name from the rooms map; fall back to "#id".
    QString name;
    const auto& rooms = m_client->rooms();
    const auto it = rooms.constFind(m_currentRoomId);
    if (it != rooms.constEnd() && !it->name.isEmpty())
        name = it->name;
    else
        name = QString("#%1").arg(m_currentRoomId);

    // Also include seat count when we have it cached from ROOM_STATE.
    QString seats;
    if (it != rooms.constEnd() && it->maxPlayers > 0)
        seats = QString("  ·  %1/%2 seats").arg(it->players).arg(it->maxPlayers);

    if (m_bannerText)
        m_bannerText->setText(QString("You're in: <b>%1</b>%2").arg(name, seats));
}

void RollbackLobbyDialog::updateServerMeta()
{
    if (!m_serverMeta) return;
    if (m_client->state() != LobbyClient::ConnectionState::Connected)
    {
        m_serverMeta->setText("Not connected");
        return;
    }
    const int players = m_client->users().size();
    const int rooms = m_client->rooms().size();
    QUrl u(m_serverUrl);
    QString host = u.isValid() && !u.host().isEmpty() ? u.host() : m_serverUrl;
    if (u.port() > 0) host = QString("%1:%2").arg(host).arg(u.port());
    m_serverMeta->setText(QString("%1  ·  %2 player%3  ·  %4 room%5")
                              .arg(host)
                              .arg(players).arg(players == 1 ? "" : "s")
                              .arg(rooms).arg(rooms == 1 ? "" : "s"));
}

// ──────────────────────────────────────────────────────────────────────
//  Presence
// ──────────────────────────────────────────────────────────────────────

void RollbackLobbyDialog::onPresenceFull()
{
    m_playersTree->clear();
    m_userItems.clear();
    for (auto it = m_client->users().constBegin(); it != m_client->users().constEnd(); ++it)
    {
        auto* row = new QTreeWidgetItem(m_playersTree);
        refreshPlayerRow(row, it.value());
        m_userItems.insert(it.key(), row);
    }
    updateServerMeta();
}

void RollbackLobbyDialog::onUserAdded(quint64 userId)
{
    const auto& users = m_client->users();
    auto it = users.constFind(userId);
    if (it == users.constEnd()) return;

    auto* row = new QTreeWidgetItem(m_playersTree);
    refreshPlayerRow(row, it.value());
    m_userItems.insert(userId, row);
    updateServerMeta();
}

void RollbackLobbyDialog::onUserRemoved(quint64 userId)
{
    auto it = m_userItems.find(userId);
    if (it == m_userItems.end()) return;
    delete it.value();
    m_userItems.erase(it);
    updateServerMeta();
}

void RollbackLobbyDialog::onUserUpdated(quint64 userId)
{
    auto it = m_userItems.find(userId);
    if (it == m_userItems.end()) return;
    const auto& users = m_client->users();
    auto userIt = users.constFind(userId);
    if (userIt == users.constEnd()) return;
    refreshPlayerRow(it.value(), userIt.value());
}

void RollbackLobbyDialog::refreshPlayerRow(QTreeWidgetItem* item, const LobbyClient::LobbyUser& u)
{
    item->setText(0, u.username);
    // Self gets bold; everyone else uses default palette text.
    if (u.id == m_client->selfUserId())
    {
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
    }
    item->setText(1, stateGlyph(u.state));
    item->setText(2, u.pingToServer > 0 ? QString("%1 ms").arg(u.pingToServer) : "—");
    item->setToolTip(0, QString("Region: %1\nServer ping: %2 ms").arg(u.region).arg(u.pingToServer));
    item->setData(0, Qt::UserRole, QVariant::fromValue(u.id));
}

QString RollbackLobbyDialog::stateGlyph(const QString& state) const
{
    if (state == "idle")       return "Online";
    if (state == "browsing")   return "Browsing";
    if (state == "hosting")    return "Hosting";
    if (state == "in_room")    return "In Room";
    if (state == "searching")  return "Searching";
    if (state == "playing")    return "In Game";
    if (state == "spectating") return "Spectating";
    if (state == "away")       return "Away";
    return state;
}

// ──────────────────────────────────────────────────────────────────────
//  Rooms list
// ──────────────────────────────────────────────────────────────────────

void RollbackLobbyDialog::onRoomListChanged()
{
    m_roomsTree->clear();
    m_roomItems.clear();
    for (auto it = m_client->rooms().constBegin(); it != m_client->rooms().constEnd(); ++it)
    {
        auto* row = new QTreeWidgetItem(m_roomsTree);
        refreshRoomRow(row, it.value());
        m_roomItems.insert(it.key(), row);
    }
    updateServerMeta();
    updateInRoomBanner();   // seat counts may have changed in our own room
}

void RollbackLobbyDialog::switchToRoomsView()
{
    if (m_roomsStack) m_roomsStack->setCurrentIndex(0);
}

void RollbackLobbyDialog::switchToInRoomView()
{
    if (m_roomsStack) m_roomsStack->setCurrentIndex(1);
}

void RollbackLobbyDialog::refreshRoomRow(QTreeWidgetItem* item, const LobbyClient::LobbyRoomSummary& r)
{
    const bool mine = (r.id == m_currentRoomId);
    QString nameCell = r.name;
    if (r.hasPassword) nameCell = QStringLiteral("🔒  ") + nameCell;
    if (mine)          nameCell = QStringLiteral("★  ") + nameCell;

    item->setText(0, nameCell);
    item->setText(1, r.hostName);
    item->setText(2, r.romName);
    item->setText(3, QString("%1/%2").arg(r.players).arg(r.maxPlayers));
    item->setText(4, stateGlyph(r.state));
    item->setData(0, Qt::UserRole, QVariant::fromValue(r.id));

    // Bold the user's own room; everything else native.
    QFont f = item->font(0);
    f.setBold(mine);
    item->setFont(0, f);
}

void RollbackLobbyDialog::onCreateRoomClicked()
{
    if (m_client->state() != LobbyClient::ConnectionState::Connected)
        return;
    if (m_currentRoomId != 0)
    {
        QMessageBox::information(this, "Already in a room",
            "Leave your current room before creating a new one.");
        return;
    }
    if (m_createRoomDialog)
    {
        m_createRoomDialog->raise();
        m_createRoomDialog->activateWindow();
        return;
    }
    m_createRoomDialog = new CreateRoomDialog(m_username, m_roms, this);
    connect(m_createRoomDialog, &CreateRoomDialog::createRequested,
            this, &RollbackLobbyDialog::onRoomCreateRequested);
    connect(m_createRoomDialog, &QDialog::finished, this, [this](int) {
        m_createRoomDialog->deleteLater();
        m_createRoomDialog = nullptr;
    });
    m_createRoomDialog->show();
}

void RollbackLobbyDialog::onRoomCreateRequested()
{
    if (!m_createRoomDialog) return;
    m_client->createRoom(
        m_createRoomDialog->name(),
        m_createRoomDialog->romName(),
        m_createRoomDialog->romMd5(),
        m_createRoomDialog->romRegion(),
        m_createRoomDialog->maxPlayers(),
        m_createRoomDialog->delay(),
        m_createRoomDialog->prediction(),
        m_createRoomDialog->password());
}

void RollbackLobbyDialog::onRoomCreated(quint64 roomId)
{
    if (m_createRoomDialog)
        m_createRoomDialog->accept();
    enterRoom(roomId,
        QString("<i>You created room #%1 — waiting for players</i>").arg(roomId));
}

void RollbackLobbyDialog::onRoomJoinOk(quint64 roomId)
{
    QString roomName;
    auto it = m_client->rooms().constFind(roomId);
    if (it != m_client->rooms().constEnd())
        roomName = it.value().name;
    enterRoom(roomId,
        roomName.isEmpty()
            ? QString("<i>You joined room #%1</i>").arg(roomId)
            : QString("<i>You joined &quot;%1&quot;</i>").arg(roomName.toHtmlEscaped()));
}

void RollbackLobbyDialog::onRoomJoinFailed(const QString& reason)
{
    QString human = reason;
    if (reason == "wrong_password")    human = "Wrong password.";
    else if (reason == "full")         human = "Room is full.";
    else if (reason == "already_started") human = "That game has already started.";
    else if (reason == "already_in_room") human = "You're already in a room.";
    else if (reason == "room_not_found")  human = "That room no longer exists.";
    QMessageBox::warning(this, "Couldn't join room", human);
}

void RollbackLobbyDialog::enterRoom(quint64 roomId, const QString& greetingChatLine)
{
    m_currentRoomId = roomId;

    if (!m_chatViewRoom)
    {
        m_chatViewRoom = new QTextEdit(this);
        m_chatViewRoom->setReadOnly(true);
        m_chatViewRoom->setLineWrapMode(QTextEdit::WidgetWidth);
        m_chatTabs->addTab(m_chatViewRoom, "Room");
    }
    m_chatTabs->setCurrentWidget(m_chatViewRoom);
    switchToInRoomView();

    if (m_createRoomBtn) m_createRoomBtn->setEnabled(false);
    if (m_quickMatchBtn) m_quickMatchBtn->setEnabled(false);

    updateInRoomBanner();

    if (!greetingChatLine.isEmpty())
        appendChatLine(CHANNEL_LOBBY, greetingChatLine);
}

void RollbackLobbyDialog::onRoomDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;
    if (m_client->state() != LobbyClient::ConnectionState::Connected) return;
    if (m_currentRoomId != 0)
    {
        QMessageBox::information(this, "Already in a room",
            "Leave your current room before joining another.");
        return;
    }

    const quint64 roomId = item->data(0, Qt::UserRole).toULongLong();
    if (roomId == 0) return;

    auto it = m_client->rooms().constFind(roomId);
    if (it == m_client->rooms().constEnd()) return;
    const auto& summary = it.value();

    if (summary.state == "in_game" || summary.state == "starting")
    {
        QMessageBox::information(this, "Match in progress",
            "That room is already playing — try another or wait for it to finish.");
        return;
    }

    QString password;
    if (summary.hasPassword)
    {
        bool ok = false;
        password = QInputDialog::getText(this, "Password required",
            QString("Enter password for \"%1\":").arg(summary.name),
            QLineEdit::Password, QString(), &ok);
        if (!ok) return;
    }
    m_client->joinRoom(roomId, password);
}

void RollbackLobbyDialog::onRoomCreateFailed(const QString& reason)
{
    QMessageBox::warning(this, "Couldn't create room", reason);
}

void RollbackLobbyDialog::onRoomStateChanged(const QJsonObject& roomState)
{
    const quint64 roomId = static_cast<quint64>(roomState.value("id").toDouble());
    if (roomId == 0 || roomId != m_currentRoomId)
        return;

    const QString name = roomState.value("name").toString();
    const QJsonObject rom = roomState.value("rom").toObject();
    const QString romName = rom.value("name").toString();
    const QString romRegion = rom.value("region").toString();
    const int delay = roomState.value("delay").toInt();
    const int prediction = roomState.value("prediction").toInt();
    const int maxPlayers = roomState.value("maxPlayers").toInt();
    const QString state = roomState.value("state").toString();
    const quint64 hostId = static_cast<quint64>(roomState.value("hostId").toDouble());
    const bool iAmHost = (hostId == m_client->selfUserId());

    m_currentRoomGame       = romName;
    m_currentRoomRegion     = romRegion;
    m_currentRoomDelay      = delay;
    m_currentRoomPrediction = prediction;

    // Auto-stop emulation if the room transitioned out of in_game while we
    // still had a live local match (host dropped it for everyone).
    const QString prevState = m_currentRoomState;
    m_currentRoomState = state;
    if (prevState == "in_game" && state != "in_game" &&
        (m_emulationActive || m_awaitingEmulationStart))
    {
        appendChatSystemLine(CHANNEL_ROOM, "Host ended the match — stopping game.");
        emit closeMatchRequested();
    }

    if (m_dropBtn)
    {
        m_dropBtn->setEnabled(state == "in_game" &&
            (m_emulationActive || m_awaitingEmulationStart));
        m_dropBtn->setToolTip(
            iAmHost
                ? "Stop the match for everyone in the room."
                : "Drop out of the match. Other players keep playing.");
    }
    if (m_leaveBtn) m_leaveBtn->setEnabled(true);

    // ── Header card ──
    QString title = romName.isEmpty() ? name : romName;
    if (!name.isEmpty() && name != romName && !romName.isEmpty())
        title = QString("%1 — %2").arg(name, romName);
    m_roomTitle->setText(title);

    // Resolve host display name: prefer hostName, fall back to users() map,
    // then to our own username if we're the host.
    QString hostName = roomState.value("hostName").toString();
    if (hostName.isEmpty())
    {
        const auto& users = m_client->users();
        const auto it = users.constFind(hostId);
        if (it != users.constEnd()) hostName = it->username;
    }
    if (hostName.isEmpty() && iAmHost) hostName = m_username;
    if (hostName.isEmpty()) hostName = QStringLiteral("—");

    m_roomSubtitle->setText(QString("Room #%1  ·  Hosted by %2  ·  %3 players max")
                                .arg(roomId).arg(hostName).arg(maxPlayers));

    m_roomStateLabel->setText(stateGlyph(state));

    const QJsonArray players = roomState.value("players").toArray();
    QStringList metaParts;
    metaParts << QString("<b>Delay:</b> %1f").arg(delay);
    metaParts << QString("<b>Prediction:</b> %1f").arg(prediction);
    metaParts << QString("<b>Seats:</b> %1/%2").arg(players.size()).arg(maxPlayers);
    if (!romRegion.isEmpty())
        metaParts << QString("<b>Region:</b> %1").arg(romRegion);
    m_roomMetaLabel->setText(metaParts.join("  ·  "));

    // ── Seats ──
    QVector<bool> filled(4, false);
    for (const auto& v : players)
    {
        const auto p = v.toObject();
        const int slot = p.value("slot").toInt();
        if (slot < 1 || slot > 4) continue;
        const QString user = p.value("username").toString();
        const quint64 uid = static_cast<quint64>(p.value("userId").toDouble());
        const bool slotIsHost = (uid == hostId);
        const bool slotIsSelf = (uid == m_client->selfUserId());
        renderSeatFilled(m_seats[slot - 1], user, slotIsHost, slotIsSelf, 0);
        filled[slot - 1] = true;
    }
    for (int i = 0; i < 4; ++i)
    {
        if (i >= maxPlayers) m_seats[i].box->setVisible(false);
        else
        {
            m_seats[i].box->setVisible(true);
            if (!filled[i]) renderSeatEmpty(m_seats[i]);
        }
    }

    // Host can start whenever there are at least 2 seated players — no ready
    // gate. Disabled mid-match (state != waiting) so the host can't start a
    // second match on top of a live one.
    const int seated = players.size();
    const bool canStart = (state == "waiting") && seated >= 2;
    m_startBtn->setEnabled(iAmHost && canStart);
    m_startBtn->setToolTip(
        !iAmHost ? "Only the host can start the game."
                 : (canStart ? "" : (seated < 2 ? "Need at least 2 players to start."
                                                : "Already in a match.")));
}

void RollbackLobbyDialog::onDropGameClicked()
{
    if (m_currentRoomState != "in_game")
        return;

    emit closeMatchRequested();
    if (m_currentMatchId != 0)
        m_client->reportMatchFinished(m_currentMatchId);
    appendChatSystemLine(CHANNEL_ROOM, "You dropped from the game.");
}

void RollbackLobbyDialog::onLeaveRoomClicked()
{
    if (m_emulationActive || m_awaitingEmulationStart)
    {
        emit closeMatchRequested();
        if (m_currentMatchId != 0)
            m_client->reportMatchFinished(m_currentMatchId);
    }
    m_client->leaveRoom();
}

void RollbackLobbyDialog::notifyEmulationStarted()
{
    if (!m_awaitingEmulationStart) return;
    m_awaitingEmulationStart = false;
    m_emulationActive = true;
    if (m_currentMatchId == 0) return;

    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Lobby→notifyEmulationStarted matchId=%llu",
            (unsigned long long)m_currentMatchId);
        CoreAddCallbackMessage(CoreDebugMessageType::Info, buf);
    }
    m_client->reportMatchConnected(m_currentMatchId, 0);
    appendChatSystemLine(CHANNEL_ROOM, "Match started — playing now.");
}

void RollbackLobbyDialog::notifyEmulationFinished()
{
    if (!m_emulationActive && !m_awaitingEmulationStart) return;
    const quint64 matchId = m_currentMatchId;
    m_emulationActive = false;
    m_awaitingEmulationStart = false;

    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Lobby→notifyEmulationFinished matchId=%llu",
            (unsigned long long)matchId);
        CoreAddCallbackMessage(CoreDebugMessageType::Info, buf);
    }
    if (matchId != 0)
        m_client->reportMatchFinished(matchId);
    m_currentMatchId = 0;
    if (m_dropBtn) m_dropBtn->setEnabled(false);

    // Re-open the UDP anchor so the next MATCH_BEGIN can hand us a fresh
    // local port. Without this, localUdpPort() returns 0 (socket is aborted
    // after onMatchBegin's releaseUdpAnchor), GekkoNet binds to a random
    // kernel-picked port for match #2, and peers send to the stale port from
    // match #1 — black screen on every match after the first.
    if (m_client) m_client->reopenUdpAnchor();
}

void RollbackLobbyDialog::onMatchPeerLeft(quint64 matchId, quint64 userId, int slot, const QString& reason)
{
    Q_UNUSED(matchId);
    QString who = QString::number(userId);
    if (m_client)
    {
        const auto& users = m_client->users();
        const auto it = users.find(userId);
        if (it != users.end() && !it->username.isEmpty())
            who = it->username;
    }
    const QString suffix = reason.isEmpty() ? QString("left") : reason;
    const QString line = slot > 0
        ? QString("%1 (P%2) dropped (%3) — controller %2 will go idle.")
              .arg(who).arg(slot).arg(suffix)
        : QString("%1 dropped (%2).").arg(who, suffix);
    appendChatSystemLine(CHANNEL_ROOM, line);
}

void RollbackLobbyDialog::onStartGameClicked()
{
    // TODO: server-side ROOM_START handler not yet implemented; this currently
    // sends the request but won't do anything until we wire MATCH_BEGIN.
    m_client->startRoom();
}

void RollbackLobbyDialog::onRoomLeft()
{
    if (m_emulationActive || m_awaitingEmulationStart)
        emit closeMatchRequested();

    m_currentRoomId = 0;
    m_currentRoomGame.clear();
    m_currentRoomRegion.clear();
    m_currentRoomState.clear();
    m_currentRoomDelay = 2;
    m_currentRoomPrediction = 7;
    m_currentMatchId = 0;
    m_awaitingEmulationStart = false;
    m_emulationActive = false;

    if (m_client) m_client->reopenUdpAnchor();

    if (m_chatViewRoom)
    {
        const int idx = m_chatTabs->indexOf(m_chatViewRoom);
        if (idx >= 0) m_chatTabs->removeTab(idx);
        m_chatViewRoom->deleteLater();
        m_chatViewRoom = nullptr;
    }
    m_chatTabs->setCurrentWidget(m_chatViewLobby);

    switchToRoomsView();
    if (m_startBtn) m_startBtn->setEnabled(false);
    if (m_dropBtn)  m_dropBtn->setEnabled(false);
    if (m_leaveBtn) m_leaveBtn->setEnabled(true);

    const bool connected = (m_client->state() == LobbyClient::ConnectionState::Connected);
    if (m_createRoomBtn) m_createRoomBtn->setEnabled(connected);
    if (m_quickMatchBtn) m_quickMatchBtn->setEnabled(connected);

    updateInRoomBanner();
    onRoomListChanged();
}

// ──────────────────────────────────────────────────────────────────────
//  Chat
// ──────────────────────────────────────────────────────────────────────

void RollbackLobbyDialog::onChatSendClicked()
{
    const QString text = m_chatInput->text().trimmed();
    if (text.isEmpty()) return;

    QString channel = CHANNEL_LOBBY;
    if (m_chatViewRoom && m_chatTabs->currentWidget() == m_chatViewRoom)
        channel = CHANNEL_ROOM;

    m_client->sendChat(channel, text);
    m_chatInput->clear();
}

void RollbackLobbyDialog::onChatMessageReceived(const LobbyClient::ChatMessage& msg)
{
    const auto ts = QDateTime::fromMSecsSinceEpoch(msg.serverTimeMs).toString("hh:mm");
    const QString line = QString("[%1] <b>%2:</b> %3")
        .arg(ts, msg.fromUsername.toHtmlEscaped(), msg.message.toHtmlEscaped());
    appendChatLine(msg.channel, line);
}

void RollbackLobbyDialog::appendChatLine(const QString& channel, const QString& text)
{
    QTextEdit* target = m_chatViewLobby;
    if (channel == CHANNEL_ROOM && m_chatViewRoom)
        target = m_chatViewRoom;
    if (target) target->append(text);
}

void RollbackLobbyDialog::appendChatSystemLine(const QString& channel, const QString& text)
{
    const auto ts = QDateTime::currentDateTime().toString("hh:mm");
    appendChatLine(channel, QString("[%1] <i>%2</i>").arg(ts, text));
}

// ──────────────────────────────────────────────────────────────────────
//  Match handoff / quick match
// ──────────────────────────────────────────────────────────────────────

void RollbackLobbyDialog::onQuickMatchClicked()
{
    if (m_client->state() != LobbyClient::ConnectionState::Connected)
        return;

    if (m_quickMatchActive)
    {
        m_client->quickMatchCancel();
    }
    else
    {
        if (m_currentRoomId != 0)
        {
            QMessageBox::information(this, "Already in a room",
                "Leave your current room before searching for a match.");
            return;
        }
        m_client->quickMatchJoin();
    }
}

void RollbackLobbyDialog::onQuickMatchStatusChanged(bool searching, int queueSize)
{
    m_quickMatchActive = searching;
    if (!m_quickMatchBtn) return;

    if (searching)
    {
        const QString suffix = queueSize > 1
            ? QString(" (%1 in queue)").arg(queueSize)
            : QString(" (searching…)");
        m_quickMatchBtn->setText("✕  Cancel Quick Match" + suffix);
        if (m_createRoomBtn) m_createRoomBtn->setEnabled(false);
    }
    else
    {
        m_quickMatchBtn->setText("⚡  Quick Match");
        if (m_createRoomBtn)
            m_createRoomBtn->setEnabled(m_client->state() == LobbyClient::ConnectionState::Connected
                                        && m_currentRoomId == 0);
    }
}

void RollbackLobbyDialog::onMatchBegin(quint64 matchId, const QList<LobbyClient::LobbyMatchPeer>& peers)
{
    const QString line = QString("Match #%1 starting with %2 player(s)").arg(matchId).arg(peers.size());
    appendChatSystemLine(CHANNEL_LOBBY, line);
    appendChatSystemLine(CHANNEL_ROOM,  line);

    m_currentMatchId = matchId;
    m_awaitingEmulationStart = true;

    // Disable start; enable Drop now — MATCH_BEGIN arrives *after* the
    // ROOM_STATE that flipped us to in_game, so onRoomStateChanged ran before
    // the awaiting flag was set and left Drop disabled.
    if (m_startBtn) m_startBtn->setEnabled(false);
    if (m_dropBtn)  m_dropBtn->setEnabled(true);
    if (m_roomStateLabel) m_roomStateLabel->setText("Connecting…");

    const quint64 selfId = m_client->selfUserId();
    LobbyClient::LobbyMatchPeer local{};
    bool foundLocal = false;
    QStringList remotePeers;
    for (const auto& p : peers)
    {
        if (p.userId == selfId)
        {
            local = p;
            foundLocal = true;
            continue;
        }
        // "<slot>,<ip>,<port>" — matches the LOBBY| address peer-entry format.
        remotePeers << QString("%1,%2,%3").arg(p.slot).arg(p.publicIp).arg(p.publicPort);
    }
    if (!foundLocal || remotePeers.isEmpty())
    {
        appendChatSystemLine(CHANNEL_ROOM, "MATCH_BEGIN missing local or remote peer — aborting");
        return;
    }

    const quint16 localPort = m_client->localUdpPort();
    m_client->releaseUdpAnchor();

    {
        char buf[640];
        std::snprintf(buf, sizeof(buf),
            "Lobby→matchReady (deferred 100ms): game='%s' peers=%d (%s) localPort=%u slot=%d delay=%d pred=%d",
            m_currentRoomGame.toUtf8().constData(),
            int(remotePeers.size()),
            remotePeers.join("; ").toUtf8().constData(),
            unsigned(localPort),
            local.slot,
            m_currentRoomDelay,
            m_currentRoomPrediction);
        CoreAddCallbackMessage(CoreDebugMessageType::Info, buf);
    }

    // Defer the emit so the OS finishes releasing the anchor port before
    // GekkoNet attempts to bind it. 100ms is plenty on Windows for an UDP
    // socket teardown to complete; without this delay the bind races.
    const QString gameName  = m_currentRoomGame;
    const int localPortInt  = int(localPort);
    const int slot          = local.slot;
    const int delay         = m_currentRoomDelay;
    const int prediction    = m_currentRoomPrediction;
    QTimer::singleShot(100, this, [this, gameName, remotePeers, localPortInt, slot, delay, prediction]() {
        emit matchReady(gameName, remotePeers, localPortInt, slot, delay, prediction);
    });
}

#endif // NETPLAY
