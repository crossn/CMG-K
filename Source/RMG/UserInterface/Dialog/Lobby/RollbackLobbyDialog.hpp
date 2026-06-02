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

class QFrame;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QTextEdit;
class QLineEdit;
class QPushButton;
class QSplitter;
class QTabWidget;
class QStackedWidget;
class QComboBox;

namespace UserInterface
{
namespace Dialog
{

// Top-level rollback lobby UI. Owns a LobbyClient and renders presence /
// rooms / chat. Modeless — opened from the main window menu.
//
// Visual approach matches the Kaillera launcher: palette-based colors,
// bold section-header labels with hairline dividers (no bordered group
// boxes), native widget styling. Sizes standardized across the dialog
// via the constants block in the .cpp.
class RollbackLobbyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RollbackLobbyDialog(QWidget* parent = nullptr);
    ~RollbackLobbyDialog() override;

    // MainWindow refreshes this on every open so the Create Room dropdown
    // reflects the user's latest ROM library.
    void setRomLibrary(const QMap<QString, CoreRomSettings>& roms);

    // MainWindow calls these from its existing emulation-thread signal slots
    // so the lobby server stays in sync with the actual game lifecycle.
    void notifyEmulationStarted();
    void notifyEmulationFinished();

    // Sends a room-channel chat message — used by the in-game chat overlay so
    // typed messages reach the room while a match is running.
    void sendRoomChat(const QString& message);

signals:
    // Fired when the server has issued MATCH_BEGIN. Each entry in remotePeers
    // is pre-formatted as "<slot>,<ip>,<port>" — matches the LOBBY| address
    // peer-entry format consumed by CoreStartEmulation.
    void matchReady(QString gameName, QStringList remotePeers,
                    int localPort, int localPlayer,
                    int frameDelay, int predictionWindow);

    // Fired when the user clicks "Close Game" mid-match or when a peer drops.
    void closeMatchRequested();

    // Fired for every *remote* room-channel chat message (own messages are
    // filtered out — the overlay echoes those locally). MainWindow routes this
    // to the in-game chat overlay.
    void roomChatReceived(QString nickname, QString message);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onConnectClicked();
    void onClientStateChanged(LobbyClient::ConnectionState s);
    void onHelloFailed(const QString& reason);
    void onConnectError(const QString& msg);

    void onPresenceFull();
    void onUserAdded(quint64 userId);
    void onUserRemoved(quint64 userId);
    void onUserUpdated(quint64 userId);

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

    void onQuickMatchClicked();
    void onQuickMatchStatusChanged(bool searching, int queueSize);

    void onLeaveRoomClicked();
    void onDropGameClicked();
    void onStartGameClicked();

    void onMatchPeerLeft(quint64 matchId, quint64 userId, int slot, const QString& reason);

    void onChatSendClicked();
    void onChatMessageReceived(const LobbyClient::ChatMessage& msg);
    void onMatchBegin(quint64 matchId, const QList<LobbyClient::LobbyMatchPeer>& peers);

    // Periodic probe driver: while in a room, requests a fresh ping
    // measurement from each seated peer (skipping self). Cadence is set
    // by m_pingProbeTimer's interval.
    void onPingProbeTick();
    void onPingMeasured(quint64 userId, int rttMs);

private:
    void buildUi();
    QWidget* buildConnectView();   // inline username/connect screen (index 0)
    QWidget* buildLobbyView();     // marquee + splitter (index 1)
    QWidget* buildMarquee();
    QWidget* buildBrowseView();
    QWidget* buildInRoomView();
    QWidget* buildChatColumn();
    QWidget* buildPlayersColumn();
    void     applyStylesheet();

    // Repopulate the browse-view ROM picker from m_roms (RMG-K library).
    void     populateBrowseRoms();

    // Swap the top-level stack between the connect screen and the live lobby.
    void     showConnectView(const QString& statusMessage = QString());
    void     showLobbyView();
    QString  prefillUsername() const;

    void refreshPlayerRow(QTreeWidgetItem* item, const LobbyClient::LobbyUser& u);
    void refreshRoomRow(QTreeWidgetItem* item, const LobbyClient::LobbyRoomSummary& r);
    // Ticks the Ongoing Matches "Duration" cells once a second.
    void updateMatchDurations();

    QString stateGlyph(const QString& state) const;
    void    appendChatLine(const QString& channel, const QString& text);
    void    appendChatSystemLine(const QString& channel, const QString& text);
    void    switchToRoomsView();
    void    switchToInRoomView();
    void    enterRoom(quint64 roomId, const QString& greetingChatLine);
    void    updateStatusIndicator(LobbyClient::ConnectionState s);
    void    updateServerMeta();
    void    updateInRoomBanner();   // refresh "you're in: X" banner in browse view

    // Host-only auto delay/prediction. worstSeatPingMs() = max measured RTT
    // over seated peers (-1 if none). applyHostRoomSettings() resolves the
    // Auto selections and pushes the concrete values via the lobby client.
    int     worstSeatPingMs() const;
    void    applyHostRoomSettings(bool force);

    // Seat row API — 4 fixed slots rendered as a vertical player list.
    // Empty rows show a ○ dot + "Waiting…"; filled rows show ● + name + meta.
    // Only rows 1..maxPlayers are visible (others hidden via setVisible).
    struct SeatRow
    {
        QWidget* row       = nullptr;
        QLabel*  dotLabel  = nullptr;     // ● filled, ○ empty
        QLabel*  slotLabel = nullptr;     // "P1"
        QLabel*  nameLabel = nullptr;     // username or "Waiting…"
        QLabel*  metaLabel = nullptr;     // "host · 12ms" — right-aligned
        bool     isHost    = false;
        quint64  userId    = 0;           // seated user, 0 when empty
    };
    void buildSeatRow(SeatRow& row, int slotIdx, QWidget* parent);
    void renderSeatEmpty(SeatRow& row);
    void renderSeatFilled(SeatRow& row, const QString& username, bool isHost,
                          bool isSelf, int pingMs);

    LobbyClient* m_client = nullptr;

    // ── Top-level stack: connect screen (0) ↔ live lobby (1) ──
    QStackedWidget* m_topStack            = nullptr;
    QLineEdit*      m_connectUsernameEdit = nullptr;
    QPushButton*    m_connectButton       = nullptr;
    QLabel*         m_connectStatusLabel  = nullptr;

    // ── Marquee bar ──
    QFrame*  m_marquee     = nullptr;
    QLabel*  m_brandLabel  = nullptr;
    QLabel*  m_statusLed   = nullptr;
    QLabel*  m_statusText  = nullptr;
    QLabel*  m_serverMeta  = nullptr;
    QLabel*  m_userLabel   = nullptr;

    // ── Main panels ──
    QSplitter*   m_splitter      = nullptr;
    QTreeWidget* m_playersTree   = nullptr;
    QTreeWidget* m_roomsTree     = nullptr;
    QTreeWidget* m_matchesTree   = nullptr;
    QTimer*      m_matchDurationTimer = nullptr;
    QTabWidget*  m_chatTabs      = nullptr;
    QTextEdit*   m_chatViewLobby = nullptr;
    QTextEdit*   m_chatViewRoom  = nullptr; // nullptr when not in a room
    QLineEdit*   m_chatInput     = nullptr;

    // ── Stacked browse / in-room view ──
    QStackedWidget* m_roomsStack = nullptr;

    // ── Persistent "you're in a room" banner shown in browse view when
    //    the user is in a room (so they can flip back without losing their seat). ──
    QFrame*    m_inRoomBanner    = nullptr;
    QLabel*    m_bannerText      = nullptr;
    QPushButton* m_bannerReturn  = nullptr;

    // In-room header card
    QLabel*    m_roomTitle      = nullptr;   // ROM title (large)
    QLabel*    m_roomSubtitle   = nullptr;   // host · region · max
    QLabel*    m_roomStateLabel = nullptr;   // "Waiting" / "In Game"
    QLabel*    m_roomMetaLabel  = nullptr;   // Seats 2/4 · Region NTSC

    // Host-editable rollback settings (delay / prediction). Disabled for
    // non-hosts and mid-match. Combo index maps 1:1 to the integer value
    // (0..9), so currentIndex() is the wire value.
    QComboBox* m_delayCombo      = nullptr;
    QComboBox* m_predictionCombo = nullptr;
    bool       m_suppressSettingsSignal = false;  // guard against ROOM_STATE → setCurrentIndex echo

    // Seat rows (always 4 — slots beyond maxPlayers are hidden)
    SeatRow    m_seats[4];

    // Drives PING_PROBE_REQUEST → UDP PROBE → PROBE_REPLY refresh cycle for
    // each peer seated in the current room. Started when a room is entered,
    // stopped when it's left. Inactive (and unused) outside rooms.
    QTimer*    m_pingProbeTimer = nullptr;

    // In-room action bar
    QPushButton* m_startBtn      = nullptr;
    QPushButton* m_dropBtn       = nullptr;
    QPushButton* m_leaveBtn      = nullptr;

    // Hero / browse-view action buttons
    QPushButton* m_quickMatchBtn = nullptr;   // primary CTA (blue)
    QPushButton* m_createRoomBtn = nullptr;
    QComboBox*   m_browseRomCombo = nullptr;   // library game picker (feeds Create Room)

    QHash<quint64, QTreeWidgetItem*> m_userItems;
    QHash<quint64, QTreeWidgetItem*> m_roomItems;

    class CreateRoomDialog* m_createRoomDialog = nullptr;

    QString  m_username;
    QString  m_serverUrl;
    quint64  m_currentRoomId = 0;

    QString m_currentRoomGame;
    QString m_currentRoomRegion;
    QString m_currentRoomState;
    int     m_currentRoomDelay      = 2;
    int     m_currentRoomPrediction = 7;
    quint64 m_currentMatchId        = 0;

    // Host-only "Auto" selections for the in-room dropdowns. Delay-auto is
    // ping-based; prediction-auto is a fixed 7. Default on so a fresh host
    // gets sensible tuning without thinking about it.
    bool    m_delayAuto      = true;
    bool    m_predictionAuto = true;

    bool m_awaitingEmulationStart = false;
    bool m_emulationActive        = false;
    bool m_quickMatchActive       = false;

    QMap<QString, CoreRomSettings> m_roms;
};

} // namespace Dialog
} // namespace UserInterface

#endif // NETPLAY
#endif // ROLLBACKLOBBYDIALOG_HPP
