/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 */
#ifdef NETPLAY

#include "LobbyClient.hpp"

#include <QWebSocket>
#include <QUdpSocket>
#include <QAbstractSocket>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonValue>
#include <QHostAddress>
#include <QHostInfo>
#include <QDateTime>
#include <QtEndian>
#include <QRandomGenerator>
#include <QDebug>
#include <cstring>

using namespace UserInterface::Dialog;

namespace
{
    constexpr int HEARTBEAT_INTERVAL_MS  = 15'000;
    constexpr int UDP_KEEPALIVE_INTERVAL = 20'000;

    constexpr char ANCHOR_MAGIC[4] = { 'R', 'M', 'G', 'K' };
    constexpr quint8 ANCHOR_OP_REGISTER    = 0x01;
    constexpr quint8 ANCHOR_OP_KEEPALIVE   = 0x02;
    constexpr quint8 ANCHOR_OP_PUNCH       = 0x03;
    constexpr quint8 ANCHOR_OP_PROBE       = 0x04; // client → peer: ping request
    constexpr quint8 ANCHOR_OP_PROBE_REPLY = 0x05; // peer → client: ping echo

    // Burst size for NAT punch-through. Defends against single-packet loss
    // and brief mapping-creation latency on consumer routers — NOT against
    // simultaneity failures (those need retries, which we don't do here yet).
    constexpr int ANCHOR_PUNCH_BURST = 10;

    // PROBE/PROBE_REPLY packet: [magic(4) | op(1) | senderUserId(8) | nonce(8)]
    constexpr int PROBE_PACKET_SIZE = 4 + 1 + 8 + 8;
} // namespace

LobbyClient::LobbyClient(QObject* parent)
    : QObject(parent)
{
    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_ws, &QWebSocket::connected,           this, &LobbyClient::onWsConnected);
    connect(m_ws, &QWebSocket::disconnected,        this, &LobbyClient::onWsDisconnected);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &LobbyClient::onWsTextMessageReceived);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(m_ws, &QWebSocket::errorOccurred,       this, &LobbyClient::onWsErrorOccurred);
#else
    connect(m_ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &LobbyClient::onWsErrorOccurred);
#endif

    m_udp = new QUdpSocket(this);
    connect(m_udp, &QUdpSocket::readyRead, this, &LobbyClient::onUdpReadyRead);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(HEARTBEAT_INTERVAL_MS);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &LobbyClient::onHeartbeatTimer);

    m_udpKeepaliveTimer = new QTimer(this);
    m_udpKeepaliveTimer->setInterval(UDP_KEEPALIVE_INTERVAL);
    connect(m_udpKeepaliveTimer, &QTimer::timeout, this, &LobbyClient::onUdpKeepaliveTimer);
}

LobbyClient::~LobbyClient()
{
    disconnectFromServer();
}

void LobbyClient::connectToServer(const QString& wsUrl, const QString& username,
                                   const QStringList& romHashes, const QString& udpAddr)
{
    if (m_state != ConnectionState::Disconnected && m_state != ConnectionState::Failed)
    {
        return;
    }

    m_pendingUsername  = username;
    m_pendingRomHashes = romHashes;
    m_selfUserId       = 0;
    m_users.clear();
    m_rooms.clear();

    QUrl url(wsUrl);
    if (!udpAddr.isEmpty())
    {
        const int sep = udpAddr.lastIndexOf(':');
        if (sep > 0)
        {
            m_udpAnchorHost = udpAddr.left(sep);
            m_udpAnchorPort = static_cast<quint16>(udpAddr.mid(sep + 1).toUInt());
        }
        else
        {
            m_udpAnchorHost = udpAddr;
            m_udpAnchorPort = 6364;
        }
    }
    else
    {
        m_udpAnchorHost = url.host();
        m_udpAnchorPort = 6364;
    }

    setState(ConnectionState::Connecting);
    m_ws->open(url);
}

void LobbyClient::disconnectFromServer()
{
    m_heartbeatTimer->stop();
    m_udpKeepaliveTimer->stop();
    if (m_ws && m_ws->state() != QAbstractSocket::UnconnectedState)
    {
        m_ws->close();
    }
    if (m_udp)
    {
        m_udp->close();
    }
    setState(ConnectionState::Disconnected);
}

void LobbyClient::setState(ConnectionState s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(m_state);
}

void LobbyClient::sendEnvelope(const QString& type, const QJsonObject& data, const QString& id)
{
    if (m_ws->state() != QAbstractSocket::ConnectedState)
        return;

    QJsonObject env;
    env["type"] = type;
    if (!id.isEmpty())
        env["id"] = id;
    if (!data.isEmpty())
        env["data"] = data;

    const QByteArray payload = QJsonDocument(env).toJson(QJsonDocument::Compact);
    m_ws->sendTextMessage(QString::fromUtf8(payload));
}

// -------- WebSocket lifecycle --------

void LobbyClient::onWsConnected()
{
    setState(ConnectionState::Authenticating);

    QJsonObject data;
    data["username"]      = m_pendingUsername;
    data["clientVersion"] = "rmgk-dev"; // TODO: pull real version
    QJsonArray romArr;
    for (const auto& h : m_pendingRomHashes)
        romArr.append(h);
    data["romHashes"] = romArr;
    if (!m_pendingLocalIp.isEmpty())
        data["localIp"] = m_pendingLocalIp;

    sendEnvelope("HELLO", data, "hello-1");
}

void LobbyClient::onWsDisconnected()
{
    m_heartbeatTimer->stop();
    m_udpKeepaliveTimer->stop();
    setState(ConnectionState::Disconnected);
}

void LobbyClient::onWsErrorOccurred()
{
    emit connectError(m_ws->errorString());
    setState(ConnectionState::Failed);
}

void LobbyClient::onWsTextMessageReceived(const QString& msg)
{
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(msg.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
        qWarning() << "lobby: bad JSON" << err.errorString();
        return;
    }
    handleEnvelope(doc.object());
}

void LobbyClient::handleEnvelope(const QJsonObject& env)
{
    const QString type = env.value("type").toString();
    const QJsonObject data = env.value("data").toObject();

    if      (type == "HELLO_OK")             handleHelloOk(data);
    else if (type == "HELLO_FAIL")           handleHelloFail(data);
    else if (type == "HEARTBEAT_ACK")        handleHeartbeatAck(data);
    else if (type == "PRESENCE_FULL")        handlePresenceFull(data);
    else if (type == "PRESENCE_DELTA")       handlePresenceDelta(data);
    else if (type == "ROOM_LIST")            handleRoomList(data);
    else if (type == "ROOM_CREATED")         handleRoomCreated(data);
    else if (type == "ROOM_CREATE_FAIL")     handleRoomCreateFail(data);
    else if (type == "ROOM_STATE")           handleRoomState(data);
    else if (type == "ROOM_JOIN_OK")         handleRoomJoinOk(data);
    else if (type == "ROOM_JOIN_FAIL")       handleRoomJoinFail(data);
    else if (type == "ROOM_LEFT")            handleRoomLeft(data);
    else if (type == "CHAT_MSG")             handleChatMsg(data);
    else if (type == "CHAT_HISTORY_REPLY")   handleChatHistoryReply(data);
    else if (type == "PING_PROBE_REPLY")     handlePingProbeReply(data);
    else if (type == "MATCH_BEGIN")          handleMatchBegin(data);
    else if (type == "MATCH_PEER_LEFT")      handleMatchPeerLeft(data);
    else if (type == "QUICK_MATCH_STATUS")   handleQuickMatchStatus(data);
    else if (type == "SPECTATE_BEGIN")       handleSpectateBegin(data);
    else if (type == "SPECTATE_DATA")        handleSpectateData(data);
    else if (type == "SPECTATE_END")         handleSpectateEnd(data);
    else if (type == "SPECTATE_FAIL")        handleSpectateFail(data);
    else qDebug() << "lobby: unknown message type" << type;
}

// -------- Specific handlers --------

void LobbyClient::handleHelloOk(const QJsonObject& data)
{
    m_selfUserId  = static_cast<quint64>(data.value("userId").toDouble());
    m_observedIp  = data.value("observedIp").toString();
    m_region      = data.value("region").toString();

    const QString udpAnchor = data.value("udpAnchor").toString();
    if (!udpAnchor.isEmpty() && udpAnchor != "TODO:6364")
    {
        const int sep = udpAnchor.lastIndexOf(':');
        if (sep > 0)
        {
            m_udpAnchorHost = udpAnchor.left(sep);
            m_udpAnchorPort = static_cast<quint16>(udpAnchor.mid(sep + 1).toUInt());
        }
    }

    setState(ConnectionState::Connected);
    m_heartbeatTimer->start();
    initiateUdpAnchor();
}

void LobbyClient::handleHelloFail(const QJsonObject& data)
{
    const QString reason = data.value("reason").toString();
    emit helloFailed(reason);
    setState(ConnectionState::Failed);
    if (m_ws)
        m_ws->close();
}

void LobbyClient::handleHeartbeatAck(const QJsonObject& data)
{
    Q_UNUSED(data);
    // TODO: use serverTime drift if we care
}

void LobbyClient::handlePresenceFull(const QJsonObject& data)
{
    m_users.clear();
    for (const auto& v : data.value("users").toArray())
    {
        const auto u = parsePresenceUser(v.toObject());
        m_users.insert(u.id, u);
    }
    emit presenceFull();
}

void LobbyClient::handlePresenceDelta(const QJsonObject& data)
{
    for (const auto& v : data.value("added").toArray())
    {
        const auto u = parsePresenceUser(v.toObject());
        m_users.insert(u.id, u);
        emit userAdded(u.id);
    }
    for (const auto& v : data.value("updated").toArray())
    {
        const auto u = parsePresenceUser(v.toObject());
        m_users.insert(u.id, u);
        emit userUpdated(u.id);
    }
    for (const auto& v : data.value("removed").toArray())
    {
        const quint64 id = static_cast<quint64>(v.toDouble());
        m_users.remove(id);
        emit userRemoved(id);
    }
}

void LobbyClient::handleRoomList(const QJsonObject& data)
{
    m_rooms.clear();
    for (const auto& v : data.value("rooms").toArray())
    {
        const auto o = v.toObject();
        LobbyRoomSummary r;
        r.id          = static_cast<quint64>(o.value("id").toDouble());
        r.name        = o.value("name").toString();
        r.hostId      = static_cast<quint64>(o.value("hostId").toDouble());
        r.hostName    = o.value("hostName").toString();
        const auto rom = o.value("rom").toObject();
        r.romName     = rom.value("name").toString();
        r.romMd5      = rom.value("md5").toString();
        r.players     = o.value("players").toInt();
        r.maxPlayers  = o.value("maxPlayers").toInt();
        r.state       = o.value("state").toString();
        r.hasPassword = o.value("hasPassword").toBool();
        r.startedAtMs = static_cast<qint64>(o.value("startedAt").toDouble());
        r.broadcasting = o.value("broadcasting").toBool();
        r.matchId      = static_cast<quint64>(o.value("matchId").toDouble());
        for (const auto& n : o.value("playerNames").toArray())
            r.playerNames << n.toString();
        m_rooms.insert(r.id, r);
    }
    emit roomListChanged();
}

void LobbyClient::handleRoomCreated(const QJsonObject& data)
{
    emit roomCreated(static_cast<quint64>(data.value("roomId").toDouble()));
}

void LobbyClient::handleRoomCreateFail(const QJsonObject& data)
{
    emit roomCreateFailed(data.value("reason").toString());
}

void LobbyClient::handleRoomLeft(const QJsonObject& data)
{
    emit roomLeft(data.value("reason").toString());
}

void LobbyClient::handleRoomState(const QJsonObject& data)
{
    emit roomStateChanged(data);
}

void LobbyClient::handleRoomJoinOk(const QJsonObject& data)
{
    emit roomJoinOk(static_cast<quint64>(data.value("id").toDouble()));
    emit roomStateChanged(data);
}

void LobbyClient::handleRoomJoinFail(const QJsonObject& data)
{
    emit roomJoinFailed(data.value("reason").toString());
}

void LobbyClient::handleChatMsg(const QJsonObject& data)
{
    ChatMessage m;
    m.channel        = data.value("channel").toString();
    m.fromUserId     = static_cast<quint64>(data.value("fromUserId").toDouble());
    m.fromUsername   = data.value("fromUsername").toString();
    m.message        = data.value("message").toString();
    m.serverTimeMs   = static_cast<qint64>(data.value("serverTime").toDouble());
    emit chatMessageReceived(m);
}

void LobbyClient::handleChatHistoryReply(const QJsonObject& data)
{
    const QString channel = data.value("channel").toString();
    QList<ChatMessage> out;
    for (const auto& v : data.value("messages").toArray())
    {
        const auto o = v.toObject();
        ChatMessage m;
        m.channel       = o.value("channel").toString();
        m.fromUserId    = static_cast<quint64>(o.value("fromUserId").toDouble());
        m.fromUsername  = o.value("fromUsername").toString();
        m.message       = o.value("message").toString();
        m.serverTimeMs  = static_cast<qint64>(o.value("serverTime").toDouble());
        out.append(m);
    }
    emit chatHistoryReceived(channel, out);
}

void LobbyClient::handlePingProbeReply(const QJsonObject& data)
{
    const quint64 uid = static_cast<quint64>(data.value("targetUserId").toDouble());
    const QString endpoint = data.value("targetEndpoint").toString();
    emit pingProbeReply(uid, endpoint);

    if (endpoint.isEmpty() || !m_udp ||
        m_udp->state() == QAbstractSocket::UnconnectedState ||
        m_selfUserId == 0)
        return;

    const int colon = endpoint.lastIndexOf(':');
    if (colon <= 0)
        return;
    const QHostAddress addr(endpoint.left(colon));
    const quint16 port = static_cast<quint16>(endpoint.mid(colon + 1).toUInt());
    if (addr.isNull() || port == 0)
        return;

    // Fire a UDP PROBE at the peer's anchor socket. The peer recognises the
    // opcode and echoes back a PROBE_REPLY; we match by nonce to compute RTT.
    const quint64 nonce = QRandomGenerator::global()->generate64();

    QByteArray pkt;
    pkt.reserve(PROBE_PACKET_SIZE);
    pkt.append(ANCHOR_MAGIC, 4);
    pkt.append(static_cast<char>(ANCHOR_OP_PROBE));
    const quint64 selfIdBE = qToBigEndian(m_selfUserId);
    pkt.append(reinterpret_cast<const char*>(&selfIdBE), sizeof(selfIdBE));
    const quint64 nonceBE = qToBigEndian(nonce);
    pkt.append(reinterpret_cast<const char*>(&nonceBE), sizeof(nonceBE));

    ProbeInFlight in;
    in.targetUserId = uid;
    in.sendMs       = QDateTime::currentMSecsSinceEpoch();
    m_pendingProbes.insert(nonce, in);

    m_udp->writeDatagram(pkt, addr, port);
}

void LobbyClient::handleMatchBegin(const QJsonObject& data)
{
    const quint64 matchId = static_cast<quint64>(data.value("matchId").toDouble());
    QList<LobbyMatchPeer> peers;
    for (const auto& v : data.value("peers").toArray())
    {
        const auto o = v.toObject();
        LobbyMatchPeer p;
        p.userId     = static_cast<quint64>(o.value("userId").toDouble());
        p.username   = o.value("username").toString();
        p.publicIp   = o.value("publicIp").toString();
        p.publicPort = static_cast<quint16>(o.value("publicPort").toInt());
        p.localIp    = o.value("localIp").toString();
        p.slot       = o.value("slot").toInt();
        peers.append(p);
    }
    emit matchBegin(matchId, peers);
}

void LobbyClient::handleMatchPeerLeft(const QJsonObject& data)
{
    const quint64 matchId = static_cast<quint64>(data.value("matchId").toDouble());
    const quint64 userId  = static_cast<quint64>(data.value("userId").toDouble());
    const QString reason  = data.value("reason").toString();
    const int slot        = data.value("slot").toInt(0);
    emit matchPeerLeft(matchId, userId, slot, reason);
}

void LobbyClient::handleQuickMatchStatus(const QJsonObject& data)
{
    emit quickMatchStatus(data.value("searching").toBool(), data.value("queueSize").toInt());
}

void LobbyClient::handleSpectateBegin(const QJsonObject& data)
{
    emit spectateBegan(static_cast<quint64>(data.value("matchId").toDouble()));
}

void LobbyClient::handleSpectateData(const QJsonObject& data)
{
    const quint64 matchId = static_cast<quint64>(data.value("matchId").toDouble());
    const QByteArray raw = QByteArray::fromBase64(data.value("data").toString().toLatin1());
    if (!raw.isEmpty())
        emit spectateData(matchId, raw);
}

void LobbyClient::handleSpectateEnd(const QJsonObject& data)
{
    emit spectateEnded(static_cast<quint64>(data.value("matchId").toDouble()),
                       data.value("reason").toString());
}

void LobbyClient::handleSpectateFail(const QJsonObject& data)
{
    emit spectateFailed(static_cast<quint64>(data.value("matchId").toDouble()),
                        data.value("reason").toString());
}

// -------- Heartbeat --------

void LobbyClient::onHeartbeatTimer()
{
    if (m_state != ConnectionState::Connected)
        return;
    QJsonObject data;
    // TODO: include measured ping to server once we add WS ping/pong sampling
    sendEnvelope("HEARTBEAT", data);
}

// -------- UDP anchor --------

void LobbyClient::initiateUdpAnchor()
{
    if (!m_udp->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress))
    {
        qWarning() << "lobby: udp bind failed:" << m_udp->errorString();
        return;
    }
    sendUdpRegister();
    m_udpKeepaliveTimer->start();
}

void LobbyClient::sendUdpRegister()
{
    if (m_selfUserId == 0)
        return;
    QByteArray pkt;
    pkt.reserve(13);
    pkt.append(ANCHOR_MAGIC, 4);
    pkt.append(static_cast<char>(ANCHOR_OP_REGISTER));
    quint64 idBE = qToBigEndian(m_selfUserId);
    pkt.append(reinterpret_cast<const char*>(&idBE), sizeof(idBE));
    m_udp->writeDatagram(pkt, QHostAddress(m_udpAnchorHost), m_udpAnchorPort);
}

void LobbyClient::sendUdpKeepalive()
{
    if (m_selfUserId == 0)
        return;
    QByteArray pkt;
    pkt.reserve(13);
    pkt.append(ANCHOR_MAGIC, 4);
    pkt.append(static_cast<char>(ANCHOR_OP_KEEPALIVE));
    quint64 idBE = qToBigEndian(m_selfUserId);
    pkt.append(reinterpret_cast<const char*>(&idBE), sizeof(idBE));
    m_udp->writeDatagram(pkt, QHostAddress(m_udpAnchorHost), m_udpAnchorPort);
}

void LobbyClient::onUdpKeepaliveTimer()
{
    sendUdpKeepalive();
}

void LobbyClient::punchPeerEndpoints(const QList<LobbyMatchPeer>& peers)
{
    if (!m_udp || m_udp->state() == QAbstractSocket::UnconnectedState)
        return;
    if (m_selfUserId == 0)
        return;

    QByteArray pkt;
    pkt.reserve(13);
    pkt.append(ANCHOR_MAGIC, 4);
    pkt.append(static_cast<char>(ANCHOR_OP_PUNCH));
    quint64 idBE = qToBigEndian(m_selfUserId);
    pkt.append(reinterpret_cast<const char*>(&idBE), sizeof(idBE));

    for (const auto& p : peers)
    {
        if (p.userId == m_selfUserId)
            continue;
        if (p.publicIp.isEmpty() || p.publicPort == 0)
            continue;
        QHostAddress addr(p.publicIp);
        if (addr.isNull())
            continue;
        for (int i = 0; i < ANCHOR_PUNCH_BURST; ++i)
            m_udp->writeDatagram(pkt, addr, p.publicPort);
    }
}

quint16 LobbyClient::localUdpPort() const
{
    return m_udp ? m_udp->localPort() : 0;
}

void LobbyClient::releaseUdpAnchor()
{
    if (m_udpKeepaliveTimer)
        m_udpKeepaliveTimer->stop();
    if (m_udp && m_udp->state() != QAbstractSocket::UnconnectedState)
    {
        // abort() is more aggressive than close(): forces immediate socket
        // teardown rather than the graceful path. Necessary so GekkoNet can
        // re-bind the same port without racing the OS's lingering release.
        m_udp->abort();
    }
}

void LobbyClient::reopenUdpAnchor()
{
    if (m_state == ConnectionState::Connected && m_udp &&
        m_udp->state() == QAbstractSocket::UnconnectedState)
    {
        initiateUdpAnchor();
    }
}

void LobbyClient::onUdpReadyRead()
{
    while (m_udp->hasPendingDatagrams())
    {
        QByteArray datagram;
        QHostAddress sender;
        quint16 senderPort = 0;
        datagram.resize(int(m_udp->pendingDatagramSize()));
        m_udp->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        if (datagram.size() < 5 || std::memcmp(datagram.constData(), ANCHOR_MAGIC, 4) != 0)
            continue;

        const quint8 op = static_cast<quint8>(datagram.at(4));
        switch (op)
        {
        case ANCHOR_OP_PROBE:
        {
            if (datagram.size() != PROBE_PACKET_SIZE)
                break;
            // Echo as PROBE_REPLY with our userId in the sender slot; nonce
            // bytes (offset 13..21) carry through unchanged so the originator
            // can match on its end.
            QByteArray reply(datagram);
            reply[4] = static_cast<char>(ANCHOR_OP_PROBE_REPLY);
            const quint64 selfIdBE = qToBigEndian(m_selfUserId);
            std::memcpy(reply.data() + 5, &selfIdBE, sizeof(selfIdBE));
            m_udp->writeDatagram(reply, sender, senderPort);
            break;
        }
        case ANCHOR_OP_PROBE_REPLY:
        {
            if (datagram.size() != PROBE_PACKET_SIZE)
                break;
            quint64 nonceBE = 0;
            std::memcpy(&nonceBE, datagram.constData() + 13, sizeof(nonceBE));
            const quint64 nonce = qFromBigEndian(nonceBE);

            const auto it = m_pendingProbes.find(nonce);
            if (it == m_pendingProbes.end())
                break;
            const qint64 nowMs  = QDateTime::currentMSecsSinceEpoch();
            const int    rttMs  = static_cast<int>(nowMs - it->sendMs);
            const quint64 uid   = it->targetUserId;
            m_pendingProbes.erase(it);
            m_measuredPing[uid] = rttMs;
            emit pingProbeMeasured(uid, rttMs);
            break;
        }
        case ANCHOR_OP_REGISTER:
        case ANCHOR_OP_KEEPALIVE:
        case ANCHOR_OP_PUNCH:
        default:
            // Server acks for register/keepalive aren't actionable yet, and
            // PUNCH packets from peers are intentional no-ops — drop silently.
            break;
        }
    }

    // Cheap stale-probe cleanup: anything older than 5 seconds is never
    // coming back, so don't let the map grow unbounded.
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - 5'000;
    for (auto it = m_pendingProbes.begin(); it != m_pendingProbes.end(); )
    {
        if (it->sendMs < cutoff)
            it = m_pendingProbes.erase(it);
        else
            ++it;
    }
}

int LobbyClient::measuredPingMs(quint64 userId) const
{
    const auto it = m_measuredPing.constFind(userId);
    return it == m_measuredPing.constEnd() ? -1 : it.value();
}

// -------- Chat API --------

void LobbyClient::sendChat(const QString& channel, const QString& message)
{
    QJsonObject d;
    d["channel"] = channel;
    d["message"] = message;
    sendEnvelope("CHAT_SEND", d);
}

void LobbyClient::requestChatHistory(const QString& channel)
{
    QJsonObject d;
    d["channel"] = channel;
    sendEnvelope("CHAT_HISTORY", d);
}

// -------- Room API --------

void LobbyClient::createRoom(const QString& name, const QString& romName, const QString& romMd5,
                              const QString& romRegion, int maxPlayers, int delay, int prediction,
                              const QString& password)
{
    QJsonObject rom;
    rom["name"]   = romName;
    rom["md5"]    = romMd5;
    rom["region"] = romRegion;

    QJsonObject d;
    d["name"]       = name;
    d["rom"]        = rom;
    d["maxPlayers"] = maxPlayers;
    d["delay"]      = delay;
    d["prediction"] = prediction;
    if (!password.isEmpty())
        d["password"] = password;

    sendEnvelope("ROOM_CREATE", d);
}

void LobbyClient::joinRoom(quint64 roomId, const QString& password)
{
    QJsonObject d;
    d["roomId"] = QJsonValue(qint64(roomId));
    if (!password.isEmpty())
        d["password"] = password;
    sendEnvelope("ROOM_JOIN", d);
}

void LobbyClient::leaveRoom()
{
    sendEnvelope("ROOM_LEAVE");
}

void LobbyClient::startRoom()
{
    sendEnvelope("ROOM_START");
}

// Host-only: change the active room's rollback parameters. The server's
// ROOM_UPDATE_SETTINGS handler validates (host, state == "waiting"), clamps,
// and rebroadcasts ROOM_STATE with the new values + auto flags so every seated
// client picks them up and the match starts on the resolved delay.
void LobbyClient::updateRoomSettings(int delay, int prediction, bool delayAuto, bool predictionAuto)
{
    QJsonObject d;
    d["delay"]          = delay;
    d["prediction"]     = prediction;
    d["delayAuto"]      = delayAuto;
    d["predictionAuto"] = predictionAuto;
    sendEnvelope("ROOM_UPDATE_SETTINGS", d);
}

void LobbyClient::kickFromRoom(quint64 userId)
{
    QJsonObject d;
    d["userId"] = QJsonValue(qint64(userId));
    sendEnvelope("ROOM_KICK", d);
}

void LobbyClient::requestPingProbe(quint64 targetUserId)
{
    QJsonObject d;
    d["targetUserId"] = QJsonValue(qint64(targetUserId));
    sendEnvelope("PING_PROBE_REQUEST", d);
}

void LobbyClient::reportMatchConnected(quint64 matchId, quint64 peerUserId)
{
    QJsonObject d;
    d["matchId"]    = QJsonValue(qint64(matchId));
    d["peerUserId"] = QJsonValue(qint64(peerUserId));
    sendEnvelope("MATCH_CONNECTED", d);
}

void LobbyClient::reportMatchPunchFailed(quint64 matchId, quint64 peerUserId)
{
    QJsonObject d;
    d["matchId"]    = QJsonValue(qint64(matchId));
    d["peerUserId"] = QJsonValue(qint64(peerUserId));
    sendEnvelope("MATCH_PUNCH_FAILED", d);
}

void LobbyClient::reportMatchFinished(quint64 matchId)
{
    QJsonObject d;
    d["matchId"] = QJsonValue(qint64(matchId));
    sendEnvelope("MATCH_FINISHED", d);
}

void LobbyClient::sendBroadcastBegin(quint64 matchId)
{
    QJsonObject d;
    d["matchId"] = QJsonValue(qint64(matchId));
    sendEnvelope("BROADCAST_BEGIN", d);
}

void LobbyClient::sendBroadcastData(quint64 matchId, const QByteArray& chunk)
{
    if (chunk.isEmpty())
        return;
    QJsonObject d;
    d["matchId"] = QJsonValue(qint64(matchId));
    d["data"]    = QString::fromLatin1(chunk.toBase64());
    sendEnvelope("BROADCAST_DATA", d);
}

void LobbyClient::sendBroadcastEnd(quint64 matchId)
{
    QJsonObject d;
    d["matchId"] = QJsonValue(qint64(matchId));
    sendEnvelope("BROADCAST_END", d);
}

void LobbyClient::startSpectate(quint64 matchId)
{
    QJsonObject d;
    d["matchId"] = QJsonValue(qint64(matchId));
    sendEnvelope("SPECTATE_START", d);
}

void LobbyClient::stopSpectate(quint64 matchId)
{
    QJsonObject d;
    d["matchId"] = QJsonValue(qint64(matchId));
    sendEnvelope("SPECTATE_STOP", d);
}

void LobbyClient::quickMatchJoin(const QString& romName, const QString& romMd5)
{
    QJsonObject rom;
    rom["name"]   = romName;
    rom["md5"]    = romMd5;
    rom["region"] = QString();
    sendEnvelope("QUICK_MATCH_JOIN", { {"rom", rom} });
}

void LobbyClient::quickMatchCancel()
{
    sendEnvelope("QUICK_MATCH_CANCEL");
}

LobbyClient::LobbyUser LobbyClient::parsePresenceUser(const QJsonObject& obj)
{
    LobbyUser u;
    u.id              = static_cast<quint64>(obj.value("id").toDouble());
    u.username        = obj.value("username").toString();
    u.state           = obj.value("state").toString();
    u.region          = obj.value("region").toString();
    u.pingToServer    = static_cast<quint16>(obj.value("pingToServer").toInt());
    u.currentRoomId   = static_cast<quint64>(obj.value("currentRoomId").toDouble());
    u.currentRoomName = obj.value("currentRoomName").toString();
    u.searchingRom    = obj.value("searchingRom").toString();
    return u;
}

#endif // NETPLAY
