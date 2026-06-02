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
#include "LobbyRegions.hpp"

#include <RMG-Core/Callback.hpp>
#include <RMG-Core/Settings.hpp>

#include <QRegularExpression>
#include <QRegularExpressionValidator>

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
#include <QComboBox>
#include <QSettings>
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>
#include <QShowEvent>
#include <QCloseEvent>
#include <QTimer>
#include <QFont>
#include <QUrl>
#include <QDebug>
#include <QVariant>
#include <cstdio>

using namespace UserInterface::Dialog;

namespace {
// Auto input-delay buckets by ping, migrated from the P2P rollback tab.
// Ping < 0 (unknown) falls back to the default. Never returns 0 — the dropdown
// hides zero delay (zero-delay rollback has open bugs; see project memory).
int autoFrameDelayForPing(int ping)
{
    if (ping < 0)    return 2;   // default when ping is unknown
    if (ping <= 50)  return 1;
    if (ping <= 100) return 2;
    if (ping <= 150) return 3;
    if (ping <= 220) return 4;
    return 5;
}

// Auto prediction is a fixed value so users don't have to think about it.
constexpr int kAutoPredictionWindow = 7;

// Show the resolved value on the "Auto" entry, e.g. "Auto (3 f)".
void setAutoComboLabel(QComboBox* combo, int resolved)
{
    if (!combo) return;
    const int idx = combo->findData(0);
    if (idx >= 0) combo->setItemText(idx, QStringLiteral("Auto (%1 f)").arg(resolved));
}
} // namespace

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
    constexpr int MARGIN_GROUP       = 10;  // banner / accent-frame content inset
    constexpr int SPACING_DEFAULT    = 8;
    constexpr int SPACING_TIGHT      = 4;

    constexpr int MARQUEE_HEIGHT     = 44;
    constexpr int BUTTON_MIN_HEIGHT  = 28;
    constexpr int HERO_BUTTON_HEIGHT = 36;
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
    connect(m_client, &LobbyClient::pingProbeMeasured,    this, &RollbackLobbyDialog::onPingMeasured);

    // 5 s cadence is a balance: fast enough that the displayed ping tracks
    // real conditions, slow enough that we're not flooding peers' anchor
    // sockets. Stays stopped until a room is entered.
    m_pingProbeTimer = new QTimer(this);
    m_pingProbeTimer->setInterval(5'000);
    connect(m_pingProbeTimer, &QTimer::timeout, this, &RollbackLobbyDialog::onPingProbeTick);
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

    // Top-level stack: the inline connect screen (0) transforms into the live
    // lobby (1) once connected, instead of a separate modal prompt.
    m_topStack = new QStackedWidget(this);
    m_topStack->addWidget(buildConnectView());   // index 0
    m_topStack->addWidget(buildLobbyView());     // index 1
    root->addWidget(m_topStack);
}

QWidget* RollbackLobbyDialog::buildLobbyView()
{
    auto* container = new QWidget(this);
    auto* lay = new QVBoxLayout(container);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    lay->addWidget(buildMarquee());

    m_splitter = new QSplitter(Qt::Horizontal, container);
    m_splitter->setHandleWidth(1);
    m_splitter->setChildrenCollapsible(false);

    auto* leftCol = new QWidget(container);
    auto* leftLay = new QVBoxLayout(leftCol);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);

    m_roomsStack = new QStackedWidget(container);
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

    lay->addWidget(m_splitter, 1);
    return container;
}

QWidget* RollbackLobbyDialog::buildConnectView()
{
    auto* page = new QWidget(this);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(32, 32, 32, 32);
    outer->addStretch(1);

    auto* card = new QWidget(page);
    card->setObjectName("LobbyConnectCard");
    card->setMaximumWidth(460);
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(14);

    auto* title = new QLabel("RMG-K Rollback Netplay", card);
    title->setAlignment(Qt::AlignHCenter);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 4.0);
    titleFont.setBold(true);
    title->setFont(titleFont);
    lay->addWidget(title);

    auto* intro = new QLabel(
        "Rollback netplay uses GGPO-style rollback for smooth, low-latency online "
        "play. Connect to the lobby to see who's online, create or join a room, and "
        "start a match.\n\nPick a username other players will see — you can change "
        "it later.", card);
    intro->setWordWrap(true);
    intro->setAlignment(Qt::AlignHCenter);
    lay->addWidget(intro);

    m_connectUsernameEdit = new QLineEdit(card);
    m_connectUsernameEdit->setMaxLength(16);
    m_connectUsernameEdit->setPlaceholderText("Username");
    m_connectUsernameEdit->setAlignment(Qt::AlignHCenter);
    auto* validator = new QRegularExpressionValidator(
        QRegularExpression(R"([A-Za-z0-9_\-\.]{1,16})"), this);
    m_connectUsernameEdit->setValidator(validator);
    // Half-width, centered (side stretch 1 : field 2 : side stretch 1 = 50%).
    auto* userRow = new QHBoxLayout();
    userRow->setContentsMargins(0, 0, 0, 0);
    userRow->addStretch(1);
    userRow->addWidget(m_connectUsernameEdit, 2);
    userRow->addStretch(1);
    lay->addLayout(userRow);

    m_connectStatusLabel = new QLabel(card);
    m_connectStatusLabel->setWordWrap(true);
    m_connectStatusLabel->setAlignment(Qt::AlignHCenter);
    lay->addWidget(m_connectStatusLabel);

    m_connectButton = new QPushButton("Connect", card);
    m_connectButton->setDefault(true);
    m_connectButton->setMinimumHeight(38);
    m_connectButton->setCursor(Qt::PointingHandCursor);
    m_connectButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Modern, rounded, primary-blue (matches the launcher's accent buttons).
    m_connectButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #0078D7;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 19px;"
        "  padding: 8px 22px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover { background-color: #1c88dc; }"
        "QPushButton:pressed { background-color: #005a9e; }"
        "QPushButton:disabled { background-color: palette(mid); color: palette(midlight); }");
    // Half-width, centered to match the username field.
    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->addStretch(1);
    btnRow->addWidget(m_connectButton, 2);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);

    connect(m_connectButton, &QPushButton::clicked,
            this, &RollbackLobbyDialog::onConnectClicked);
    connect(m_connectUsernameEdit, &QLineEdit::returnPressed,
            this, &RollbackLobbyDialog::onConnectClicked);
    connect(m_connectUsernameEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        if (m_connectStatusLabel) m_connectStatusLabel->clear();
    });

    outer->addWidget(card, 0, Qt::AlignHCenter);
    outer->addStretch(2);
    return page;
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
        "Auto-match with another player searching for the selected game.\n"
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

    // ── Game picker — pulls from the RMG-K library. Both Create Room and
    //    Quick Match key off the selected game's MD5. ──
    auto* gameRow = new QHBoxLayout;
    gameRow->setSpacing(SPACING_DEFAULT);
    auto* gameLbl = new QLabel("Game:", this);
    m_browseRomCombo = new QComboBox(this);
    m_browseRomCombo->setObjectName("BrowseRomCombo");
    m_browseRomCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    gameRow->addWidget(gameLbl, 0);
    gameRow->addWidget(m_browseRomCombo, 1);
    lay->addLayout(gameRow);
    populateBrowseRoms(); // fill from whatever library we have so far

    // ── Active Rooms ──
    auto* roomsHeader = new QLabel("ACTIVE ROOMS", this);
    roomsHeader->setProperty("class", "SectionHeader");
    lay->addWidget(roomsHeader);

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
    lay->addWidget(m_roomsTree, 1);

    // ── Ongoing Matches ──
    auto* matchesHeader = new QLabel("ONGOING MATCHES", this);
    matchesHeader->setProperty("class", "SectionHeader");
    lay->addWidget(matchesHeader);

    m_matchesTree = new QTreeWidget(this);
    m_matchesTree->setObjectName("MatchesTree");
    m_matchesTree->setHeaderLabels({ "Players", "Duration", "ROM" });
    m_matchesTree->setRootIsDecorated(false);
    m_matchesTree->setAlternatingRowColors(true);
    m_matchesTree->setFrameShape(QFrame::NoFrame);
    m_matchesTree->setFixedHeight(120);
    m_matchesTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_matchesTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_matchesTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    lay->addWidget(m_matchesTree);

    // Tick the ongoing-match durations once a second (the room list only
    // refreshes on events, so the timer keeps the elapsed time live).
    m_matchDurationTimer = new QTimer(this);
    m_matchDurationTimer->setInterval(1000);
    connect(m_matchDurationTimer, &QTimer::timeout, this, &RollbackLobbyDialog::updateMatchDurations);
    m_matchDurationTimer->start();

    return page;
}

// ── In-room view ─────────────────────────────────────────────────────

QWidget* RollbackLobbyDialog::buildInRoomView()
{
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(MARGIN_OUTER, MARGIN_OUTER, MARGIN_OUTER, MARGIN_OUTER);
    lay->setSpacing(SPACING_DEFAULT);

    // ── Header (room info) — no group-box wrapper; the hero title carries
    //    its own visual weight, and the SEATS section header below provides
    //    the only divider this column needs. ──

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
    lay->addLayout(topRow);

    // ROM title (large)
    m_roomTitle = new QLabel("—", this);
    bumpFont(m_roomTitle, 4, /*bold=*/true);
    lay->addWidget(m_roomTitle);

    // Subtitle (host / max players) — default WindowText since this is
    // important info; the bold title above gives the hierarchy.
    m_roomSubtitle = new QLabel("—", this);
    lay->addWidget(m_roomSubtitle);

    // Meta line (Seats / Region) — plain rich text. Delay and prediction
    // live in the editable settings row below instead of read-only chips.
    m_roomMetaLabel = new QLabel("—", this);
    m_roomMetaLabel->setTextFormat(Qt::RichText);
    m_roomMetaLabel->setContentsMargins(0, SPACING_TIGHT, 0, 0);
    lay->addWidget(m_roomMetaLabel);

    // ── Rollback settings row (host-editable) ──
    auto* settingsRow = new QHBoxLayout;
    settingsRow->setContentsMargins(0, SPACING_TIGHT, 0, 0);
    settingsRow->setSpacing(SPACING_DEFAULT);

    const QString delayTip = QStringLiteral(
        "Frames of input delay added before sending to peer.\n"
        "Higher delay = fewer rollbacks but more input latency.\n"
        "Recommended: 2 for ~80ms RTT, 3-4 for ~150ms RTT.\n"
        "\n"
        "Host-only. All players will use the same value.");
    const QString predictionTip = QStringLiteral(
        "Maximum frames the rollback engine may predict ahead.\n"
        "Higher prediction = more network tolerance.\n"
        "Recommended: 7 (matches Slippi default).\n"
        "\n"
        "Host-only. All players will use the same value.");

    // Frame values exposed in the dropdown. 0 is intentionally omitted —
    // GekkoNet's zero-delay path still has open bugs (see project memory:
    // 0-delay host crash with non-zero analog drift), so we hide it from
    // users until that's resolved. Editing this list updates the UI; the
    // server's clamp range governs what values it'll actually accept.
    static const QList<int> FRAME_OPTIONS = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    // The auto entry is data 0 (a value the numeric list never uses), inserted
    // at the top of each combo. Delay's auto resolves from ping (labelled
    // "Auto"); prediction's is a fixed 7 (labelled "Default").
    auto fillFrameCombo = [](QComboBox* combo, const QString& autoLabel) {
        combo->addItem(autoLabel, 0);
        for (int v : FRAME_OPTIONS)
            combo->addItem(QString("%1 f").arg(v), v);
        combo->setCurrentIndex(0); // default to the auto/default entry
    };

    auto* delayLbl = new QLabel("Frame delay:", this);
    m_delayCombo = new QComboBox(this);
    fillFrameCombo(m_delayCombo, "Auto");
    m_delayCombo->setToolTip(delayTip);
    // Stash the explainer so onRoomStateChanged can restore it after a
    // disabled stint (host became host again, or match ended).
    m_delayCombo->setProperty("originalTip", delayTip);
    settingsRow->addWidget(delayLbl);
    settingsRow->addWidget(m_delayCombo);

    settingsRow->addSpacing(SPACING_DEFAULT * 2);

    auto* predLbl = new QLabel("Prediction:", this);
    m_predictionCombo = new QComboBox(this);
    fillFrameCombo(m_predictionCombo, "Default");
    m_predictionCombo->setToolTip(predictionTip);
    m_predictionCombo->setProperty("originalTip", predictionTip);
    settingsRow->addWidget(predLbl);
    settingsRow->addWidget(m_predictionCombo);

    settingsRow->addStretch(1);
    lay->addLayout(settingsRow);

    // Both combos share the same change handler. Skip emits while we're
    // applying values from a ROOM_STATE refresh — otherwise we'd ping-pong
    // a settings update back to the server on every server-driven refresh.
    auto pushSettings = [this]() {
        if (m_suppressSettingsSignal) return;
        if (m_currentRoomId == 0) return;
        if (!m_client) return;
        // A combo's "Auto" entry has data 0; track each mode, then resolve and
        // push the concrete values (applyHostRoomSettings never sends 0).
        m_delayAuto      = (m_delayCombo->currentData().toInt() == 0);
        m_predictionAuto = (m_predictionCombo->currentData().toInt() == 0);
        applyHostRoomSettings(true);
    };
    connect(m_delayCombo,      QOverload<int>::of(&QComboBox::currentIndexChanged), this, pushSettings);
    connect(m_predictionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, pushSettings);

    // ── Seats — bold section header + vertical player-list rows ──
    auto* seatsHeader = new QLabel("SEATS", this);
    seatsHeader->setProperty("class", "SectionHeader");
    seatsHeader->setContentsMargins(0, SPACING_DEFAULT, 0, 0);
    lay->addWidget(seatsHeader);

    auto* seatsBox = new QWidget(this);
    auto* seatsLay = new QVBoxLayout(seatsBox);
    seatsLay->setContentsMargins(0, SPACING_TIGHT, 0, 0);
    seatsLay->setSpacing(0);
    for (int i = 0; i < 4; ++i)
    {
        buildSeatRow(m_seats[i], i + 1, seatsBox);
        seatsLay->addWidget(m_seats[i].row);
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

// ── Seat row ─────────────────────────────────────────────────────────

void RollbackLobbyDialog::buildSeatRow(SeatRow& s, int slotIdx, QWidget* parent)
{
    s.row = new QWidget(parent);
    auto* lay = new QHBoxLayout(s.row);
    lay->setContentsMargins(SPACING_DEFAULT, SPACING_TIGHT, SPACING_DEFAULT, SPACING_TIGHT);
    lay->setSpacing(SPACING_DEFAULT);

    // Filled/empty dot — single glyph, color follows palette so it picks
    // up the theme like everything else.
    s.dotLabel = new QLabel(QStringLiteral("○"), s.row);
    s.dotLabel->setFixedWidth(14);
    lay->addWidget(s.dotLabel);

    // Slot tag — bold, fixed width so names line up across rows.
    s.slotLabel = new QLabel(QString("P%1").arg(slotIdx), s.row);
    s.slotLabel->setFixedWidth(28);
    {
        QFont f = s.slotLabel->font();
        f.setBold(true);
        s.slotLabel->setFont(f);
    }
    lay->addWidget(s.slotLabel);

    s.nameLabel = new QLabel(QStringLiteral("Waiting…"), s.row);
    lay->addWidget(s.nameLabel);

    lay->addStretch(1);

    // Right-aligned meta (host · ping). PlaceholderText is theme-aware
    // muted body text — readable on Fusion Dark and not screaming on light.
    s.metaLabel = new QLabel(QString(), s.row);
    s.metaLabel->setForegroundRole(QPalette::PlaceholderText);
    lay->addWidget(s.metaLabel);

    // Host-only kick button. Hidden by default; shown on non-host seats only
    // when the local user is the host (set in renderSeatFilled).
    s.kickButton = new QPushButton(QStringLiteral("✕"), s.row);
    s.kickButton->setObjectName("SeatKickButton");
    s.kickButton->setFixedSize(20, 20);
    s.kickButton->setCursor(Qt::PointingHandCursor);
    s.kickButton->setFocusPolicy(Qt::NoFocus);
    s.kickButton->setToolTip(QStringLiteral("Remove this player from the match"));
    s.kickButton->setVisible(false);
    connect(s.kickButton, &QPushButton::clicked, this, [this, &s]() {
        if (!m_client || s.userId == 0)
            return;
        const QString name = s.nameLabel ? s.nameLabel->text() : QString();
        if (QMessageBox::question(this, "Remove player",
                QString("Remove %1 from the match?").arg(name.isEmpty() ? QStringLiteral("this player") : name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        m_client->kickFromRoom(s.userId);
    });
    lay->addWidget(s.kickButton);
}

void RollbackLobbyDialog::renderSeatEmpty(SeatRow& s)
{
    s.isHost = false;
    s.userId = 0;
    if (s.dotLabel)
    {
        s.dotLabel->setText(QStringLiteral("○"));
        s.dotLabel->setForegroundRole(QPalette::PlaceholderText);
    }
    if (s.slotLabel)
        s.slotLabel->setForegroundRole(QPalette::PlaceholderText);
    if (s.nameLabel)
    {
        s.nameLabel->setText(QStringLiteral("Waiting…"));
        s.nameLabel->setForegroundRole(QPalette::PlaceholderText);
    }
    if (s.metaLabel) s.metaLabel->setText(QString());
    if (s.kickButton) s.kickButton->setVisible(false);
}

void RollbackLobbyDialog::renderSeatFilled(SeatRow& s, const QString& username, bool isHost,
                                           bool isSelf, int pingMs, bool canKick)
{
    s.isHost = isHost;
    if (s.kickButton) s.kickButton->setVisible(canKick);
    if (s.dotLabel)
    {
        s.dotLabel->setText(QStringLiteral("●"));
        s.dotLabel->setForegroundRole(QPalette::WindowText);
    }
    if (s.slotLabel)
        s.slotLabel->setForegroundRole(QPalette::WindowText);
    if (s.nameLabel)
    {
        QString name = username;
        if (isSelf) name += QStringLiteral("  (you)");
        s.nameLabel->setText(name);
        s.nameLabel->setForegroundRole(QPalette::WindowText);
    }
    if (s.metaLabel)
    {
        // Compose host + ping. Self never shows a ping (pingMs == -1 also
        // means "no measurement yet" for peers we haven't probed). Ping shows
        // up after the first PROBE_REPLY arrives, refreshes on each tick.
        QStringList parts;
        if (isHost)              parts << QStringLiteral("host");
        if (!isSelf && pingMs >= 0) parts << QStringLiteral("%1 ms").arg(pingMs);
        s.metaLabel->setText(parts.join(QStringLiteral(" · ")));
    }
}

// ── Chat column ──────────────────────────────────────────────────────

QWidget* RollbackLobbyDialog::buildChatColumn()
{
    // No "Chat" wrapper — the tab labels ("Lobby" / "Room") already convey
    // the column's identity and the splitter handle separates it visually.
    auto* col = new QWidget(this);
    auto* lay = new QVBoxLayout(col);
    lay->setContentsMargins(MARGIN_OUTER, MARGIN_OUTER, MARGIN_OUTER, MARGIN_OUTER);
    lay->setSpacing(SPACING_DEFAULT);

    m_chatTabs = new QTabWidget(this);
    // documentMode=true gives Chrome-style "floating" tabs that visually
    // detach from the content below; documentMode=false (the default) lets
    // the active tab sit flush in the pane border instead. Combine with
    // QTextEdit::NoFrame inside so we don't double-border the chat area.

    m_chatViewLobby = new QTextEdit(this);
    m_chatViewLobby->setReadOnly(true);
    m_chatViewLobby->setLineWrapMode(QTextEdit::WidgetWidth);
    m_chatViewLobby->setFrameShape(QFrame::NoFrame);
    m_chatTabs->addTab(m_chatViewLobby, "Lobby");

    lay->addWidget(m_chatTabs, 1);

    m_chatInput = new QLineEdit(this);
    m_chatInput->setPlaceholderText("Type a message and press Enter…");
    m_chatInput->setEnabled(false);
    m_chatInput->setMinimumHeight(BUTTON_MIN_HEIGHT);
    connect(m_chatInput, &QLineEdit::returnPressed, this, &RollbackLobbyDialog::onChatSendClicked);
    lay->addWidget(m_chatInput);

    return col;
}

// ── Players column ───────────────────────────────────────────────────

QWidget* RollbackLobbyDialog::buildPlayersColumn()
{
    // No "Players" wrapper — the tree's column header ("Player / State /
    // Ping") already labels the content and the splitter handle isolates
    // the column visually.
    auto* col = new QWidget(this);
    auto* lay = new QVBoxLayout(col);
    lay->setContentsMargins(MARGIN_OUTER, MARGIN_OUTER, MARGIN_OUTER, MARGIN_OUTER);
    lay->setSpacing(SPACING_DEFAULT);

    m_playersTree = new QTreeWidget(this);
    m_playersTree->setObjectName("PlayersTree");
    m_playersTree->setHeaderLabels({ "Player", "State", "Est. Ping" });
    m_playersTree->setRootIsDecorated(false);
    m_playersTree->setSortingEnabled(true);
    m_playersTree->setAlternatingRowColors(true);
    m_playersTree->setFrameShape(QFrame::NoFrame);
    m_playersTree->setUniformRowHeights(true);
    m_playersTree->header()->setStretchLastSection(false);
    m_playersTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_playersTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_playersTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    lay->addWidget(m_playersTree);

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

        // Section headers — bold uppercase label with a hairline divider
        // below. Replaces the QGroupBox wrappers we used to have around
        // every region; reads as a flatter, list-style hierarchy.
        "QLabel[class=\"SectionHeader\"] {"
        "  font-weight: 700;"
        "  color: palette(text);"
        "  padding-top: 6px;"
        "  padding-bottom: 4px;"
        "  border-bottom: 1px solid %1;"
        "}"

        // In-room banner — visible in browse view when the user is seated.
        // Kept framed since it's a CTA accent, not a content region.
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
//  ROM library
// ──────────────────────────────────────────────────────────────────────

void RollbackLobbyDialog::setRomLibrary(const QMap<QString, CoreRomSettings>& roms)
{
    m_roms = roms;
    populateBrowseRoms();
}

void RollbackLobbyDialog::populateBrowseRoms()
{
    if (!m_browseRomCombo) return;

    m_browseRomCombo->blockSignals(true);
    m_browseRomCombo->clear();

    if (m_roms.isEmpty())
    {
        m_browseRomCombo->addItem("No ROMs in your library — add some first");
        m_browseRomCombo->setEnabled(false);
        m_browseRomCombo->blockSignals(false);
        return;
    }
    m_browseRomCombo->setEnabled(true);

    // Sort by display name (file path as tiebreaker), matching CreateRoomDialog
    // so the names line up and the Create Room pre-select resolves.
    QMap<QString, QString> sortedByName; // "<name>\x01<file>" → file
    for (auto it = m_roms.constBegin(); it != m_roms.constEnd(); ++it)
    {
        const QString display = CreateRoomDialog::displayGameName(
            QString::fromStdString(it.value().GoodName), it.key());
        sortedByName.insert(display + "\x01" + it.key(), it.key());
    }
    for (auto it = sortedByName.constBegin(); it != sortedByName.constEnd(); ++it)
    {
        const QString file = it.value();
        const QString display = CreateRoomDialog::displayGameName(
            QString::fromStdString(m_roms.value(file).GoodName), file);
        // Store the ROM's name + MD5 as item data so Quick Match can scope the
        // search to the selected game (matches the shape Create Room sends).
        QVariantMap romData;
        romData["name"] = display;
        romData["md5"]  = QString::fromStdString(m_roms.value(file).MD5);
        romData["file"] = file;
        m_browseRomCombo->addItem(display, romData);
    }

    // Restore the last-used selection (shared with Create Room via QSettings).
    QSettings s;
    s.beginGroup("Lobby/CreateRoom");
    const QString preferred = s.value("Rom").toString();
    s.endGroup();
    if (!preferred.isEmpty())
    {
        const int idx = m_browseRomCombo->findText(preferred);
        if (idx >= 0) m_browseRomCombo->setCurrentIndex(idx);
    }

    m_browseRomCombo->blockSignals(false);
}

// ──────────────────────────────────────────────────────────────────────
//  Show / close
// ──────────────────────────────────────────────────────────────────────

void RollbackLobbyDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // Show the inline connect screen unless we're already in a live session.
    if (m_client->state() == LobbyClient::ConnectionState::Connected)
    {
        showLobbyView();
    }
    else
    {
        showConnectView();
    }
}

void RollbackLobbyDialog::closeEvent(QCloseEvent* event)
{
    if (m_client)
        m_client->disconnectFromServer();
    QDialog::closeEvent(event);
}

QString RollbackLobbyDialog::prefillUsername() const
{
    // Prefer a previously saved lobby name, then the Kaillera username, then the
    // OS username. Conform whatever we pick to the lobby's allowed charset/length.
    QSettings s;
    QString name = s.value("Lobby/Username").toString().trimmed();

    if (name.isEmpty())
    {
        const QString kaillera = QString::fromStdString(
            CoreSettingsGetStringValue(SettingsID::Kaillera_Username)).trimmed();
        if (!kaillera.isEmpty() && kaillera != "Player")
        {
            name = kaillera;
        }
    }

    if (name.isEmpty())
    {
        QByteArray envUser = qgetenv("USER");
        if (envUser.isEmpty())
        {
            envUser = qgetenv("USERNAME");
        }
        name = QString::fromUtf8(envUser).trimmed();
    }

    name.remove(QRegularExpression(R"([^A-Za-z0-9_\-\.])"));
    return name.left(16);
}

void RollbackLobbyDialog::showConnectView(const QString& statusMessage)
{
    if (!m_topStack)
    {
        return;
    }

    if (m_connectUsernameEdit)
    {
        if (m_connectUsernameEdit->text().trimmed().isEmpty())
        {
            m_connectUsernameEdit->setText(prefillUsername());
        }
        m_connectUsernameEdit->setFocus();
        m_connectUsernameEdit->selectAll();
    }
    if (m_connectButton)
    {
        m_connectButton->setEnabled(true);
    }
    if (m_connectStatusLabel)
    {
        m_connectStatusLabel->setStyleSheet(statusMessage.isEmpty() ? QString() : "color: #c0392b;");
        m_connectStatusLabel->setText(statusMessage);
    }

    m_topStack->setCurrentIndex(0);
}

void RollbackLobbyDialog::showLobbyView()
{
    if (m_topStack)
    {
        m_topStack->setCurrentIndex(1);
    }
}

void RollbackLobbyDialog::onConnectClicked()
{
    const QString username = m_connectUsernameEdit
        ? m_connectUsernameEdit->text().trimmed()
        : QString();

    if (username.length() < 3)
    {
        if (m_connectStatusLabel)
        {
            m_connectStatusLabel->setStyleSheet("color: #c0392b;");
            m_connectStatusLabel->setText("Username must be at least 3 characters.");
        }
        if (m_connectUsernameEdit) m_connectUsernameEdit->setFocus();
        return;
    }

    m_username  = username;
    m_serverUrl = LobbyConnectDialog::defaultServerUrl();
    if (m_userLabel) m_userLabel->setText(QString("User: %1").arg(m_username));

    // Remember it so the field pre-fills next time.
    QSettings s;
    s.setValue("Lobby/Username", m_username);

    if (m_connectButton) m_connectButton->setEnabled(false);
    if (m_connectStatusLabel)
    {
        m_connectStatusLabel->setStyleSheet(QString());
        m_connectStatusLabel->setText("Connecting…");
    }

    updateServerMeta();
    m_client->connectToServer(m_serverUrl, m_username, {}, QString());

    // Transform straight into the lobby; the marquee shows live connection state.
    showLobbyView();
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

    // Drive the top-level connect ↔ lobby swap. Failed is handled by the error
    // slots below (onConnectError/onHelloFailed), which fire just before it and
    // show the connect screen with a message.
    if (s == LobbyClient::ConnectionState::Connected)
    {
        showLobbyView();
    }
    else if (s == LobbyClient::ConnectionState::Disconnected)
    {
        showConnectView();
    }
}

void RollbackLobbyDialog::onHelloFailed(const QString& reason)
{
    QString human = reason;
    if (reason == "username_taken")    human = "That username is already in use.";
    else if (reason == "invalid_hello") human = "Server rejected the connection handshake.";
    else if (reason == "version_mismatch") human = "Client version is incompatible with this server.";

    showConnectView(human);
}

void RollbackLobbyDialog::onConnectError(const QString& msg)
{
    showConnectView("Couldn't reach the lobby: " + msg);
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

    // While searching, hovering the state shows which ROM they're queued for.
    if (u.state == "searching" && !u.searchingRom.isEmpty())
        item->setToolTip(1, QString("Searching for: %1").arg(u.searchingRom));
    else
        item->setToolTip(1, QString());

    // Ping column shows an estimated round-trip between *this* client and the
    // peer, based on region buckets the server assigned via IP geolocation.
    // It's intentionally rough — same-region pairs read ~30 ms, transatlantic
    // ~90, transpacific ~200. Self-row always shows "—".
    // U+2014 EM DASH — using QChar avoids any file-encoding ambiguity in
    // the literal.
    const QString dash = QString(QChar(0x2014));
    if (u.id == m_client->selfUserId())
    {
        item->setText(2, dash);
    }
    else
    {
        const int rtt = UserInterface::Dialog::LobbyRegions::estimatedRttMs(
            m_client->selfRegion(), u.region);
        item->setText(2, rtt > 0 ? QString("~%1 ms").arg(rtt) : dash);
    }

    const QString regionLabel = UserInterface::Dialog::LobbyRegions::labelFor(u.region);
    const QString regionTip = QString("Region: %1").arg(regionLabel.isEmpty() ? "unknown" : regionLabel);
    item->setToolTip(0, regionTip);
    item->setToolTip(2, regionTip); // hovering the ping shows the peer's region
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

static QString formatMatchDuration(qint64 startedAtMs)
{
    if (startedAtMs <= 0)
        return QStringLiteral("—");

    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - startedAtMs;
    const qint64 totalSecs = elapsedMs > 0 ? elapsedMs / 1000 : 0;
    const qint64 h = totalSecs / 3600;
    const qint64 m = (totalSecs % 3600) / 60;
    const qint64 s = totalSecs % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

void RollbackLobbyDialog::onRoomListChanged()
{
    m_roomsTree->clear();
    m_matchesTree->clear();
    m_roomItems.clear();
    for (auto it = m_client->rooms().constBegin(); it != m_client->rooms().constEnd(); ++it)
    {
        const LobbyClient::LobbyRoomSummary& r = it.value();

        // A room that's playing is a live match — show it under Ongoing Matches
        // and drop it from the joinable Active Rooms list.
        if (r.state == "in_game")
        {
            auto* matchRow = new QTreeWidgetItem(m_matchesTree);
            matchRow->setText(0, r.playerNames.isEmpty() ? r.name : r.playerNames.join(" vs "));
            matchRow->setText(1, formatMatchDuration(r.startedAtMs));
            matchRow->setText(2, r.romName);
            matchRow->setData(0, Qt::UserRole, QVariant::fromValue(r.id));
            matchRow->setData(1, Qt::UserRole, r.startedAtMs); // for the duration ticker
            continue;
        }

        auto* row = new QTreeWidgetItem(m_roomsTree);
        refreshRoomRow(row, r);
        m_roomItems.insert(it.key(), row);
    }
    updateServerMeta();
    updateInRoomBanner();   // seat counts may have changed in our own room
}

void RollbackLobbyDialog::updateMatchDurations()
{
    if (!m_matchesTree)
        return;
    for (int i = 0; i < m_matchesTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = m_matchesTree->topLevelItem(i);
        item->setText(1, formatMatchDuration(item->data(1, Qt::UserRole).toLongLong()));
    }
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
    // Open Create Room on the game chosen in the browse picker. CreateRoomDialog
    // seeds its ROM from this key (findText on identical display names).
    if (m_browseRomCombo && m_browseRomCombo->isEnabled() &&
        !m_browseRomCombo->currentText().isEmpty())
    {
        QSettings s;
        s.beginGroup("Lobby/CreateRoom");
        s.setValue("Rom", m_browseRomCombo->currentText());
        s.endGroup();
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

    // Being in a room is mutually exclusive with searching. A successful Quick
    // Match drops us straight into an auto-created room without the server ever
    // sending QUICK_MATCH_STATUS{searching:false}, so clear the toggle state
    // here. Otherwise m_quickMatchActive stays true through the game and onClick
    // sends quickMatchCancel() (a no-op once we're out of the queue) instead of
    // quickMatchJoin() — leaving the button unable to start a new search.
    m_quickMatchActive = false;
    if (m_quickMatchBtn) m_quickMatchBtn->setText("⚡  Quick Match");

    if (!m_chatViewRoom)
    {
        m_chatViewRoom = new QTextEdit(this);
        m_chatViewRoom->setReadOnly(true);
        m_chatViewRoom->setLineWrapMode(QTextEdit::WidgetWidth);
        m_chatViewRoom->setFrameShape(QFrame::NoFrame);
        m_chatTabs->addTab(m_chatViewRoom, "Room");
    }
    m_chatTabs->setCurrentWidget(m_chatViewRoom);
    switchToInRoomView();

    if (m_createRoomBtn) m_createRoomBtn->setEnabled(false);
    if (m_quickMatchBtn) m_quickMatchBtn->setEnabled(false);

    updateInRoomBanner();

    if (!greetingChatLine.isEmpty())
        appendChatLine(CHANNEL_LOBBY, greetingChatLine);

    // Start measuring actual round-trip to seated peers. Seat assignments
    // populate via the follow-up ROOM_STATE message; the first tick fires
    // 5 s after entry which gives that state plenty of time to arrive.
    if (m_pingProbeTimer && !m_pingProbeTimer->isActive())
        m_pingProbeTimer->start();
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
    metaParts << QString("<b>Seats:</b> %1/%2").arg(players.size()).arg(maxPlayers);
    if (!romRegion.isEmpty())
        metaParts << QString("<b>Region:</b> %1").arg(romRegion);
    m_roomMetaLabel->setText(metaParts.join("  ·  "));

    // Sync the delay / prediction combos with the authoritative room state.
    // Suppress the currentIndexChanged signal during the assignment so the
    // combo doesn't immediately re-send an UPDATE for the same value we
    // just received from the server.
    if (m_delayCombo && m_predictionCombo)
    {
        static const QString tipDisabledNotHost = QStringLiteral(
            "Only the host can change rollback settings.");
        static const QString tipDisabledMidMatch = QStringLiteral(
            "Can't change settings during a match.");

        const bool editable = iAmHost && state == "waiting";
        m_suppressSettingsSignal = true;
        // Look up the index for the server-supplied value via findData
        // since the combo's index no longer maps 1:1 to the value (0/Auto is
        // not a real value). Falls back to the floor entry if the server
        // somehow handed us a value we don't expose.
        auto pickIndex = [](QComboBox* combo, int value) {
            const int idx = combo->findData(value);
            return idx >= 0 ? idx : 0;
        };
        // For the host of a waiting room, keep the combo on "Auto" when that's
        // the chosen mode — otherwise the server's echo of the resolved number
        // would knock it off Auto. Everyone else shows the concrete value.
        if (editable && m_delayAuto)
            m_delayCombo->setCurrentIndex(m_delayCombo->findData(0));
        else
            m_delayCombo->setCurrentIndex(pickIndex(m_delayCombo, delay));
        if (editable && m_predictionAuto)
            m_predictionCombo->setCurrentIndex(m_predictionCombo->findData(0));
        else
            m_predictionCombo->setCurrentIndex(pickIndex(m_predictionCombo, prediction));
        // Keep the delay "Auto" entry's label showing the live resolved value.
        // (Prediction's "Default" entry is a plain, fixed label.)
        setAutoComboLabel(m_delayCombo, delay);
        m_delayCombo->setEnabled(editable);
        m_predictionCombo->setEnabled(editable);

        const QString delayTip = editable
            ? m_delayCombo->property("originalTip").toString()
            : (iAmHost ? tipDisabledMidMatch : tipDisabledNotHost);
        const QString predTip = editable
            ? m_predictionCombo->property("originalTip").toString()
            : (iAmHost ? tipDisabledMidMatch : tipDisabledNotHost);
        m_delayCombo->setToolTip(delayTip);
        m_predictionCombo->setToolTip(predTip);
        m_suppressSettingsSignal = false;
    }

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
        const int pingMs = slotIsSelf ? -1 : m_client->measuredPingMs(uid);
        // The host can remove any seated player except themselves (the host seat).
        const bool canKick = iAmHost && !slotIsHost;
        m_seats[slot - 1].userId = uid;
        renderSeatFilled(m_seats[slot - 1], user, slotIsHost, slotIsSelf, pingMs, canKick);
        filled[slot - 1] = true;
    }
    for (int i = 0; i < 4; ++i)
    {
        if (i >= maxPlayers) m_seats[i].row->setVisible(false);
        else
        {
            m_seats[i].row->setVisible(true);
            if (!filled[i])
            {
                m_seats[i].userId = 0;
                renderSeatEmpty(m_seats[i]);
            }
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

void RollbackLobbyDialog::onPingProbeTick()
{
    if (!m_client || m_currentRoomId == 0)
        return;

    const quint64 selfId = m_client->selfUserId();
    for (const auto& s : m_seats)
    {
        if (s.userId == 0 || s.userId == selfId)
            continue;
        m_client->requestPingProbe(s.userId);
    }
}

void RollbackLobbyDialog::onPingMeasured(quint64 userId, int rttMs)
{
    // Only redraw the seat that just got a measurement — avoids churning the
    // whole room view on every probe response.
    for (auto& s : m_seats)
    {
        if (s.userId != userId || !s.metaLabel)
            continue;
        QStringList parts;
        if (s.isHost) parts << QStringLiteral("host");
        parts << QStringLiteral("%1 ms").arg(rttMs);
        s.metaLabel->setText(parts.join(QStringLiteral(" · ")));
        break;
    }

    // Auto delay follows ping — re-resolve when a fresh RTT lands. The helper
    // only acts when we're the host of a waiting room with Auto selected.
    if (m_delayAuto)
        applyHostRoomSettings(false);
}

int RollbackLobbyDialog::worstSeatPingMs() const
{
    if (!m_client) return -1;
    const quint64 selfId = m_client->selfUserId();
    int worst = -1;
    for (const auto& s : m_seats)
    {
        if (s.userId == 0 || s.userId == selfId)
            continue;
        const int p = m_client->measuredPingMs(s.userId);
        if (p > worst) worst = p; // tune against the worst host↔peer link
    }
    return worst;
}

void RollbackLobbyDialog::applyHostRoomSettings(bool force)
{
    if (m_currentRoomId == 0 || !m_client || !m_delayCombo || !m_predictionCombo)
        return;
    // The delay combo is enabled only for the host of a waiting room, so its
    // enabled state doubles as the "I'm allowed to push settings now" gate.
    if (!m_delayCombo->isEnabled())
        return;

    const int effDelay = m_delayAuto
        ? autoFrameDelayForPing(worstSeatPingMs())
        : m_delayCombo->currentData().toInt();
    const int effPred = m_predictionAuto
        ? kAutoPredictionWindow
        : m_predictionCombo->currentData().toInt();

    // Delay's "Auto" entry shows the resolved value, e.g. "Auto (4 f)".
    // Prediction's "Default" entry stays a plain label (always 7, not ping-based).
    if (m_delayAuto) setAutoComboLabel(m_delayCombo, effDelay);

    if (!force && effDelay == m_currentRoomDelay && effPred == m_currentRoomPrediction)
        return;

    m_client->updateRoomSettings(effDelay, effPred);

    // Persist the concrete resolved values (never the Auto sentinel 0) so
    // CreateRoomDialog seeds a valid delay/prediction for the next room.
    QSettings s;
    s.beginGroup("Lobby/CreateRoom");
    s.setValue("Delay",      effDelay);
    s.setValue("Prediction", effPred);
    s.endGroup();
}

void RollbackLobbyDialog::onRoomLeft(const QString& reason)
{
    if (m_emulationActive || m_awaitingEmulationStart)
        emit closeMatchRequested();

    if (m_pingProbeTimer) m_pingProbeTimer->stop();
    for (auto& s : m_seats)
        s.userId = 0;

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

    // Tell the user why they left, if it wasn't their own doing.
    if (reason == QLatin1String("kicked"))
        QMessageBox::information(this, "Removed from match",
            "You were removed from the lobby by the host.");
    else if (reason == QLatin1String("host_left"))
        QMessageBox::information(this, "Room closed",
            "The host closed the room.");
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

    // Mirror room chat into the in-game overlay. The server relays room chat to
    // every member (including the sender), so forwarding all room messages —
    // own and remote — makes both show on the OSD whether they were typed in
    // the dialog or via the in-game chat key.
    if (msg.channel == CHANNEL_ROOM)
    {
        emit roomChatReceived(msg.fromUsername, msg.message);
    }
}

void RollbackLobbyDialog::sendRoomChat(const QString& message)
{
    if (!m_client)
        return;

    const QString text = message.trimmed();
    if (text.isEmpty())
        return;

    m_client->sendChat(CHANNEL_ROOM, text);
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

        const QVariantMap romData = m_browseRomCombo ? m_browseRomCombo->currentData().toMap() : QVariantMap();
        const QString romName = romData.value("name").toString();
        const QString romMd5  = romData.value("md5").toString();
        if (romMd5.isEmpty())
        {
            QMessageBox::information(this, "Select a game",
                "Pick a game from the dropdown before searching — Quick Match "
                "only pairs you with players searching for that same ROM.");
            return;
        }
        qInfo() << "lobby: quick match search" << "game" << romName << "md5" << romMd5;
        m_client->quickMatchJoin(romName, romMd5);
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

    // Punch peer NATs from the anchor socket before handing the port to
    // GekkoNet. Both peers receive MATCH_BEGIN within ~RTT of each other, so
    // both fire while the other's anchor is still open — opens the NAT
    // mapping so GekkoNet's first frame doesn't have to eat the handshake.
    m_client->punchPeerEndpoints(peers);

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
