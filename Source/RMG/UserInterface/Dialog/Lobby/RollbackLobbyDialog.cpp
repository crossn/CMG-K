/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 */
#ifdef NETPLAY

#include "RollbackLobbyDialog.hpp"
#include "LobbyConnectDialog.hpp"
#include "CreateRoomDialog.hpp"

#include <RMG-Core/Callback.hpp>

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
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>
#include <QGroupBox>
#include <QShowEvent>
#include <QCloseEvent>
#include <QTimer>
#include <QFont>
#include <QBrush>
#include <cstdio>

using namespace UserInterface::Dialog;

namespace
{
    const char* const CHANNEL_LOBBY = "lobby";
    const char* const CHANNEL_ROOM  = "room";

    QString humanState(LobbyClient::ConnectionState s)
    {
        switch (s)
        {
            case LobbyClient::ConnectionState::Disconnected:   return "Disconnected";
            case LobbyClient::ConnectionState::Connecting:     return "Connecting...";
            case LobbyClient::ConnectionState::Authenticating: return "Authenticating...";
            case LobbyClient::ConnectionState::Connected:      return "Connected";
            case LobbyClient::ConnectionState::Failed:         return "Connection failed";
        }
        return "Unknown";
    }

    QString stateDotColor(LobbyClient::ConnectionState s)
    {
        switch (s)
        {
            case LobbyClient::ConnectionState::Connected:      return "#3c3";  // green
            case LobbyClient::ConnectionState::Connecting:
            case LobbyClient::ConnectionState::Authenticating: return "#dc3";  // yellow
            case LobbyClient::ConnectionState::Failed:         return "#c33";  // red
            default:                                           return "#666";  // gray
        }
    }
} // namespace

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
    resize(1100, 700);

    m_client = new LobbyClient(this);

    buildUi();

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

// ---- UI construction ----

void RollbackLobbyDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // ---- Top connection bar ----
    auto* topBar = new QHBoxLayout;

    m_statusDot = new QLabel(this);
    m_statusDot->setFixedSize(12, 12);
    m_statusDot->setStyleSheet("background:#666;border-radius:6px;");
    topBar->addWidget(m_statusDot);

    m_statusLabel = new QLabel("Disconnected", this);
    topBar->addWidget(m_statusLabel);

    topBar->addStretch();

    m_userLabel = new QLabel(this);
    m_userLabel->setStyleSheet("color: gray;");
    topBar->addWidget(m_userLabel);

    // No explicit disconnect button — closing the window (X) triggers
    // closeEvent which tears down the WS. Avoids the QDialog default-button
    // gotcha where Enter in the chat input would otherwise click Disconnect.

    root->addLayout(topBar);

    // ---- Main splitter: Rooms+Matches | Chat | Players ----
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Rooms list / in-room view (stacked) + Ongoing Matches in left column
    {
        auto* mid = new QWidget(this);
        auto* lay = new QVBoxLayout(mid);
        lay->setContentsMargins(0, 0, 0, 0);

        m_roomsStack = new QStackedWidget(this);

        // Page 0: Active Rooms list
        auto* roomsGrp = new QGroupBox("Active Rooms", this);
        auto* rlay = new QVBoxLayout(roomsGrp);
        m_roomsTree = new QTreeWidget(this);
        m_roomsTree->setHeaderLabels({ "Name", "Host", "ROM", "Players", "State" });
        m_roomsTree->setRootIsDecorated(false);
        m_roomsTree->setSortingEnabled(true);
        connect(m_roomsTree, &QTreeWidget::itemDoubleClicked,
                this, &RollbackLobbyDialog::onRoomDoubleClicked);
        rlay->addWidget(m_roomsTree);

        auto* roomBtns = new QHBoxLayout;
        m_createRoomBtn = new QPushButton("Create Room", this);
        m_createRoomBtn->setAutoDefault(false);
        m_createRoomBtn->setDefault(false);
        m_quickMatchBtn = new QPushButton("Quick Match", this);
        m_quickMatchBtn->setEnabled(false);
        m_quickMatchBtn->setAutoDefault(false);
        m_quickMatchBtn->setDefault(false);
        m_quickMatchBtn->setToolTip(
            "Auto-match with another player searching right now.\n"
            "Defaults to delay 2 / prediction 7.");
        roomBtns->addWidget(m_createRoomBtn);
        roomBtns->addWidget(m_quickMatchBtn);
        roomBtns->addStretch();
        rlay->addLayout(roomBtns);
        connect(m_createRoomBtn, &QPushButton::clicked, this, &RollbackLobbyDialog::onCreateRoomClicked);
        connect(m_quickMatchBtn, &QPushButton::clicked, this, &RollbackLobbyDialog::onQuickMatchClicked);

        m_roomsStack->addWidget(roomsGrp);

        // Page 1: In-room view
        auto* inRoomGrp = new QGroupBox("Your Room", this);
        buildInRoomView(inRoomGrp);
        m_roomsStack->addWidget(inRoomGrp);

        lay->addWidget(m_roomsStack);

        auto* matchesGrp = new QGroupBox("Ongoing Matches", this);
        auto* mlay = new QVBoxLayout(matchesGrp);
        m_matchesTree = new QTreeWidget(this);
        m_matchesTree->setHeaderLabels({ "Players", "Duration", "Settings" });
        m_matchesTree->setRootIsDecorated(false);
        mlay->addWidget(m_matchesTree);
        lay->addWidget(matchesGrp);

        m_splitter->addWidget(mid);
    }

    // Chat panel (tabbed: Lobby / Room)
    {
        auto* chatGrp = new QGroupBox("Chat", this);
        auto* lay = new QVBoxLayout(chatGrp);

        m_chatTabs = new QTabWidget(this);
        m_chatTabs->setDocumentMode(true);

        m_chatViewLobby = new QTextEdit(this);
        m_chatViewLobby->setReadOnly(true);
        m_chatViewLobby->setLineWrapMode(QTextEdit::WidgetWidth);
        m_chatTabs->addTab(m_chatViewLobby, "Lobby");

        lay->addWidget(m_chatTabs);

        auto* sendRow = new QHBoxLayout;
        m_chatInput = new QLineEdit(this);
        m_chatInput->setPlaceholderText("Type a message and press Enter");
        m_chatInput->setEnabled(false);
        connect(m_chatInput, &QLineEdit::returnPressed, this, &RollbackLobbyDialog::onChatSendClicked);
        sendRow->addWidget(m_chatInput);
        lay->addLayout(sendRow);

        m_splitter->addWidget(chatGrp);
    }

    // Players panel (right side, next to chat)
    {
        auto* players = new QGroupBox("Players", this);
        auto* lay = new QVBoxLayout(players);
        m_playersTree = new QTreeWidget(this);
        m_playersTree->setHeaderLabels({ "User", "State", "Ping" });
        m_playersTree->header()->setStretchLastSection(false);
        m_playersTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_playersTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_playersTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_playersTree->setRootIsDecorated(false);
        m_playersTree->setSortingEnabled(true);
        lay->addWidget(m_playersTree);
        m_splitter->addWidget(players);
    }

    m_splitter->setStretchFactor(0, 4); // rooms + matches
    m_splitter->setStretchFactor(1, 3); // chat
    m_splitter->setStretchFactor(2, 2); // players

    root->addWidget(m_splitter, 1);
}

// ---- Show/close ----

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
    m_userLabel->setText(QString("User: %1").arg(m_username));
    // TODO: collect ROM hashes from RomBrowser when integrating.
    m_client->connectToServer(d.serverUrl(), m_username, {}, QString());
}

// ---- Connection events ----

void RollbackLobbyDialog::onClientStateChanged(LobbyClient::ConnectionState s)
{
    m_statusDot->setStyleSheet(QString("background:%1;border-radius:6px;").arg(stateDotColor(s)));
    m_statusLabel->setText(humanState(s));
    const bool connected = (s == LobbyClient::ConnectionState::Connected);
    m_chatInput->setEnabled(connected);
    if (m_quickMatchBtn)
        m_quickMatchBtn->setEnabled(connected && m_currentRoomId == 0);

    if (s == LobbyClient::ConnectionState::Disconnected)
    {
        m_playersTree->clear();
        m_roomsTree->clear();
        m_seatsTree->clear();
        m_userItems.clear();
        m_roomItems.clear();
        m_currentRoomId = 0;
        m_currentRoomState.clear();
        m_currentMatchId = 0;
        m_awaitingEmulationStart = false;
        m_emulationActive = false;
        m_quickMatchActive = false;
        if (m_quickMatchBtn)
            m_quickMatchBtn->setText("Quick Match");

        if (m_chatViewRoom)
        {
            const int idx = m_chatTabs->indexOf(m_chatViewRoom);
            if (idx >= 0)
                m_chatTabs->removeTab(idx);
            m_chatViewRoom->deleteLater();
            m_chatViewRoom = nullptr;
        }
        m_chatTabs->setCurrentWidget(m_chatViewLobby);
        switchToRoomsView();
    }
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

// ---- Presence ----

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
}

void RollbackLobbyDialog::onUserAdded(quint64 userId)
{
    const auto& users = m_client->users();
    auto it = users.constFind(userId);
    if (it == users.constEnd()) return;

    auto* row = new QTreeWidgetItem(m_playersTree);
    refreshPlayerRow(row, it.value());
    m_userItems.insert(userId, row);
}

void RollbackLobbyDialog::onUserRemoved(quint64 userId)
{
    auto it = m_userItems.find(userId);
    if (it == m_userItems.end()) return;
    delete it.value();
    m_userItems.erase(it);
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
    item->setText(1, stateGlyph(u.state));
    item->setText(2, u.pingToServer > 0 ? QString("~%1ms").arg(u.pingToServer) : "—");
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
    if (state == "playing")    return "Playing";
    if (state == "spectating") return "Spectating";
    if (state == "away")       return "Away";
    return state;
}

// ---- Rooms ----

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
}

void RollbackLobbyDialog::buildInRoomView(QWidget* container)
{
    auto* lay = new QVBoxLayout(container);

    m_inRoomName = new QLabel(this);
    QFont nameFont = m_inRoomName->font();
    nameFont.setBold(true);
    nameFont.setPointSizeF(nameFont.pointSizeF() + 1.0);
    m_inRoomName->setFont(nameFont);
    m_inRoomName->setText("—");
    lay->addWidget(m_inRoomName);

    m_inRoomRom = new QLabel(this);
    m_inRoomRom->setText("—");
    lay->addWidget(m_inRoomRom);

    m_inRoomSettings = new QLabel(this);
    m_inRoomSettings->setStyleSheet("color: gray;");
    m_inRoomSettings->setText("—");
    lay->addWidget(m_inRoomSettings);

    m_seatsTree = new QTreeWidget(this);
    m_seatsTree->setHeaderLabels({ "Seat", "Player" });
    m_seatsTree->setRootIsDecorated(false);
    m_seatsTree->setSelectionMode(QAbstractItemView::NoSelection);
    m_seatsTree->header()->setStretchLastSection(false);
    m_seatsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_seatsTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    lay->addWidget(m_seatsTree, 1);

    auto* btnRow = new QHBoxLayout;
    m_startBtn = new QPushButton("Start Game", this);
    m_startBtn->setEnabled(false);
    m_startBtn->setAutoDefault(false);
    m_startBtn->setDefault(false);
    connect(m_startBtn, &QPushButton::clicked, this, &RollbackLobbyDialog::onStartGameClicked);

    m_dropBtn = new QPushButton("Drop Game", this);
    m_dropBtn->setEnabled(false);
    m_dropBtn->setAutoDefault(false);
    m_dropBtn->setDefault(false);
    m_dropBtn->setToolTip("Stop playing the current match. If you're the host, the match ends for everyone.");
    connect(m_dropBtn, &QPushButton::clicked, this, &RollbackLobbyDialog::onDropGameClicked);

    m_leaveBtn = new QPushButton("Leave Room", this);
    m_leaveBtn->setAutoDefault(false);
    m_leaveBtn->setDefault(false);
    m_leaveBtn->setToolTip("Leave the room and return to the lobby.");
    connect(m_leaveBtn, &QPushButton::clicked, this, &RollbackLobbyDialog::onLeaveRoomClicked);

    btnRow->addWidget(m_startBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_dropBtn);
    btnRow->addWidget(m_leaveBtn);
    lay->addLayout(btnRow);
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
    QString nameCell = r.hasPassword ? QString("🔒 %1").arg(r.name) : r.name;
    if (mine)
        nameCell = QString("★ %1").arg(nameCell);

    item->setText(0, nameCell);
    item->setText(1, r.hostName);
    item->setText(2, r.romName);
    item->setText(3, QString("%1/%2").arg(r.players).arg(r.maxPlayers));
    item->setText(4, r.state);
    item->setData(0, Qt::UserRole, QVariant::fromValue(r.id));

    QFont f = item->font(0);
    f.setBold(mine);
    for (int col = 0; col < item->columnCount(); ++col)
        item->setFont(col, f);
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
        m_createRoomDialog->accept(); // closes the form
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

    // Can't quick-match or create while in a room.
    if (m_createRoomBtn) m_createRoomBtn->setEnabled(false);
    if (m_quickMatchBtn) m_quickMatchBtn->setEnabled(false);

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
            "That game has already started.");
        return;
    }
    if (summary.players >= summary.maxPlayers)
    {
        QMessageBox::information(this, "Room full",
            QString("\"%1\" is already full.").arg(summary.name));
        return;
    }

    QString password;
    if (summary.hasPassword)
    {
        bool ok = false;
        password = QInputDialog::getText(this,
            "Password Required",
            QString("Enter password for \"%1\":").arg(summary.name),
            QLineEdit::Password, QString(), &ok);
        if (!ok) return;
    }

    m_client->joinRoom(roomId, password);
}

void RollbackLobbyDialog::onRoomCreateFailed(const QString& reason)
{
    if (m_createRoomDialog)
        m_createRoomDialog->showCreateFailure(reason);
}

void RollbackLobbyDialog::onRoomStateChanged(const QJsonObject& roomState)
{
    const quint64 roomId = static_cast<quint64>(roomState.value("id").toDouble());
    if (roomId == 0 || roomId != m_currentRoomId)
        return; // ignore stale state for a room we're not in

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

    // Cache for MATCH_BEGIN — server doesn't repeat these settings in the
    // MATCH_BEGIN payload (peers carry only endpoints), so we keep our own copy.
    m_currentRoomGame       = romName;
    m_currentRoomRegion     = romRegion;
    m_currentRoomDelay      = delay;
    m_currentRoomPrediction = prediction;

    // If the room state just transitioned out of in_game while we still have
    // a live local match, the host dropped it for everyone — tear down our
    // own emulation. The dropper itself has m_emulationActive already false
    // (closeMatchRequested ran on their own click), so this branch is a no-op
    // for them and only fires on the surviving seats.
    const QString prevState = m_currentRoomState;
    m_currentRoomState = state;
    if (prevState == "in_game" && state != "in_game" &&
        (m_emulationActive || m_awaitingEmulationStart))
    {
        appendChatLine(CHANNEL_ROOM, "<i>Host ended the match — stopping game.</i>");
        emit closeMatchRequested();
    }

    // Drop Game is only meaningful while we personally have a live match —
    // not just whenever the room is in_game, since after our own drop the
    // room may still be in_game for the others. Track that via the local
    // emulation flags. Leave Room stays enabled throughout.
    if (m_dropBtn)
    {
        m_dropBtn->setEnabled(state == "in_game" &&
            (m_emulationActive || m_awaitingEmulationStart));
        m_dropBtn->setToolTip(
            iAmHost
                ? "Stop the match for everyone in the room."
                : "Drop out of the match. Other players keep playing.");
    }
    if (m_leaveBtn)
    {
        m_leaveBtn->setEnabled(true);
    }

    m_inRoomName->setText(QString("%1%2").arg(iAmHost ? "★ " : "").arg(name));
    if (!romRegion.isEmpty())
        m_inRoomRom->setText(QString("%1 (%2)").arg(romName, romRegion));
    else
        m_inRoomRom->setText(romName);
    m_inRoomSettings->setText(
        QString("Delay %1 · Prediction %2 · %3 players · %4")
            .arg(delay).arg(prediction).arg(maxPlayers).arg(state));

    // Seats: render filled seats then empty placeholders up to maxPlayers.
    m_seatsTree->clear();
    const QJsonArray players = roomState.value("players").toArray();
    for (const auto& v : players)
    {
        const auto p = v.toObject();
        auto* row = new QTreeWidgetItem(m_seatsTree);
        const int slot = p.value("slot").toInt();
        const QString user = p.value("username").toString();
        const quint64 uid = static_cast<quint64>(p.value("userId").toDouble());
        row->setText(0, QString::number(slot));
        row->setText(1, uid == hostId ? QString("%1 (host)").arg(user) : user);
    }
    for (int slot = players.size(); slot < maxPlayers; ++slot)
    {
        auto* row = new QTreeWidgetItem(m_seatsTree);
        row->setText(0, QString::number(slot + 1));
        row->setText(1, "<empty>");
        for (int col = 0; col < 2; ++col)
            row->setForeground(col, QBrush(Qt::gray));
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

    // Drop locally and notify the server. Host vs non-host outcome is decided
    // server-side: host drop dissolves the room for everyone, non-host drop
    // only removes this player. Either way our client gets a ROOM_LEFT back.
    emit closeMatchRequested();
    if (m_currentMatchId != 0)
        m_client->reportMatchFinished(m_currentMatchId);
    appendChatLine(CHANNEL_ROOM, "<i>You dropped from the game.</i>");
}

void RollbackLobbyDialog::onLeaveRoomClicked()
{
    // Leave the room. If we have a live local match, drop it first so
    // emulation tears down cleanly; then send ROOM_LEAVE — MATCH_FINISHED
    // alone keeps us seated under the new semantics, so we must explicitly
    // leave to actually exit the room.
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
    // Only fire if we're actually waiting on a lobby-driven match. Kaillera
    // or other emulation paths also hit on_Emulation_Started in MainWindow.
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
    // The reportMatchConnected API takes a peer userId for future per-peer
    // tracking; for v1 with 2-player only, we use 0 (server treats it as
    // "any peer" since the match state transition is global).
    m_client->reportMatchConnected(m_currentMatchId, 0);
    appendChatLine(CHANNEL_ROOM, "<i>Match started — playing now.</i>");
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
    {
        // MATCH_FINISHED tells the server we dropped from the match. With the
        // new "stay-in-room" semantics, the server clears our match link but
        // we remain seated. Our local m_currentMatchId is cleared on the
        // ROOM_STATE that follows (or via ROOM_LEFT if we then leave).
        m_client->reportMatchFinished(matchId);
    }
    m_currentMatchId = 0;
    // We dropped from the match — disable the Drop button until a new match
    // begins. The user can still click Leave Room to actually exit the room.
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
    // MATCH_PEER_LEFT is informational only — the match keeps running for the
    // remaining seats. Tear-down is signalled by ROOM_LEFT (which the server
    // only sends to us when the room actually dissolves). GekkoNet will
    // detect the dropped peer's UDP timeout and start feeding zeroed input
    // for that actor automatically, so the abandoned slot just goes idle —
    // no explicit "unplug controller" call needed.
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
        ? QString("<i>%1 (P%2) dropped (%3) — controller %2 will go idle.</i>")
              .arg(who).arg(slot).arg(suffix)
        : QString("<i>%1 dropped (%2).</i>").arg(who, suffix);
    appendChatLine(CHANNEL_ROOM, line);
}

void RollbackLobbyDialog::onStartGameClicked()
{
    // TODO: server-side ROOM_START handler not yet implemented; this currently
    // sends the request but won't do anything until we wire MATCH_BEGIN.
    m_client->startRoom();
}

void RollbackLobbyDialog::onRoomLeft()
{
    // If a match was still running at the moment the server dropped us from
    // the room, that means the room dissolved out from under us (host drop,
    // host disconnect, or down to <2 players). Tear down local emulation —
    // MATCH_PEER_LEFT no longer does this, since it now fires for non-fatal
    // peer drops too.
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

    // Re-open the UDP anchor if we'd released it for a match.
    if (m_client) m_client->reopenUdpAnchor();

    // Remove the Room chat tab and snap back to the Lobby tab.
    if (m_chatViewRoom)
    {
        const int idx = m_chatTabs->indexOf(m_chatViewRoom);
        if (idx >= 0)
            m_chatTabs->removeTab(idx);
        m_chatViewRoom->deleteLater();
        m_chatViewRoom = nullptr;
    }
    m_chatTabs->setCurrentWidget(m_chatViewLobby);

    // Swap the middle column back to the rooms list.
    switchToRoomsView();
    if (m_startBtn)  m_startBtn->setEnabled(false);
    if (m_dropBtn)   m_dropBtn->setEnabled(false);
    if (m_leaveBtn)  m_leaveBtn->setEnabled(true);

    // Back in the lobby — re-enable create/quick-match.
    const bool connected = (m_client->state() == LobbyClient::ConnectionState::Connected);
    if (m_createRoomBtn) m_createRoomBtn->setEnabled(connected);
    if (m_quickMatchBtn) m_quickMatchBtn->setEnabled(connected);

    onRoomListChanged(); // re-render to drop highlight
}

// ---- Chat ----

void RollbackLobbyDialog::onChatSendClicked()
{
    const QString text = m_chatInput->text().trimmed();
    if (text.isEmpty()) return;

    // Route to whichever tab is active.
    QString channel = CHANNEL_LOBBY;
    if (m_chatViewRoom && m_chatTabs->currentWidget() == m_chatViewRoom)
        channel = CHANNEL_ROOM;

    m_client->sendChat(channel, text);
    m_chatInput->clear();
}

void RollbackLobbyDialog::onChatMessageReceived(const LobbyClient::ChatMessage& msg)
{
    const auto ts = QDateTime::fromMSecsSinceEpoch(msg.serverTimeMs).toString("hh:mm");
    appendChatLine(msg.channel,
        QString("[%1] %2: %3").arg(ts, msg.fromUsername.toHtmlEscaped(), msg.message.toHtmlEscaped()));
}

void RollbackLobbyDialog::appendChatLine(const QString& channel, const QString& text)
{
    QTextEdit* target = m_chatViewLobby;
    if (channel == CHANNEL_ROOM && m_chatViewRoom)
        target = m_chatViewRoom;
    if (target)
        target->append(text);
}

// ---- Match handoff ----

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
            : QString(" (searching...)");
        m_quickMatchBtn->setText("Cancel Quick Match" + suffix);
        if (m_createRoomBtn)
            m_createRoomBtn->setEnabled(false);
    }
    else
    {
        m_quickMatchBtn->setText("Quick Match");
        if (m_createRoomBtn)
            m_createRoomBtn->setEnabled(m_client->state() == LobbyClient::ConnectionState::Connected
                                        && m_currentRoomId == 0);
    }
}

void RollbackLobbyDialog::onMatchBegin(quint64 matchId, const QList<LobbyClient::LobbyMatchPeer>& peers)
{
    // Surface in both lobby and room channels so the message is wherever the
    // user happens to be looking.
    const QString line = QString("<b>Match #%1 starting with %2 player(s)</b>").arg(matchId).arg(peers.size());
    appendChatLine(CHANNEL_LOBBY, line);
    appendChatLine(CHANNEL_ROOM,  line);

    m_currentMatchId = matchId;
    m_awaitingEmulationStart = true;

    // Disable start; enable Drop now — MATCH_BEGIN arrives *after* the
    // ROOM_STATE that flipped us to in_game, so onRoomStateChanged ran before
    // the awaiting flag was set and left Drop disabled. Set it directly here.
    if (m_startBtn)  m_startBtn->setEnabled(false);
    if (m_dropBtn)   m_dropBtn->setEnabled(true);
    if (m_inRoomSettings)
        m_inRoomSettings->setText("Connecting to peers...");

    // Pick the local peer; collect every non-self peer with its slot/ip/port.
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
        appendChatLine(CHANNEL_ROOM, "<span style='color:red;'>MATCH_BEGIN missing local or remote peer — aborting</span>");
        return;
    }

    // Free the anchor socket so GekkoNet can bind the same local port for
    // gameplay (keeps NAT mappings consistent with what the server reported).
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
