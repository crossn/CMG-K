/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 */
#ifndef ROLLBACKLOBBYDIALOG_HPP
#define ROLLBACKLOBBYDIALOG_HPP

#ifdef NETPLAY

#include "LobbyClient.hpp"

#include <QDialog>
#include <QHash>
#include <QMap>
#include <QStringList>

#include <RMG-Core/RomSettings.hpp>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QTextEdit;
class QLineEdit;
class QPushButton;
class QSplitter;
class QStatusBar;
class QTabWidget;
class QStackedWidget;

namespace UserInterface
{
namespace Dialog
{

// Top-level rollback lobby UI. Owns a LobbyClient and renders
// presence / rooms / chat. Modeless — opened from the main window menu.
class RollbackLobbyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RollbackLobbyDialog(QWidget* parent = nullptr);
    ~RollbackLobbyDialog() override;

    // MainWindow refreshes this on every open so the Create Room dropdown
    // reflects the user's latest ROM library.
    void setRomLibrary(const QMap<QString, CoreRomSettings>& roms) { m_roms = roms; }

    // MainWindow calls these from its existing emulation-thread signal slots
    // so the lobby server stays in sync with the actual game lifecycle.
    // No-op when there's no current lobby-driven match.
    void notifyEmulationStarted();
    void notifyEmulationFinished();

signals:
    // Fired when the server has issued MATCH_BEGIN. Carries every non-local
    // peer's endpoint (slot/ip/port) so 3-/4-player sessions can wire each
    // remote actor to its own peer address. Each entry in remotePeers is
    // pre-formatted as "<slot>,<ip>,<port>" — matches the LOBBY| address
    // peer-entry format consumed by CoreStartEmulation.
    void matchReady(QString gameName, QStringList remotePeers,
                    int localPort, int localPlayer,
                    int frameDelay, int predictionWindow);

    // Fired when the user clicks "Close Game" mid-match or when a peer drops.
    // MainWindow connects this to stop emulation cleanly.
    void closeMatchRequested();

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    // Connection
    void promptConnect();
    void onClientStateChanged(LobbyClient::ConnectionState s);
    void onHelloFailed(const QString& reason);
    void onConnectError(const QString& msg);

    // Presence
    void onPresenceFull();
    void onUserAdded(quint64 userId);
    void onUserRemoved(quint64 userId);
    void onUserUpdated(quint64 userId);

    // Rooms
    void onRoomListChanged();
    void onCreateRoomClicked();
    void onRoomDoubleClicked(QTreeWidgetItem* item, int column);
    void onRoomCreateRequested();
    void onRoomCreated(quint64 roomId);
    void onRoomCreateFailed(const QString& reason);
    void onRoomJoinOk(quint64 roomId);
    void onRoomJoinFailed(const QString& reason);
    void onRoomLeft();
    void onRoomStateChanged(const QJsonObject& roomState);

    // Quick match
    void onQuickMatchClicked();
    void onQuickMatchStatusChanged(bool searching, int queueSize);

    // In-room actions
    void onReadyClicked();
    void onLeaveRoomClicked();
    void onStartGameClicked();

    // Match
    void onMatchPeerLeft(quint64 matchId, quint64 userId, const QString& reason);

    // Chat
    void onChatSendClicked();
    void onChatMessageReceived(const LobbyClient::ChatMessage& msg);

    // Match
    void onMatchBegin(quint64 matchId, const QList<LobbyClient::LobbyMatchPeer>& peers);

private:
    void buildUi();
    void buildInRoomView(class QWidget* container);
    void refreshPlayerRow(QTreeWidgetItem* item, const LobbyClient::LobbyUser& u);
    void refreshRoomRow(QTreeWidgetItem* item, const LobbyClient::LobbyRoomSummary& r);
    QString stateGlyph(const QString& state) const;
    void appendChatLine(const QString& channel, const QString& text);
    void switchToRoomsView();
    void switchToInRoomView();
    void enterRoom(quint64 roomId, const QString& greetingChatLine);

    LobbyClient* m_client = nullptr;

    // Top connection bar
    QLabel*      m_statusDot   = nullptr;
    QLabel*      m_statusLabel = nullptr;
    QLabel*      m_userLabel   = nullptr;

    // Main panels
    QSplitter*   m_splitter    = nullptr;
    QTreeWidget* m_playersTree    = nullptr;
    QTreeWidget* m_roomsTree      = nullptr;
    QTreeWidget* m_matchesTree    = nullptr;
    QTabWidget*  m_chatTabs       = nullptr;
    QTextEdit*   m_chatViewLobby  = nullptr;
    QTextEdit*   m_chatViewRoom   = nullptr; // nullptr when not in a room
    QLineEdit*   m_chatInput      = nullptr;

    // Stacked rooms-list / in-room views in the middle column
    QStackedWidget* m_roomsStack   = nullptr;
    QTreeWidget*    m_seatsTree    = nullptr;
    QLabel*         m_inRoomName   = nullptr;
    QLabel*         m_inRoomRom    = nullptr;
    QLabel*         m_inRoomSettings = nullptr;
    QPushButton*    m_readyBtn     = nullptr;
    QPushButton*    m_startBtn     = nullptr;
    QPushButton*    m_leaveBtn     = nullptr;
    bool            m_localReady   = false; // last toggle state we sent

    // Bottom actions
    QPushButton* m_createRoomBtn  = nullptr;
    QPushButton* m_quickMatchBtn  = nullptr;

    // Mapping user/room id → tree item for cheap updates
    QHash<quint64, QTreeWidgetItem*> m_userItems;
    QHash<quint64, QTreeWidgetItem*> m_roomItems;

    // Active modal create-room form (nullptr when closed)
    class CreateRoomDialog* m_createRoomDialog = nullptr;

    // Username we authed with — used as default for room name etc.
    QString m_username;

    // The room id we currently belong to (host or joined). 0 if not in a room.
    quint64 m_currentRoomId = 0;

    // Cached room settings from ROOM_STATE — needed when MATCH_BEGIN fires so
    // we can construct the session-start parameters without re-querying.
    QString m_currentRoomGame;
    QString m_currentRoomRegion;
    QString m_currentRoomState; // "waiting" / "in_game" / etc.
    int     m_currentRoomDelay      = 2;
    int     m_currentRoomPrediction = 7;
    quint64 m_currentMatchId        = 0;

    // Lifecycle tracking for the lobby↔emulation handoff:
    //   awaiting: matchReady has been emitted, waiting for emu to actually start
    //   active:   emu started, server has been told (MATCH_CONNECTED sent)
    bool m_awaitingEmulationStart = false;
    bool m_emulationActive        = false;

    // Whether we're currently in the matchmaker queue.
    bool m_quickMatchActive = false;

    // ROM library, fed by MainWindow so the Create Room form can populate
    // the dropdown with real entries instead of placeholder strings.
    QMap<QString, CoreRomSettings> m_roms;
};

} // namespace Dialog
} // namespace UserInterface

#endif // NETPLAY
#endif // ROLLBACKLOBBYDIALOG_HPP
