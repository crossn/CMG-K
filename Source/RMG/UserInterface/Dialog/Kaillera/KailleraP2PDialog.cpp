/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "KailleraP2PDialog.hpp"
#include "KailleraTraversalConfig.hpp"

#ifdef NETPLAY

#include "../../KailleraUIBridge.hpp"

#include <RMG-Core/Emulation.hpp>
#include <RMG-Core/Kaillera.hpp>
#include <RMG-Core/Settings.hpp>

#include "core/p2p_core.h"
#include "kailleraclient.h"
#include "n02_client.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QClipboard>
#include <QApplication>
#include <QIcon>
#include <QFrame>
#include <QListView>
#include <QMessageBox>
#include <QSettings>
#include <QAbstractSpinBox>
#include <QCursor>
#include <QMenu>
#include <QPoint>
#include <QWidgetAction>

#include <algorithm>
#include <cstring>

#include <chrono>
static inline unsigned long monotonicTickCount() {
    using namespace std::chrono;
    return (unsigned long)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static const char* kSsrvHost = "kaillerareborn.2manygames.fr";
static const int kSsrvPort = 27887;
static const char* kGameLayerMessagePrefix = "RMGK:MODE:";
static const char* kGameLayerStandard = "STD";
static const char* kGameLayerRollback = "RB";
static const char* kRollbackDelayMessagePrefix = "RMGK:RBDELAY:";
static constexpr int kRollbackDelayModeDefault = 0;
static constexpr int kRollbackDelayModeLower = 1;
static constexpr int kRollbackDelayModeHigher = 2;
static constexpr int kRollbackDelayModeCustom = 3;
static constexpr int kRollbackMinFrameDelay = 1;
static constexpr int kRollbackMaxFrameDelay = 9;
static constexpr int kDefaultRollbackFrameDelay = 2;
static constexpr int kDefaultRollbackPredictionWindow = 7;

static int clampRollbackFrameDelay(int delay)
{
    return std::clamp(delay, kRollbackMinFrameDelay, kRollbackMaxFrameDelay);
}

static int normalizeRollbackDelayMode(int mode)
{
    if (mode < kRollbackDelayModeDefault || mode > kRollbackDelayModeCustom)
    {
        return kRollbackDelayModeDefault;
    }
    return mode;
}

static QString timestamp()
{
    return QDateTime::currentDateTime().toString("[h:mm AP] ");
}

static QString formatTraversalCode(const QString& prefix, int number)
{
    return prefix + "@" + QString::number(number);
}

static QIcon themedP2PIcon(const QString& iconName)
{
    const bool darkTheme = QApplication::palette().window().color().value() < 128;
    return QIcon(QString(":/icons/%1/svg/%2.svg")
        .arg(darkTheme ? "white" : "black", iconName));
}

static bool localGameListContains(const QString& gameName)
{
    if (gameName.startsWith("*") || infos.gameList == nullptr)
    {
        return true;
    }

    const char* p = infos.gameList;
    while (*p)
    {
        if (gameName == QString::fromUtf8(p))
        {
            return true;
        }
        p += strlen(p) + 1;
    }

    return false;
}

static QString buildP2PStyleSheet(const QString& theme)
{
    if (theme != "Modern")
    {
        return QString();
    }

    const bool darkTheme = QApplication::palette().window().color().value() < 128;
    const QString comboArrowIcon = QString(":/icons/%1/svg/arrow-down-s-line.svg")
        .arg(darkTheme ? "white" : "black");

    return QString(
        "QDialog#KailleraP2PDialog {"
        "  background-color: palette(window);"
        "}"
        "QFrame#KailleraP2PTopBar {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 10px;"
        "  background-color: palette(base);"
        "}"
        "QLabel#KailleraP2PGameBanner {"
        "  padding: 0px;"
        "  font-weight: 500;"
        "}"
        "QLabel#KailleraP2PPeerStatus {"
        "  color: palette(text);"
        "  padding: 0px 2px;"
        "  font-weight: 600;"
        "}"
        "QLabel#KailleraP2PStatusLabel {"
        "  color: palette(text);"
        "  padding: 0 2px;"
        "  font-weight: 600;"
        "}"
        "QTextBrowser#KailleraP2PSurface {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 10px;"
        "  background-color: palette(base);"
        "  padding: 6px;"
        "}"
        "QLineEdit#KailleraP2PInput {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  background-color: palette(base);"
        "  padding: 5px 8px;"
        "  min-height: 24px;"
        "}"
        "QLineEdit#KailleraP2PInput:focus {"
        "  border-color: palette(highlight);"
        "}"
        "QComboBox#KailleraP2PCombo {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  background-color: palette(base);"
        "  padding: 4px 8px;"
        "  min-height: 24px;"
        "}"
        "QComboBox#KailleraP2PCombo:focus {"
        "  border-color: palette(highlight);"
        "}"
        "QComboBox#KailleraP2PCombo::drop-down {"
        "  subcontrol-origin: padding;"
        "  subcontrol-position: top right;"
        "  width: 24px;"
        "  border: none;"
        "  border-left: 1px solid palette(mid);"
        "  border-top-right-radius: 7px;"
        "  border-bottom-right-radius: 7px;"
        "  background-color: transparent;"
        "  margin: 1px;"
        "}"
        "QComboBox#KailleraP2PCombo::down-arrow {"
        "  image: url(%1);"
        "  width: 12px;"
        "  height: 12px;"
        "}"
        "QComboBox#KailleraP2PCombo QAbstractItemView {"
        "  border: 1px solid palette(mid);"
        "  background-color: palette(base);"
        "  selection-background-color: palette(highlight);"
        "  selection-color: palette(highlighted-text);"
        "  outline: none;"
        "  padding: 0px;"
        "  margin: 0px;"
        "}"
        "QComboBox#KailleraP2PCombo QAbstractItemView::item {"
        "  padding: 0px 8px;"
        "  min-height: 18px;"
        "  margin: 0px;"
        "}"
        "QSpinBox#KailleraP2PSpin {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  background-color: palette(base);"
        "  padding: 4px 8px;"
        "  min-height: 24px;"
        "}"
        "QSpinBox#KailleraP2PSpin:focus {"
        "  border-color: palette(highlight);"
        "}"
        "QGroupBox#KailleraP2PGroup {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 10px;"
        "  background-color: palette(base);"
        "  margin-top: 10px;"
        "  padding-top: 6px;"
        "}"
        "QGroupBox#KailleraP2PGroup::title {"
        "  subcontrol-origin: margin;"
        "  left: 10px;"
        "  padding: 0 4px;"
        "  font-weight: 600;"
        "}"
        "QWidget#KailleraChatComposer {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  background-color: palette(base);"
        "}"
        "QLineEdit#KailleraChatComposerInput {"
        "  border: none;"
        "  background: transparent;"
        "  padding: 1px 0px;"
        "  min-height: 16px;"
        "}"
        "QLineEdit#KailleraChatComposerInput:focus {"
        "  border: none;"
        "}"
        "QPushButton#KailleraChatComposerSendButton {"
        "  border: none;"
        "  border-radius: 6px;"
        "  min-width: 20px;"
        "  min-height: 20px;"
        "  max-width: 20px;"
        "  max-height: 20px;"
        "  padding: 0px;"
        "  background-color: transparent;"
        "  color: palette(text);"
        "  font-size: 15px;"
        "  font-weight: 900;"
        "}"
        "QPushButton#KailleraChatComposerSendButton:hover {"
        "  border: 1px solid palette(mid);"
        "  background-color: palette(light);"
        "}"
        "QPushButton#KailleraChatComposerSendButton:pressed {"
        "  border: 1px solid palette(mid);"
        "  background-color: palette(midlight);"
        "}"
        "QPushButton#KailleraPlayerKickButton {"
        "  border: 1px solid transparent;"
        "  border-radius: 6px;"
        "  min-width: 20px;"
        "  max-width: 20px;"
        "  min-height: 20px;"
        "  max-height: 20px;"
        "  padding: 0px;"
        "  color: #c03a3a;"
        "  background-color: transparent;"
        "  font-weight: 800;"
        "}"
        "QPushButton#KailleraPlayerKickButton:hover {"
        "  border-color: #d77a7a;"
        "  background-color: rgba(208, 72, 72, 0.10);"
        "}"
        "QPushButton#KailleraPlayerKickButton:pressed {"
        "  border-color: #b55a5a;"
        "  background-color: rgba(208, 72, 72, 0.18);"
        "}"
        "QPushButton#KailleraP2PPrimaryButton {"
        "  border: 1px solid #0066b4;"
        "  border-radius: 7px;"
        "  min-height: 24px;"
        "  padding: 4px 12px;"
        "  font-weight: 700;"
        "  color: white;"
        "  background-color: #0078D7;"
        "}"
        "QPushButton#KailleraP2PPrimaryButton:hover {"
        "  background-color: #1584dd;"
        "}"
        "QPushButton#KailleraP2PPrimaryButton:pressed,"
        "QPushButton#KailleraP2PPrimaryButton:checked {"
        "  background-color: #0063b1;"
        "}"
        "QPushButton#KailleraP2PSecondaryButton {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  min-height: 24px;"
        "  padding: 4px 12px;"
        "  background-color: palette(window);"
        "  font-weight: 600;"
        "}"
        "QPushButton#KailleraP2PSecondaryButton:hover {"
        "  border-color: palette(dark);"
        "  background-color: palette(light);"
        "}"
        "QPushButton#KailleraP2PSecondaryButton:pressed {"
        "  border-color: palette(shadow);"
        "  background-color: palette(mid);"
        "  padding-top: 5px;"
        "  padding-bottom: 3px;"
        "}"
        "QMenu#KailleraP2PAdvancedMenu {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  background-color: palette(base);"
        "  padding: 2px;"
        "}"
        "QWidget#KailleraP2PLayerToggle {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 14px;"
        "  background-color: palette(window);"
        "}"
        "QPushButton#KailleraP2PLayerLeftButton,"
        "QPushButton#KailleraP2PLayerRightButton {"
        "  border: none;"
        "  min-height: 24px;"
        "  padding: 3px 14px;"
        "  background-color: transparent;"
        "  font-weight: 600;"
        "}"
        "QPushButton#KailleraP2PLayerLeftButton {"
        "  border-top-left-radius: 13px;"
        "  border-bottom-left-radius: 13px;"
        "}"
        "QPushButton#KailleraP2PLayerRightButton {"
        "  border-top-right-radius: 13px;"
        "  border-bottom-right-radius: 13px;"
        "}"
        "QPushButton#KailleraP2PLayerLeftButton:hover,"
        "QPushButton#KailleraP2PLayerRightButton:hover {"
        "  background-color: palette(light);"
        "}"
        "QPushButton#KailleraP2PLayerLeftButton:checked,"
        "QPushButton#KailleraP2PLayerRightButton:checked {"
        "  background-color: palette(midlight);"
        "  color: palette(text);"
        "  font-weight: 700;"
        "}"
        "QPushButton#KailleraP2PIconButton {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 6px;"
        "  padding: 0px;"
        "  background-color: palette(button);"
        "}"
        "QPushButton#KailleraP2PIconButton:hover {"
        "  background-color: palette(light);"
        "}"
        "QPushButton#KailleraP2PIconButton:pressed {"
        "  background-color: palette(midlight);"
        "}"
    ).arg(comboArrowIcon);
}

static void configureP2PComboPopup(QComboBox* combo, const QString& theme)
{
    if (combo == nullptr || theme != "Modern")
    {
        return;
    }

    auto* popupView = new QListView(combo);
    popupView->setUniformItemSizes(true);
    popupView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    popupView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    const QPalette appPalette = QApplication::palette();
    const QColor windowColor = appPalette.window().color();
    const QColor baseColor = appPalette.base().color();
    const bool darkTheme = windowColor.value() < 128;
    const QColor popupColor = darkTheme
        ? baseColor.darker(106)
        : baseColor.darker(108);
    const QColor borderColor = darkTheme
        ? windowColor.lighter(142)
        : windowColor.darker(132);

    popupView->setStyleSheet(QString(
        "QListView {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  outline: none;"
        "  padding: 2px 0px;"
        "}"
        "QListView::item {"
        "  padding: 2px 8px;"
        "  min-height: 18px;"
        "  margin: 0px;"
        "}"
        "QListView::item:selected {"
        "  background-color: palette(highlight);"
        "  color: palette(highlighted-text);"
        "}").arg(popupColor.name(QColor::HexRgb), borderColor.name(QColor::HexRgb)));

    combo->setView(popupView);
}

// Check if a string looks like a NAT traversal code rather than an IP address.
static QString normalizeTraversalCode(const QString& input)
{
    QString s = input.trimmed().toUpper();
    s.remove(' ');
    if (s.isEmpty()) return QString();

    for (const QChar& ch : s)
    {
        if (ch == '.' || ch == ':' || ch == '/') return QString();
    }

    if (s.size() < 4) return QString();

    int prefixLength = 0;
    while (prefixLength < s.size() && prefixLength < 4 && s[prefixLength].isLetter())
    {
        ++prefixLength;
    }
    if (prefixLength < 3) return QString();
    if (prefixLength < s.size() && s[prefixLength].isLetter()) return QString();

    const QString prefix = s.left(prefixLength);
    QString digits = s.mid(prefixLength);
    if (digits.startsWith('@') || digits.startsWith('#') || digits.startsWith('-') || digits.startsWith('_'))
        digits.remove(0, 1);
    if (digits.isEmpty()) return QString();
    if (digits.size() > 3) return QString();

    for (const QChar& ch : digits)
    {
        if (!ch.isDigit()) return QString();
    }

    bool ok = false;
    const int number = digits.toInt(&ok);
    if (!ok || number < 0) return QString();

    return formatTraversalCode(prefix, number);
}

// Matches n02-rmg's LooksLikeTraversalCode() intent, but accepts legacy separators and leading zeroes.
static bool looksLikeTraversalCode(const QString& s)
{
    return !normalizeTraversalCode(s).isEmpty();
}

// Try to extract an IPv4 address and optional port from a string.
// Returns true if an IP was found.
static bool tryExtractIPv4AndPort(const QByteArray& s, QString& outIp, int& outPort)
{
    outIp.clear();
    outPort = 0;

    const char* data = s.constData();
    int len = s.size();

    for (int i = 0; i < len; i++)
    {
        if (data[i] < '0' || data[i] > '9') continue;

        unsigned int octets[4];
        const char* cur = data + i;
        bool ok = true;
        for (int o = 0; o < 4; o++)
        {
            unsigned int val = 0;
            int digits = 0;
            while (*cur >= '0' && *cur <= '9' && digits < 3)
            {
                val = val * 10 + (unsigned int)(*cur - '0');
                cur++;
                digits++;
            }
            if (digits == 0 || val > 255) { ok = false; break; }
            octets[o] = val;
            if (o < 3)
            {
                if (*cur != '.') { ok = false; break; }
                cur++;
            }
        }
        if (!ok) continue;

        outIp = QString("%1.%2.%3.%4").arg(octets[0]).arg(octets[1]).arg(octets[2]).arg(octets[3]);

        // Optional ":port"
        while (*cur == ' ' || *cur == '\t') cur++;
        if (*cur == ':') cur++;
        else if (*cur != 0 && (*cur < '0' || *cur > '9')) return true;

        unsigned int port = 0;
        int digits = 0;
        while (*cur >= '0' && *cur <= '9' && digits < 5)
        {
            port = port * 10 + (unsigned int)(*cur - '0');
            cur++;
            digits++;
        }
        if (digits > 0 && port > 0 && port <= 65535)
            outPort = (int)port;
        return true;
    }

    return false;
}

KailleraP2PDialog::KailleraP2PDialog(bool isHost, const QString& gameName,
                                     const QString& username,
                                     const QString& joinCode, QWidget* parent)
    : QDialog(parent, Qt::Window), m_isHost(isHost), m_gameName(gameName), m_username(username)
{
    if (m_isHost)
    {
        QSettings settings("RMG-K", "n02");
        m_gameLayer = settings.value("P2P_GameLayer", QString(kGameLayerStandard)).toString() == kGameLayerRollback ?
            GameLayer::Rollback : GameLayer::Standard;
        m_rollbackDelayMode = normalizeRollbackDelayMode(
            settings.value("Rollback_FrameDelayMode", kRollbackDelayModeDefault).toInt());
        m_customRollbackFrameDelay = clampRollbackFrameDelay(
            settings.value("Rollback_FrameDelay", kDefaultRollbackFrameDelay).toInt());
        m_rollbackFrameDelay = m_customRollbackFrameDelay;
    }

    setWindowIcon(QIcon(":Resource/Kaillera.svg"));
    setupUI();
    connectSignals();

    // P2P step timer — drives the p2p state machine (1ms)
    m_stepTimer = new QTimer(this);
    connect(m_stepTimer, &QTimer::timeout, this, &KailleraP2PDialog::onStepTimer);
    m_stepTimer->start(1);

    // NAT traversal housekeeping timer (1s)
    m_travTimer = new QTimer(this);
    connect(m_travTimer, &QTimer::timeout, this, &KailleraP2PDialog::onTravTimer);
    m_travTimer->start(1000);

    travResetState();
    travLoadIdentity();

    if (m_isHost)
    {
        // Host always uses NAT traversal (host by code)
        m_travHostEnabled = true;
        if (!m_travToken.isEmpty() && !m_travCode.isEmpty())
        {
            travSendHostOpen();
        }
        else
        {
            travSendClaimAuto();
        }
        m_travNextRegMs = QDateTime::currentMSecsSinceEpoch() + 2000;
        updateHostCodeUI();

        m_chat->append("<span style='color:green;'>" + timestamp() +
                       "Hosting " + m_gameName.toHtmlEscaped() +
                       " on port " + QString::number(p2p_core_get_port()) + "</span>");
    }
    else if (!joinCode.isEmpty() && looksLikeTraversalCode(joinCode))
    {
        // Join by traversal code
        m_travJoinEnabled = true;
        m_travJoinCode = normalizeTraversalCode(joinCode);
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        travSendJoin();
        m_travNextJoinMs = now + 3000;
        m_travJoinDeadlineMs = now + 15000;

        m_chat->append("<span style='color:green;'>" + timestamp() +
                       "Looking up host for code " + joinCode.toHtmlEscaped() + "</span>");
    }
}

KailleraP2PDialog::~KailleraP2PDialog()
{
    cleanupSessionForClose();
}

void KailleraP2PDialog::setupUI()
{
    setObjectName("KailleraP2PDialog");
    setWindowTitle(m_isHost ? "Hosting P2P" : "P2P Game");
    setMinimumSize(610, 465);
    resize(minimumSize());

    const QString theme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));
    setStyleSheet(buildP2PStyleSheet(theme));

    auto* mainLayout = new QVBoxLayout(this);

    auto* topBar = new QFrame(this);
    topBar->setObjectName("KailleraP2PTopBar");
    topBar->setFrameStyle(QFrame::Box | QFrame::Sunken);
    auto* topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(10, 6, 8, 6);
    topBarLayout->setSpacing(8);

    m_gameLabel = new QLabel(topBar);
    m_gameLabel->setObjectName("KailleraP2PGameBanner");
    m_gameLabel->setText("Game: " + m_gameName);
    m_gameLabel->setMinimumWidth(0);
    topBarLayout->addWidget(m_gameLabel, 1);

    m_peerStatusLabel = new QLabel(topBar);
    m_peerStatusLabel->setObjectName("KailleraP2PPeerStatus");
    m_peerStatusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    topBarLayout->addWidget(m_peerStatusLabel, 0);

    m_btnKickPeer = new QPushButton("X", topBar);
    m_btnKickPeer->setObjectName("KailleraPlayerKickButton");
    m_btnKickPeer->setCursor(Qt::PointingHandCursor);
    m_btnKickPeer->setToolTip("Kick opponent from the lobby");
    m_btnKickPeer->setVisible(false);
    connect(m_btnKickPeer, &QPushButton::clicked, this, &KailleraP2PDialog::onKickPeer);
    topBarLayout->addWidget(m_btnKickPeer, 0);
    updatePeerConnectionUI();

    mainLayout->addWidget(topBar);

    // Chat / status area (takes most space)
    m_chat = new QTextBrowser(this);
    m_chat->setObjectName("KailleraP2PSurface");
    m_chat->setOpenExternalLinks(true);
    m_chat->document()->setMaximumBlockCount(1000);
    mainLayout->addWidget(m_chat, 1);

    // Chat input row
    auto* chatInputLayout = new QHBoxLayout();
    chatInputLayout->setContentsMargins(0, 0, 0, 0);

    auto* chatComposer = new QWidget(this);
    chatComposer->setObjectName("KailleraChatComposer");
    chatComposer->setFixedHeight(24);
    auto* chatComposerLayout = new QHBoxLayout(chatComposer);
    chatComposerLayout->setContentsMargins(10, 2, 4, 2);
    chatComposerLayout->setSpacing(4);

    m_chatInput = new QLineEdit(chatComposer);
    m_chatInput->setPlaceholderText("Type a message...");
    m_chatInput->setClearButtonEnabled(true);
    m_chatInput->setObjectName("KailleraChatComposerInput");
    m_chatInput->setFrame(false);

    m_btnChat = new QPushButton(chatComposer);
    m_btnChat->setObjectName("KailleraChatComposerSendButton");
    m_btnChat->setToolTip("Send message");
    m_btnChat->setText("");
    m_btnChat->setIcon(themedP2PIcon("play-line"));
    m_btnChat->setIconSize(QSize(13, 13));

    chatComposerLayout->addWidget(m_chatInput);
    chatComposerLayout->addWidget(m_btnChat, 0, Qt::AlignVCenter);
    chatInputLayout->addWidget(chatComposer, 1);
    mainLayout->addLayout(chatInputLayout);

    // Button row: Ready, Drop Game, Record game checkbox  |  Host group
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(8);
    bottomLayout->setAlignment(Qt::AlignTop);

    // Left side: buttons
    auto* leftWidget = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, m_isHost ? 12 : 0, 0, 0);
    leftLayout->setSpacing(0);

    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(8);
    m_btnReady = new QPushButton("Ready", this);
    m_btnReady->setCheckable(true);
    m_btnReady->setObjectName("KailleraP2PPrimaryButton");
    if (!m_isHost)
    {
        m_btnReady->setEnabled(false);
    }
    m_btnDrop = new QPushButton("Drop Game", this);
    m_btnDrop->setObjectName("KailleraP2PSecondaryButton");
    btnRow->addWidget(m_btnReady);
    btnRow->addWidget(m_btnDrop);
    m_recordCheck = new QCheckBox("Record game", this);
    const bool recordingEnabledByDefault = CoreGetKailleraEffectiveRecordingDefault();
    extern bool n02_kaillera_recording_enabled;
    n02_kaillera_recording_enabled = recordingEnabledByDefault;
    m_recordCheck->setChecked(recordingEnabledByDefault);
    connect(m_recordCheck, &QCheckBox::toggled, this, [](bool checked) {
        extern bool n02_kaillera_recording_enabled;
        n02_kaillera_recording_enabled = checked;
    });
    btnRow->addWidget(m_recordCheck);
    btnRow->addStretch();
    leftLayout->addLayout(btnRow);

    m_advancedSettingsButton = new QPushButton("Advanced settings", leftWidget);
    m_advancedSettingsButton->setObjectName("KailleraP2PSecondaryButton");
    m_advancedSettingsButton->setIcon(themedP2PIcon("settings-3-line"));
    m_advancedSettingsButton->setIconSize(QSize(14, 14));
    leftLayout->addSpacing(12);
    leftLayout->addWidget(m_advancedSettingsButton, 0, Qt::AlignLeft);

    auto* advancedMenu = new QMenu(m_advancedSettingsButton);
    advancedMenu->setObjectName("KailleraP2PAdvancedMenu");
    auto* advancedMenuAction = new QWidgetAction(advancedMenu);
    auto* advancedWidget = new QWidget(advancedMenu);
    auto* advancedLayout = new QVBoxLayout(advancedWidget);
    advancedLayout->setContentsMargins(10, 8, 10, 8);
    advancedLayout->setSpacing(8);

    if (m_isHost)
    {
        auto* layerRow = new QHBoxLayout();
        layerRow->setContentsMargins(0, 0, 0, 0);
        layerRow->setSpacing(8);
        auto* layerLabel = new QLabel("Netcode:", advancedWidget);
        layerRow->addWidget(layerLabel);

        auto* layerToggle = new QWidget(advancedWidget);
        layerToggle->setObjectName("KailleraP2PLayerToggle");
        auto* layerToggleLayout = new QHBoxLayout(layerToggle);
        layerToggleLayout->setContentsMargins(2, 2, 2, 2);
        layerToggleLayout->setSpacing(0);

        m_standardLayerButton = new QPushButton("Standard", layerToggle);
        m_standardLayerButton->setObjectName("KailleraP2PLayerLeftButton");
        m_standardLayerButton->setCheckable(true);
        m_standardLayerButton->setAutoExclusive(true);
        m_standardLayerButton->setToolTip("Use the standard Kaillera game layer");
        m_rollbackLayerButton = new QPushButton("Rollback", layerToggle);
        m_rollbackLayerButton->setObjectName("KailleraP2PLayerRightButton");
        m_rollbackLayerButton->setCheckable(true);
        m_rollbackLayerButton->setAutoExclusive(true);
        m_rollbackLayerButton->setToolTip("Use the rollback game layer");
        layerToggleLayout->addWidget(m_standardLayerButton);
        layerToggleLayout->addWidget(m_rollbackLayerButton);

        layerRow->addWidget(layerToggle);
        advancedLayout->addLayout(layerRow);

        connect(m_standardLayerButton, &QPushButton::clicked, this, [this]() {
            setGameLayer(GameLayer::Standard, true, true);
        });
        connect(m_rollbackLayerButton, &QPushButton::clicked, this, [this]() {
            setGameLayer(GameLayer::Rollback, true, true);
        });
    }

    auto* predictionLayout = new QHBoxLayout();
    predictionLayout->setContentsMargins(0, 0, 0, 0);
    predictionLayout->setSpacing(6);
    auto* predictionWindowLabel = new QLabel("Prediction Window:", advancedWidget);
    predictionLayout->addWidget(predictionWindowLabel);
    m_predictionWindowCombo = new QComboBox(advancedWidget);
    m_predictionWindowCombo->setObjectName("KailleraP2PCombo");
    m_predictionWindowCombo->setMinimumWidth(120);
    m_predictionWindowCombo->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
    configureP2PComboPopup(m_predictionWindowCombo, theme);
    for (int frames = 1; frames <= 10; frames++)
    {
        m_predictionWindowCombo->addItem(frames == 1 ? "1 frame" : QString("%1 frames").arg(frames));
    }
    QSettings settings("RMG-K", "n02");
    int predictionWindow = settings.value("Rollback_PredictionWindow", kDefaultRollbackPredictionWindow).toInt();
    if (predictionWindow < 1 || predictionWindow > 10) predictionWindow = kDefaultRollbackPredictionWindow;
    m_predictionWindowCombo->setCurrentIndex(predictionWindow - 1);
    connect(m_predictionWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [](int index) {
        QSettings settings("RMG-K", "n02");
        settings.setValue("Rollback_PredictionWindow", index + 1);
    });
    predictionLayout->addWidget(m_predictionWindowCombo);
    advancedLayout->addLayout(predictionLayout);
    advancedMenuAction->setDefaultWidget(advancedWidget);
    advancedMenu->addAction(advancedMenuAction);
    connect(advancedMenu, &QMenu::aboutToHide, this, [this]() {
        if (m_advancedSettingsButton == nullptr || QApplication::mouseButtons() == Qt::NoButton)
        {
            return;
        }

        const QPoint buttonPos = m_advancedSettingsButton->mapFromGlobal(QCursor::pos());
        if (m_advancedSettingsButton->rect().contains(buttonPos))
        {
            m_suppressAdvancedSettingsPopup = true;
        }
    });
    connect(m_advancedSettingsButton, &QPushButton::clicked, this, [this, advancedMenu]() {
        if (m_advancedSettingsButton == nullptr)
        {
            return;
        }
        if (m_suppressAdvancedSettingsPopup)
        {
            m_suppressAdvancedSettingsPopup = false;
            return;
        }
        if (advancedMenu->isVisible())
        {
            advancedMenu->hide();
            return;
        }

        advancedMenu->popup(m_advancedSettingsButton->mapToGlobal(
            QPoint(0, m_advancedSettingsButton->height())));
    });

    bottomLayout->addWidget(leftWidget, 0, Qt::AlignTop);

    // Right side: host connection details and per-layer options.
    m_hostGroup = new QGroupBox(m_isHost ? "Host:" : "Session:", this);
    m_hostGroup->setObjectName("KailleraP2PGroup");
    auto* hostLayout = new QVBoxLayout(m_hostGroup);
    hostLayout->setContentsMargins(9, 7, 9, 9);
    hostLayout->setSpacing(8);

    if (!m_isHost)
    {
        m_gameLayerStatusLabel = new QLabel(m_hostGroup);
        m_gameLayerStatusLabel->setObjectName("KailleraP2PStatusLabel");
        hostLayout->addWidget(m_gameLayerStatusLabel);
    }

    m_frameDelayRow = new QWidget(m_hostGroup);
    auto* fdlyLayout = new QHBoxLayout(m_frameDelayRow);
    fdlyLayout->setContentsMargins(0, 0, 0, 0);
    fdlyLayout->setSpacing(6);
    m_frameDelayLabel = new QLabel(m_frameDelayRow);
    m_frameDelayLabel->setText(isRollbackMode() ? "Input Delay:" : "Frame Delay:");
    fdlyLayout->addWidget(m_frameDelayLabel);
    m_frameDelayCombo = new QComboBox(m_frameDelayRow);
    m_frameDelayCombo->setObjectName("KailleraP2PCombo");
    m_frameDelayCombo->setMinimumWidth(126);
    m_frameDelayCombo->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
    configureP2PComboPopup(m_frameDelayCombo, theme);
    connect(m_frameDelayCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (isRollbackMode())
        {
            if (m_isHost && index >= 0)
            {
                setRollbackDelayMode(m_frameDelayCombo->itemData(index).toInt(), true, true);
            }
        }
        else
        {
            KailleraUIBridge::instance().setSelectedDelay(index);
        }
    });
    fdlyLayout->addWidget(m_frameDelayCombo);

    m_frameDelaySpin = new QSpinBox(m_frameDelayRow);
    m_frameDelaySpin->setObjectName("KailleraP2PSpin");
    m_frameDelaySpin->setRange(kRollbackMinFrameDelay, kRollbackMaxFrameDelay);
    m_frameDelaySpin->setKeyboardTracking(false);
    m_frameDelaySpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_frameDelaySpin->setAlignment(Qt::AlignCenter);
    m_frameDelaySpin->setSuffix("f");
    m_frameDelaySpin->setMinimumWidth(48);
    m_frameDelaySpin->setMaximumWidth(54);
    m_frameDelaySpin->setValue(m_customRollbackFrameDelay);
    connect(m_frameDelaySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (isRollbackMode() && m_isHost && m_rollbackDelayMode == kRollbackDelayModeCustom)
        {
            setRollbackCustomFrameDelay(value, true, true);
        }
    });
    fdlyLayout->addWidget(m_frameDelaySpin);
    fdlyLayout->addStretch();
    hostLayout->addWidget(m_frameDelayRow);

    if (m_isHost)
    {
        auto* codeRow = new QHBoxLayout();
        codeRow->setContentsMargins(0, 0, 0, 0);
        codeRow->setSpacing(6);
        auto* codeLabel = new QLabel("Connect code:", m_hostGroup);
        codeRow->addWidget(codeLabel);

        m_connectCodeEdit = new QLineEdit(m_hostGroup);
        m_connectCodeEdit->setObjectName("KailleraP2PInput");
        m_connectCodeEdit->setReadOnly(true);
        m_connectCodeEdit->setText("{waiting}");
        m_connectCodeEdit->setMinimumWidth(118);
        m_connectCodeEdit->setMaximumWidth(190);
        m_copyAction = m_connectCodeEdit->addAction(themedP2PIcon("copy-line"), QLineEdit::TrailingPosition);
        m_copyAction->setToolTip("Copy to clipboard");
        connect(m_copyAction, &QAction::triggered, this, &KailleraP2PDialog::onCopyConnectCode);
        codeRow->addWidget(m_connectCodeEdit);
        hostLayout->addLayout(codeRow);

        m_enlistCheck = new QCheckBox("Show on public list", m_hostGroup);
        m_enlistCheck->setChecked(CoreSettingsGetBoolValue(SettingsID::Kaillera_P2PShowOnPublicList));
        hostLayout->addWidget(m_enlistCheck);
        connect(m_enlistCheck, &QCheckBox::toggled, this, [this](bool checked) {
            CoreSettingsSetValue(SettingsID::Kaillera_P2PShowOnPublicList, checked);
            CoreSettingsSave();
            if (checked)
                enlistGame();
            else
                unenlistGame();
        });

        const int formLabelWidth = std::max({
            m_frameDelayLabel->sizeHint().width(),
            codeLabel->sizeHint().width()
        });
        m_frameDelayLabel->setMinimumWidth(formLabelWidth);
        codeLabel->setMinimumWidth(formLabelWidth);
    }

    const QMargins hostMargins = hostLayout->contentsMargins();
    m_frameDelayLabel->setText(isRollbackMode() ? "Input Delay:" : "Frame Delay:");
    const int labelWidth = m_frameDelayLabel->sizeHint().width();
    const int hostMinWidth =
        hostMargins.left() +
        labelWidth +
        fdlyLayout->spacing() +
        m_frameDelayCombo->minimumWidth() +
        fdlyLayout->spacing() +
        m_frameDelaySpin->minimumWidth() +
        hostMargins.right();
    m_hostGroup->setMinimumWidth(hostMinWidth);
    applyGameLayerUI();
    bottomLayout->addWidget(m_hostGroup, 0, Qt::AlignTop);
    const int hostMinHeight = m_hostGroup->sizeHint().height();
    m_hostGroup->setMinimumHeight(hostMinHeight);

    mainLayout->addLayout(bottomLayout);

    auto* statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(12);
    m_netcodeModeLabel = new QLabel(this);
    m_netcodeModeLabel->setObjectName("KailleraP2PStatusLabel");
    statusLayout->addWidget(m_netcodeModeLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    m_pingLabel = new QLabel("Ping: --", this);
    m_pingLabel->setObjectName("KailleraP2PStatusLabel");
    statusLayout->addWidget(m_pingLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    m_delayLabel = new QLabel("Delay: --", this);
    m_delayLabel->setObjectName("KailleraP2PStatusLabel");
    statusLayout->addWidget(m_delayLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    statusLayout->addStretch();
    mainLayout->addLayout(statusLayout);
    updateNetcodeModeStatus();
    if (isRollbackMode())
    {
        updateRollbackDelayControls();
    }

    // Connect button actions
    connect(m_btnChat, &QPushButton::clicked, this, &KailleraP2PDialog::onSendChat);
    connect(m_chatInput, &QLineEdit::returnPressed, this, &KailleraP2PDialog::onSendChat);
    connect(m_btnReady, &QPushButton::clicked, this, &KailleraP2PDialog::onReady);
    connect(m_btnDrop, &QPushButton::clicked, this, &KailleraP2PDialog::onDrop);

    m_copyFeedbackTimer = new QTimer(this);
    m_copyFeedbackTimer->setSingleShot(true);
    connect(m_copyFeedbackTimer, &QTimer::timeout, this, [this]() {
        if (m_copyAction == nullptr)
        {
            return;
        }

        m_copyAction->setIcon(themedP2PIcon("copy-line"));
        m_copyAction->setToolTip("Copy to clipboard");
    });
}

void KailleraP2PDialog::connectSignals()
{
    auto& bridge = KailleraUIBridge::instance();
    constexpr Qt::ConnectionType kUiCallbackConnection = Qt::QueuedConnection;

    connect(&bridge, &KailleraUIBridge::p2pChatReceived, this, &KailleraP2PDialog::onChatReceived,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pGameStarted, this, &KailleraP2PDialog::onGameStarted,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pGameEnded, this, &KailleraP2PDialog::onGameEnded,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pClientDropped, this, &KailleraP2PDialog::onClientDropped,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pDebugMessage, this, &KailleraP2PDialog::onDebug,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pHostedGame, this, &KailleraP2PDialog::onHostedGame,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pPingUpdated, this, &KailleraP2PDialog::onPingUpdated,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pPeerJoined, this, &KailleraP2PDialog::onPeerJoined,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pPeerLeft, this, &KailleraP2PDialog::onPeerLeft,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pPeerInfo, this, &KailleraP2PDialog::onPeerInfo,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pFodippResult, this, &KailleraP2PDialog::onFodippResult,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pSsrvPacketReceived, this, &KailleraP2PDialog::onSsrvPacketReceived,
            kUiCallbackConnection);
}

void KailleraP2PDialog::reject()
{
    cleanupSessionForClose();
    QDialog::reject();
}

void KailleraP2PDialog::cleanupSessionForClose()
{
    if (m_closeCleanupDone)
        return;

    m_closeCleanupDone = true;

    if (m_stepTimer) m_stepTimer->stop();
    if (m_travTimer) m_travTimer->stop();
    m_ready = false;
    if (m_btnReady) m_btnReady->setChecked(false);

    if (isRollbackMode() && m_rollbackGameActive)
    {
        m_rollbackGameActive = false;
        CoreStopEmulation();
    }

    // Remove from public game list
    if (m_enlistCheck && m_enlistCheck->isChecked())
        unenlistGame();

    if (m_isHost && m_travHostEnabled)
        travSendHostClose();

    p2p_disconnect();
    p2p_core_cleanup();

    travResetState();
}

bool KailleraP2PDialog::isRollbackMode() const
{
    return m_gameLayer == GameLayer::Rollback;
}

int KailleraP2PDialog::automaticRollbackFrameDelay() const
{
    if (m_lastPing < 0)
    {
        return kDefaultRollbackFrameDelay;
    }

    const int oneWayMs = (m_lastPing / 2) + 5;
    return clampRollbackFrameDelay(1 + ((oneWayMs * 60) / 1000));
}

int KailleraP2PDialog::calculatedRollbackFrameDelay() const
{
    int delay = automaticRollbackFrameDelay();

    if (m_rollbackDelayMode == kRollbackDelayModeLower)
    {
        delay--;
    }
    else if (m_rollbackDelayMode == kRollbackDelayModeHigher)
    {
        delay++;
    }
    else if (m_rollbackDelayMode == kRollbackDelayModeCustom)
    {
        delay = m_customRollbackFrameDelay;
    }

    return clampRollbackFrameDelay(delay);
}

int KailleraP2PDialog::effectiveRollbackFrameDelay() const
{
    if (!m_isHost && m_hasRemoteRollbackDelaySettings)
    {
        return clampRollbackFrameDelay(m_rollbackFrameDelay);
    }

    return calculatedRollbackFrameDelay();
}

bool KailleraP2PDialog::parseRollbackDelayMessage(const QString& message, int& mode, int& delay) const
{
    QString text = message.trimmed().toUpper();
    if (!text.startsWith(kRollbackDelayMessagePrefix))
    {
        return false;
    }

    text = text.mid(QString(kRollbackDelayMessagePrefix).size());
    const int separator = text.indexOf(':');
    bool modeOk = false;
    bool delayOk = false;
    const int parsedMode = (separator >= 0 ? text.left(separator) : text).toInt(&modeOk);
    const int parsedDelay = (separator >= 0) ? text.mid(separator + 1).toInt(&delayOk) : kDefaultRollbackFrameDelay;

    mode = modeOk ? normalizeRollbackDelayMode(parsedMode) : kRollbackDelayModeDefault;
    delay = delayOk ? clampRollbackFrameDelay(parsedDelay) : kDefaultRollbackFrameDelay;
    return true;
}

void KailleraP2PDialog::sendRollbackDelaySettings(bool force)
{
    if (!m_isHost || !isRollbackMode() || !p2p_is_connected())
    {
        return;
    }

    const int delay = effectiveRollbackFrameDelay();
    if (!force && m_hasSentRollbackDelaySettings &&
        m_lastSentRollbackDelayMode == m_rollbackDelayMode &&
        m_lastSentRollbackFrameDelay == delay)
    {
        return;
    }

    QByteArray message = QByteArray(kRollbackDelayMessagePrefix) +
        QByteArray::number(m_rollbackDelayMode) + ":" +
        QByteArray::number(delay);
    p2p_send_chat(message.data());

    m_hasSentRollbackDelaySettings = true;
    m_lastSentRollbackDelayMode = m_rollbackDelayMode;
    m_lastSentRollbackFrameDelay = delay;
}

void KailleraP2PDialog::setRollbackDelayMode(int mode, bool announceToPeer, bool resetReady)
{
    mode = normalizeRollbackDelayMode(mode);
    const int previousDelay = effectiveRollbackFrameDelay();
    const bool changed = (m_rollbackDelayMode != mode);
    m_rollbackDelayMode = mode;

    if (changed && mode == kRollbackDelayModeCustom)
    {
        m_customRollbackFrameDelay = previousDelay;
    }

    if (m_isHost)
    {
        QSettings settings("RMG-K", "n02");
        settings.setValue("Rollback_FrameDelayMode", m_rollbackDelayMode);
        if (mode == kRollbackDelayModeCustom)
        {
            settings.setValue("Rollback_FrameDelay", m_customRollbackFrameDelay);
        }
    }

    updateRollbackDelayControls();

    if (changed && resetReady)
    {
        resetReadyState();
    }
    if (announceToPeer)
    {
        sendRollbackDelaySettings(true);
    }
}

void KailleraP2PDialog::setRollbackCustomFrameDelay(int delay, bool announceToPeer, bool resetReady)
{
    delay = clampRollbackFrameDelay(delay);
    const bool changed = (m_customRollbackFrameDelay != delay);
    m_customRollbackFrameDelay = delay;

    if (m_isHost)
    {
        QSettings settings("RMG-K", "n02");
        settings.setValue("Rollback_FrameDelay", m_customRollbackFrameDelay);
    }

    updateRollbackDelayControls();

    if (changed && resetReady)
    {
        resetReadyState();
    }
    if (changed && announceToPeer)
    {
        sendRollbackDelaySettings(true);
    }
}

void KailleraP2PDialog::updateRollbackDelayControls()
{
    if (!isRollbackMode())
    {
        return;
    }

    const bool inGame = m_rollbackGameActive;
    const int delay = effectiveRollbackFrameDelay();
    const bool editableDelayInput = m_isHost && m_rollbackDelayMode == kRollbackDelayModeCustom;
    m_rollbackFrameDelay = delay;

    if (m_frameDelayCombo != nullptr)
    {
        const bool blocked = m_frameDelayCombo->blockSignals(true);
        const int index = m_frameDelayCombo->findData(m_rollbackDelayMode);
        if (index >= 0)
        {
            m_frameDelayCombo->setCurrentIndex(index);
        }
        m_frameDelayCombo->setEnabled(m_isHost && !inGame);
        m_frameDelayCombo->blockSignals(blocked);
    }

    if (m_frameDelaySpin != nullptr)
    {
        const bool blocked = m_frameDelaySpin->blockSignals(true);
        m_frameDelaySpin->setValue(delay);
        m_frameDelaySpin->setVisible(true);
        m_frameDelaySpin->setEnabled(editableDelayInput && !inGame);
        m_frameDelaySpin->blockSignals(blocked);
    }

    if (m_delayLabel != nullptr)
    {
        m_delayLabel->setText(QString("Delay: %1f").arg(delay));
    }
}

void KailleraP2PDialog::updateNetcodeModeStatus()
{
    if (m_netcodeModeLabel == nullptr)
    {
        return;
    }

    if (isRollbackMode())
    {
        m_netcodeModeLabel->setText("Rollback");
        m_netcodeModeLabel->setStyleSheet(QString());
    }
    else
    {
        m_netcodeModeLabel->setText("Delay-only");
        m_netcodeModeLabel->setStyleSheet("color: #c03a3a;");
    }
}

void KailleraP2PDialog::updatePeerConnectionUI()
{
    if (m_peerStatusLabel != nullptr)
    {
        const QString displayName = m_peerName.trimmed().isEmpty() ? "Opponent" : m_peerName.trimmed();
        if (m_peerConnected)
        {
            m_peerStatusLabel->setText("Connected: " + displayName);
        }
        else if (!m_peerName.trimmed().isEmpty())
        {
            m_peerStatusLabel->setText("Left: " + displayName);
        }
        else
        {
            m_peerStatusLabel->setText(m_isHost ? "Waiting for opponent" : "Opponent: Not connected");
        }
    }

    if (m_btnKickPeer != nullptr)
    {
        const bool inGame = (isRollbackMode() && m_rollbackGameActive) || n02::isGameRunning();
        const bool showKick = m_isHost && m_peerConnected && !inGame;
        m_btnKickPeer->setVisible(showKick);
        m_btnKickPeer->setEnabled(showKick);
    }
}

void KailleraP2PDialog::resetReadyState()
{
    if (!m_ready && (m_btnReady == nullptr || !m_btnReady->isChecked()))
    {
        return;
    }

    m_ready = false;
    if (m_btnReady != nullptr)
    {
        m_btnReady->setChecked(false);
    }
    p2p_set_ready(false);
}

bool KailleraP2PDialog::parseGameLayerMessage(const QString& message, GameLayer& layer) const
{
    QString text = message.trimmed().toUpper();
    if (!text.startsWith(kGameLayerMessagePrefix))
    {
        return false;
    }

    text = text.mid(QString(kGameLayerMessagePrefix).size());
    if (text == kGameLayerRollback)
    {
        layer = GameLayer::Rollback;
        return true;
    }
    if (text == kGameLayerStandard)
    {
        layer = GameLayer::Standard;
        return true;
    }

    return false;
}

void KailleraP2PDialog::sendGameLayer()
{
    if (!m_isHost)
    {
        return;
    }

    QByteArray message = QByteArray(kGameLayerMessagePrefix) +
        (isRollbackMode() ? kGameLayerRollback : kGameLayerStandard);
    p2p_send_chat(message.data());
}

void KailleraP2PDialog::setGameLayer(GameLayer layer, bool announceToPeer, bool resetReady)
{
    const bool changed = (m_gameLayer != layer);
    m_gameLayer = layer;

    if (changed)
    {
        applyGameLayerUI();
        if (resetReady)
        {
            resetReadyState();
        }
    }

    if (m_isHost)
    {
        QSettings settings("RMG-K", "n02");
        settings.setValue("P2P_GameLayer", isRollbackMode() ? kGameLayerRollback : kGameLayerStandard);
    }

    if (announceToPeer)
    {
        sendGameLayer();
        sendRollbackDelaySettings(true);
    }
}

void KailleraP2PDialog::applyGameLayerUI()
{
    const bool rollback = isRollbackMode();

    if (m_standardLayerButton != nullptr)
    {
        const bool blocked = m_standardLayerButton->blockSignals(true);
        m_standardLayerButton->setChecked(!rollback);
        m_standardLayerButton->blockSignals(blocked);
    }
    if (m_rollbackLayerButton != nullptr)
    {
        const bool blocked = m_rollbackLayerButton->blockSignals(true);
        m_rollbackLayerButton->setChecked(rollback);
        m_rollbackLayerButton->blockSignals(blocked);
    }

    if (m_gameLayerStatusLabel != nullptr)
    {
        m_gameLayerStatusLabel->setText(rollback ?
            "Netcode: Rollback" :
            "Netcode: Delay-only");
    }

    if (m_frameDelayCombo != nullptr)
    {
        const bool blocked = m_frameDelayCombo->blockSignals(true);
        m_frameDelayCombo->clear();
        if (rollback)
        {
            m_frameDelayCombo->setMinimumWidth(126);
            m_frameDelayCombo->setMaximumWidth(150);
            m_frameDelayCombo->addItem("Default", kRollbackDelayModeDefault);
            m_frameDelayCombo->addItem("Lower delay", kRollbackDelayModeLower);
            m_frameDelayCombo->addItem("Higher delay", kRollbackDelayModeHigher);
            m_frameDelayCombo->addItem("Custom", kRollbackDelayModeCustom);
            const int modeIndex = m_frameDelayCombo->findData(m_rollbackDelayMode);
            m_frameDelayCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
        }
        else
        {
            m_frameDelayCombo->setMinimumWidth(175);
            m_frameDelayCombo->setMaximumWidth(16777215);
            m_frameDelayCombo->addItem("Auto");
            m_frameDelayCombo->addItem("1 frame (0-33ms)");
            m_frameDelayCombo->addItem("2 frames (34-67ms)");
            m_frameDelayCombo->addItem("3 frames (68-99ms)");
            m_frameDelayCombo->addItem("4 frames (100-133ms)");
            m_frameDelayCombo->addItem("5 frames (134-167ms)");
            m_frameDelayCombo->addItem("6 frames (168-199ms)");
            m_frameDelayCombo->addItem("7 frames (200-233ms)");
            m_frameDelayCombo->addItem("8 frames (234-267ms)");
            m_frameDelayCombo->addItem("9 frames (268+ms)");
            KailleraUIBridge::instance().setSelectedDelay(m_frameDelayCombo->currentIndex());
        }
        m_frameDelayCombo->blockSignals(blocked);
    }

    if (m_frameDelayLabel != nullptr)
    {
        m_frameDelayLabel->setText(rollback ? "Input Delay:" : "Frame Delay:");
    }
    if (!rollback && m_delayLabel != nullptr)
    {
        m_delayLabel->setText("Delay: --");
    }

    // Frame delay is baked into the GekkoNet (or Kaillera) session at start —
    // changing it mid-game has no effect until the next session, so lock the
    // row down for the duration of an active game.
    const bool inGame = (rollback && m_rollbackGameActive) || n02::isGameRunning();

    if (m_frameDelayRow != nullptr)
    {
        m_frameDelayRow->setVisible(true);
        m_frameDelayRow->setEnabled(true);
    }
    if (!rollback && m_frameDelayCombo != nullptr)
    {
        m_frameDelayCombo->setEnabled(m_isHost && !inGame);
    }
    if (rollback)
    {
        updateRollbackDelayControls();
    }
    else if (m_frameDelaySpin != nullptr)
    {
        m_frameDelaySpin->setVisible(false);
    }
    if (m_advancedSettingsButton != nullptr)
    {
        m_advancedSettingsButton->setVisible(m_isHost || rollback);
        m_advancedSettingsButton->setEnabled((m_isHost || rollback) && !inGame);
    }
    if (m_standardLayerButton != nullptr)
    {
        m_standardLayerButton->setEnabled(!inGame);
    }
    if (m_rollbackLayerButton != nullptr)
    {
        m_rollbackLayerButton->setEnabled(!inGame);
    }
    if (m_predictionWindowCombo != nullptr)
    {
        m_predictionWindowCombo->setEnabled(rollback && !inGame);
    }
    updateNetcodeModeStatus();
    updatePeerConnectionUI();
}

// ---- NAT traversal helpers ----

void KailleraP2PDialog::travResetState()
{
    m_travHostEnabled = false;
    m_travJoinEnabled = false;
    m_travLiveToken.clear();
    m_travRegAttempts = 0;
    m_travHostSessionSuspended = false;
    m_travHostFallbackActive = false;
    m_travHostIpPending = false;
    m_travHostIpPort.clear();
    m_travNextRegMs = 0;
    m_travNextKeepMs = 0;
    m_travNextJoinMs = 0;
    m_travJoinDeadlineMs = 0;
    m_travJoinCode.clear();
    m_travJoinGotHost = false;
    m_travJoinToken.clear();
    m_travJoinHostIp.clear();
    m_travJoinHostPort = 0;
    m_travNextConnectMs = 0;
    m_travJoinFallbackIpPort.clear();
    m_travJoinFallbackTried = false;
    m_travJoinBusy = false;
    m_travJoinPunchAttempts = 0;
    m_travHostPeerIp.clear();
    m_travHostPeerPort = 0;
    m_travHostPeerDeadlineMs = 0;
    m_travNextHostPunchMs = 0;
    m_ssrvCopyMyIpPending = false;
}

void KailleraP2PDialog::travLoadIdentity()
{
    const QString storedCode =
        normalizeTraversalCode(QString::fromStdString(CoreSettingsGetStringValue(SettingsID::Kaillera_P2PStaticCode)));
    const QString storedToken =
        QString::fromStdString(CoreSettingsGetStringValue(SettingsID::Kaillera_P2PStaticCodeOwnerToken)).trimmed();

    if (!storedCode.isEmpty() && !storedToken.isEmpty())
    {
        m_travCode = storedCode;
        m_travToken = storedToken;
    }
    else
    {
        m_travCode.clear();
        m_travToken.clear();
    }
}

void KailleraP2PDialog::travSaveIdentity() const
{
    CoreSettingsSetValue(SettingsID::Kaillera_P2PStaticCode, m_travCode.toStdString());
    CoreSettingsSetValue(SettingsID::Kaillera_P2PStaticCodeOwnerToken, m_travToken.toStdString());
    CoreSettingsSave();
}

void KailleraP2PDialog::travClearIdentity()
{
    m_travCode.clear();
    m_travToken.clear();
    m_travLiveToken.clear();

    CoreSettingsSetValue(SettingsID::Kaillera_P2PStaticCode, std::string(""));
    CoreSettingsSetValue(SettingsID::Kaillera_P2PStaticCodeOwnerToken, std::string(""));
    CoreSettingsSave();
}

void KailleraP2PDialog::travSendToServer(const QByteArray& msg)
{
    if (msg.isEmpty()) return;
    // NAT traversal messages must NOT include the trailing NUL byte.
    p2p_send_ssrv_packet(const_cast<char*>(msg.constData()), msg.size(),
                         const_cast<char*>(kN02TraversalHost), kN02TraversalPort);
}

void KailleraP2PDialog::travSendClaimAuto()
{
    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|CLAIM|AUTO";
    travSendToServer(msg);
    if (m_travHostEnabled)
        m_travRegAttempts++;
}

void KailleraP2PDialog::travSendClaimAck()
{
    if (m_travToken.isEmpty()) return;
    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|CLAIMACK|" + m_travToken.toUtf8();
    travSendToServer(msg);
}

void KailleraP2PDialog::travSendHostOpen()
{
    if (m_travToken.isEmpty()) return;
    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|HOSTOPEN|" + m_travToken.toUtf8();
    travSendToServer(msg);
}

void KailleraP2PDialog::travSendHostKeep()
{
    if (m_travToken.isEmpty()) return;
    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|HOSTKEEP|" + m_travToken.toUtf8();
    travSendToServer(msg);
}

void KailleraP2PDialog::travSendHostClose()
{
    if (m_travToken.isEmpty()) return;
    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|HOSTCLOSE|" + m_travToken.toUtf8();
    travSendToServer(msg);
}

void KailleraP2PDialog::travSendJoin()
{
    if (m_travJoinCode.isEmpty()) return;
    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|JOIN|" +
                     m_travJoinCode.toUtf8() + "|" +
                     QByteArray::number((quint32)monotonicTickCount());
    travSendToServer(msg);
}

void KailleraP2PDialog::travPunchEndpoint(const QString& hostIp, int hostPort, const QString& token)
{
    if (hostIp.isEmpty() || hostPort <= 0) return;

    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|PUNCH|" + token.toUtf8();
    QByteArray ipBytes = hostIp.toUtf8();

    // Send 10 punch packets to open NAT mappings
    for (int i = 0; i < 10; i++)
    {
        p2p_send_ssrv_packet(const_cast<char*>(msg.constData()), msg.size(),
                             const_cast<char*>(ipBytes.constData()), hostPort);
    }
}

bool KailleraP2PDialog::travTryFallbackConnect(const QString& reason)
{
    if (m_travJoinFallbackTried) return false;

    QString fallback;
    if (!m_travJoinFallbackIpPort.isEmpty())
        fallback = m_travJoinFallbackIpPort;
    else if (!m_travJoinHostIp.isEmpty())
        fallback = m_travJoinHostIp;
    if (fallback.isEmpty()) return false;

    m_travJoinFallbackTried = true;

    QString ip;
    int port = 0;
    QByteArray fallbackBytes = fallback.toUtf8();
    if (!tryExtractIPv4AndPort(fallbackBytes, ip, port))
        return false;
    if (port <= 0) port = 27886;

    m_chat->append("<span style='color:green;'>" + timestamp() +
                   "NAT traversal: " + reason.toHtmlEscaped() +
                   ". Falling back to direct connect</span>");

    QByteArray ipBytes = ip.toUtf8();
    if (!p2p_core_connect(ipBytes.data(), port))
    {
        m_chat->append("<span style='color:red;'>" + timestamp() +
                       "NAT traversal: fallback connect failed</span>");
        return false;
    }
    return true;
}

void KailleraP2PDialog::updateHostCodeUI()
{
    if (!m_isHost || !m_connectCodeEdit) return;

    if (m_hostGroup) m_hostGroup->setVisible(true);
    const bool codeActive = !m_travHostSessionSuspended;
    if (m_connectCodeEdit) m_connectCodeEdit->setEnabled(codeActive);
    if (m_copyAction) m_copyAction->setEnabled(codeActive);
    if (m_enlistCheck) m_enlistCheck->setEnabled(codeActive);

    if (!codeActive)
    {
        m_connectCodeEdit->setText("(peer connected)");
        return;
    }

    if (!m_travCode.isEmpty())
    {
        m_connectCodeEdit->setText(m_travCode);
    }
    else if (!m_travHostIpPort.isEmpty())
    {
        m_connectCodeEdit->setText(m_travHostIpPort);
    }
    else if (m_travHostIpPending)
    {
        m_connectCodeEdit->setText("(checking ip)");
        if (m_copyAction) m_copyAction->setEnabled(false);
    }
    else
    {
        m_connectCodeEdit->setText("(waiting)");
        if (m_copyAction) m_copyAction->setEnabled(false);
    }
}

void KailleraP2PDialog::ssrvSend(const QByteArray& cmd)
{
    // Send to super-server (SSRV). Includes trailing NUL byte per ssrv protocol.
    int sendLen = cmd.size() + 1;
    p2p_send_ssrv_packet(const_cast<char*>(cmd.constData()), sendLen,
                         const_cast<char*>(kSsrvHost), kSsrvPort);
}

void KailleraP2PDialog::ssrvWhatIsMyIp()
{
    m_ssrvCopyMyIpPending = true;
    ssrvSend("WHATISMYIP");
}

QString KailleraP2PDialog::buildEnlistAppName()
{
    extern char APP[128];
    QString app = QString::fromUtf8(APP);

    // Append traversal code if hosting by code
    if (m_isHost && m_travHostEnabled && !m_travCode.isEmpty())
    {
        app += " {CC:" + m_travCode + "}";
    }
    return app;
}

void KailleraP2PDialog::enlistGame()
{
    QString app = buildEnlistAppName();
    int port = p2p_core_get_port();

    // Use ENLISP (with port) when hosting by code or on non-default port
    if (m_travHostEnabled || port != 27886)
    {
        QByteArray msg = QString("ENLISP %1|%2|%3|%4")
            .arg(m_gameName, app, m_username)
            .arg(port).toUtf8();
        ssrvSend(msg);
    }
    else
    {
        QByteArray msg = QString("ENLIST %1|%2|%3")
            .arg(m_gameName, app, m_username).toUtf8();
        ssrvSend(msg);
    }
}

void KailleraP2PDialog::unenlistGame()
{
    ssrvSend("UNENLIST");
}

// ---- SSRV packet handler (NAT traversal + super-server responses) ----

void KailleraP2PDialog::onSsrvPacketReceived(QByteArray cmd, QByteArray saddr)
{
    if (cmd.isEmpty()) return;

    // Null-terminate for safe string operations
    QByteArray cmdBuf = cmd;
    if (cmdBuf.size() > 0 && cmdBuf[cmdBuf.size() - 1] != '\0')
        cmdBuf.append('\0');

    const char* cmdStr = cmdBuf.constData();

    // ---- NAT traversal ----
    const QByteArray traversalPrefix = QByteArray(kN02TraversalProtocol) + "|";
    if (strncmp(cmdStr, traversalPrefix.constData(), traversalPrefix.size()) == 0)
    {
        // Split the message by '|'
        QByteArray parseBuf = cmdBuf;
        char* parts[8] = { nullptr };
        int partCount = 0;
        parts[partCount++] = parseBuf.data();
        for (char* p = parseBuf.data(); *p && partCount < 8; p++)
        {
            if (*p == '|')
            {
                *p = 0;
                parts[partCount++] = p + 1;
            }
        }

        if (partCount < 2) return;
        const char* type = parts[1];

        if (strcmp(type, "CLAIMOK") == 0 && partCount >= 4)
        {
            if (!m_travHostEnabled) return;

            m_travCode = normalizeTraversalCode(QString::fromUtf8(parts[2]));
            m_travToken = QString::fromUtf8(parts[3]);
            m_travLiveToken.clear();
            m_travRegAttempts = 0;
            m_travHostIpPending = false;
            m_travHostIpPort.clear();
            travSaveIdentity();

            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "Claimed connect code: " + m_travCode.toHtmlEscaped() + "</span>");

            // Auto-copy to clipboard
            QApplication::clipboard()->setText(m_travCode);
            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "Copied connect code to clipboard</span>");

            updateHostCodeUI();
            travSendClaimAck();
            travSendHostOpen();

            return;
        }

        if (strcmp(type, "CLAIMSUGGEST") == 0 && partCount >= 4)
        {
            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "Requested code " + QString::fromUtf8(parts[2]).toHtmlEscaped() +
                           " is unavailable. Suggested: " +
                           QString::fromUtf8(parts[3]).toHtmlEscaped() + "</span>");
            return;
        }

        if (strcmp(type, "HOSTOK") == 0 && partCount >= 6)
        {
            if (!m_travHostEnabled) return;

            m_travCode = normalizeTraversalCode(QString::fromUtf8(parts[2]));
            m_travLiveToken = QString::fromUtf8(parts[3]);
            m_travRegAttempts = 0;
            m_travHostIpPending = false;
            m_travHostIpPort.clear();
            m_travNextRegMs = 0;
            m_travNextKeepMs = 0;
            travSaveIdentity();
            updateHostCodeUI();

            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "Host session opened for " + m_travCode.toHtmlEscaped() + "</span>");

            if (m_enlistCheck && m_enlistCheck->isChecked())
                enlistGame();

            return;
        }

        // HOST: Joiner received the host's endpoint from the traversal server.
        if (strcmp(type, "HOST") == 0 && partCount >= 5)
        {
            if (!m_travJoinEnabled) return;

            QString token = QString::fromUtf8(parts[2]);
            QString hostIp = QString::fromUtf8(parts[3]);
            int hostPort = atoi(parts[4]);

            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "NAT traversal: got host endpoint</span>");

            m_travJoinToken = token;
            m_travJoinHostIp = hostIp;
            m_travJoinHostPort = hostPort;
            m_travJoinGotHost = true;

            if (m_travJoinFallbackIpPort.isEmpty())
            {
                m_travJoinFallbackIpPort = hostIp;
                m_travJoinFallbackTried = false;
            }

            // Stop asking the server
            m_travNextJoinMs = 0;

            // Try connecting immediately
            m_travJoinPunchAttempts++;
            travPunchEndpoint(m_travJoinHostIp, m_travJoinHostPort, m_travJoinToken);

            QByteArray ipBytes = m_travJoinHostIp.toUtf8();
            if (!p2p_core_connect(ipBytes.data(), m_travJoinHostPort))
            {
                travTryFallbackConnect("connect failed");
            }
            if (m_travJoinPunchAttempts >= 3)
            {
                travTryFallbackConnect("trying direct IP/port");
            }
            return;
        }

        // PEER: Host received a peer's endpoint — need to punch their NAT.
        if (strcmp(type, "PEER") == 0 && partCount >= 5)
        {
            if (!m_travHostEnabled) return;

            QString token = QString::fromUtf8(parts[2]);
            QString peerIp = QString::fromUtf8(parts[3]);
            int peerPort = atoi(parts[4]);

            if (!m_travLiveToken.isEmpty() && token != m_travLiveToken) return;

            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "NAT traversal: got peer endpoint</span>");

            m_travHostPeerIp = peerIp;
            m_travHostPeerPort = peerPort;
            m_travHostPeerDeadlineMs = QDateTime::currentMSecsSinceEpoch() + 15000;
            m_travNextHostPunchMs = 0;

            travPunchEndpoint(peerIp, peerPort, token);
            return;
        }

        // ERR: Server error
        if (strcmp(type, "ERR") == 0 && partCount >= 3)
        {
            const QString reason = QString::fromUtf8(parts[2]);

            if (m_travJoinEnabled && strcmp(parts[2], "BUSY") == 0)
            {
                m_travJoinBusy = true;
                m_travJoinDeadlineMs = QDateTime::currentMSecsSinceEpoch();
                m_travNextJoinMs = 0;
                return;
            }

            if (m_travJoinEnabled && (strcmp(parts[2], "OFFLINE") == 0 || strcmp(parts[2], "UNKNOWNCODE") == 0))
            {
                m_travJoinEnabled = false;
                m_travJoinDeadlineMs = 0;
                m_travNextJoinMs = 0;
            }

            if (m_travHostEnabled && strcmp(parts[2], "NOAUTH") == 0)
            {
                m_chat->append("<span style='color:green;'>" + timestamp() +
                               "Saved connect code identity was rejected. Claiming a new code.</span>");
                travClearIdentity();
                m_travRegAttempts = 0;
                m_travNextRegMs = 0;
                m_travNextKeepMs = 0;
                m_travHostIpPending = false;
                updateHostCodeUI();
                return;
            }

            if (m_travHostEnabled && strcmp(parts[2], "NOSESSION") == 0)
            {
                m_travLiveToken.clear();
                m_travNextRegMs = 0;
                m_travNextKeepMs = 0;
            }

            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "NAT traversal error: " + reason.toHtmlEscaped() + "</span>");
            return;
        }

        // OK: Acknowledgement
        if (strcmp(type, "OK") == 0) return;

        // PUNCH: No-op payload for NAT hole punching
        if (strcmp(type, "PUNCH") == 0) return;

        return;
    }

    // ---- PINGRQ: Super-server ping request ----
    if (strncmp(cmdStr, "PINGRQ", 6) == 0)
    {
        if (!saddr.isEmpty())
        {
            p2p_send_ssrv_packet(const_cast<char*>("xxxxxxxxxx"), 11, saddr.data());
        }
        return;
    }

    // ---- MSG: Ignore ----
    if (strncmp(cmdStr, "MSG", 3) == 0) return;

    // ---- WHATISMYIP response handling ----
    if (m_ssrvCopyMyIpPending)
    {
        QString ip;
        int port = 0;
        if (tryExtractIPv4AndPort(cmd, ip, port))
        {
            if (port <= 0) port = p2p_core_get_port();
            if (port > 0)
            {
                m_travHostIpPort = ip + ":" + QString::number(port);
                m_travHostIpPending = false;
                updateHostCodeUI();

                if (m_travHostFallbackActive)
                {
                    m_chat->append("<span style='color:green;'>" + timestamp() +
                                   "Your IP address is: " + m_travHostIpPort + "</span>");
                }
            }
        }
        m_ssrvCopyMyIpPending = false;
    }
}

// ---- Signal handlers ----

void KailleraP2PDialog::onChatReceived(QString nick, QString message)
{
    GameLayer layer = GameLayer::Standard;
    if (parseGameLayerMessage(message, layer))
    {
        if (!m_isHost)
        {
            setGameLayer(layer, false, true);
        }
        return;
    }

    int rollbackDelayMode = kRollbackDelayModeDefault;
    int rollbackFrameDelay = kDefaultRollbackFrameDelay;
    if (parseRollbackDelayMessage(message, rollbackDelayMode, rollbackFrameDelay))
    {
        if (!m_isHost)
        {
            m_rollbackDelayMode = rollbackDelayMode;
            m_rollbackFrameDelay = rollbackFrameDelay;
            m_hasRemoteRollbackDelaySettings = true;
            updateRollbackDelayControls();
        }
        return;
    }

    m_chat->append("<b>" + nick.toHtmlEscaped() + ":</b> " + message.toHtmlEscaped());
}

void KailleraP2PDialog::onGameStarted(QString game, int player, int maxPlayers)
{
    if (isRollbackMode())
    {
        if (m_rollbackGameActive)
        {
            return;
        }

        char peerIp[128] = {};
        int peerP2PPort = 0;
        const int localP2PPort = p2p_core_get_port();
        const int frameDelay = effectiveRollbackFrameDelay();
        const int predictionWindow = (m_predictionWindowCombo != nullptr) ?
            m_predictionWindowCombo->currentIndex() + 1 : kDefaultRollbackPredictionWindow;
        if (!p2p_core_get_peer_endpoint(peerIp, sizeof(peerIp), &peerP2PPort))
        {
            m_chat->append("<span style='color:red;'>" + timestamp() + "Could not get rollback peer endpoint.</span>");
            return;
        }
        if (localP2PPort <= 0 || peerP2PPort <= 0)
        {
            m_chat->append("<span style='color:red;'>" + timestamp() + "Could not get rollback game ports.</span>");
            return;
        }

        m_rollbackGameActive = true;
        applyGameLayerUI();
        m_chat->append("<span style='color:green;'>" + timestamp() + "Rollback game started: " + game.toHtmlEscaped() + "</span>");
        emit rollbackSessionReady(game, QString::fromUtf8(peerIp), localP2PPort, peerP2PPort, player, frameDelay, predictionWindow);
        return;
    }

    (void)player;
    (void)maxPlayers;
    m_chat->append("<span style='color:green;'>" + timestamp() + "Game started: " + game.toHtmlEscaped() + "</span>");
    applyGameLayerUI();
}

void KailleraP2PDialog::onGameEnded()
{
    if (isRollbackMode())
    {
        const bool wasActive = m_rollbackGameActive;
        const bool wasReady = m_ready || (m_btnReady != nullptr && m_btnReady->isChecked());
        m_rollbackGameActive = false;
        m_ready = false;
        if (m_btnReady) m_btnReady->setChecked(false);
        applyGameLayerUI();
        if (wasActive || wasReady)
        {
            m_chat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp() + "Game ended.</span>");
        }
        if (wasActive)
        {
            QTimer::singleShot(0, this, []() {
                CoreStopEmulation();
            });
        }
        return;
    }

    CoreMarkKailleraGameInactive();
    CoreStopEmulation();
    m_ready = false;
    if (m_btnReady) m_btnReady->setChecked(false);
    applyGameLayerUI();
    m_chat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp() + "Game ended.</span>");
}

void KailleraP2PDialog::onClientDropped(QString nick, int player)
{
    (void)player;
    if (m_pingLabel) m_pingLabel->setText("Ping: --");
    m_chat->append("<span style='color:red;'>" + timestamp() + nick.toHtmlEscaped() + " dropped.</span>");
}

void KailleraP2PDialog::onDebug(QString message)
{
    m_chat->append("<span style='color:green;'>" + message.toHtmlEscaped() + "</span>");
}

void KailleraP2PDialog::onHostedGame(QString game)
{
    if (m_isHost)
    {
        return;
    }

    m_gameName = game;
    if (m_gameLabel != nullptr)
    {
        m_gameLabel->setText("Game: " + m_gameName);
    }

    if (localGameListContains(m_gameName))
    {
        if (m_btnReady != nullptr)
        {
            m_btnReady->setEnabled(true);
        }
        return;
    }

    m_ready = false;
    if (m_btnReady != nullptr)
    {
        m_btnReady->setChecked(false);
        m_btnReady->setEnabled(false);
    }

    const QString message = "The ROM '" + m_gameName + "' is not in your list.";
    m_chat->append("<span style='color:red;'>" + timestamp() + message.toHtmlEscaped() + "</span>");
    QMessageBox::warning(this, "P2P Join", message);
    reject();
}

void KailleraP2PDialog::onPingUpdated(int ping)
{
    m_lastPing = ping;

    if (m_pingLabel != nullptr && ping >= 0)
    {
        m_pingLabel->setText(QString("Ping: %1ms").arg(ping));
    }
    else if (m_pingLabel != nullptr)
    {
        m_pingLabel->setText("Ping: --");
    }

    if (isRollbackMode())
    {
        updateRollbackDelayControls();
        sendRollbackDelaySettings(false);
    }
}

void KailleraP2PDialog::onPeerJoined()
{
    if (m_pingLabel) m_pingLabel->setText("Ping: measuring...");
    m_peerConnected = true;
    updatePeerConnectionUI();
    m_chat->append("<span style='color:green;'>" + timestamp() + "Peer connected.</span>");
    if (m_isHost && CoreSettingsGetBoolValue(SettingsID::Kaillera_BeepOnJoin))
    {
        QApplication::beep();
    }
    if (m_isHost && CoreSettingsGetBoolValue(SettingsID::Kaillera_FlashOnJoin) && !isActiveWindow())
    {
        QApplication::alert(this);
    }
    sendGameLayer();
    sendRollbackDelaySettings(true);
    m_travHostPeerIp.clear();
    m_travHostPeerPort = 0;
    m_travHostPeerDeadlineMs = 0;
    m_travNextHostPunchMs = 0;

    if (m_isHost && m_travHostEnabled)
    {
        m_travHostSessionSuspended = true;
        m_travNextRegMs = 0;
        m_travNextKeepMs = 0;

        if (!m_travLiveToken.isEmpty())
        {
            travSendHostClose();
            m_travLiveToken.clear();
        }
    }

    // Remove from public game list while peer is connected
    if (m_isHost && m_enlistCheck && m_enlistCheck->isChecked())
        unenlistGame();
}

void KailleraP2PDialog::onPeerLeft()
{
    const bool kickedPeer = m_peerKickPending;
    m_peerKickPending = false;
    if (m_pingLabel) m_pingLabel->setText("Ping: --");
    m_peerConnected = false;
    m_lastPing = -1;
    m_hasRemoteRollbackDelaySettings = false;
    m_hasSentRollbackDelaySettings = false;
    m_lastSentRollbackDelayMode = -1;
    m_lastSentRollbackFrameDelay = -1;
    if (isRollbackMode())
    {
        updateRollbackDelayControls();
    }
    updatePeerConnectionUI();
    if (!kickedPeer)
    {
        m_chat->append("<span style='color:red;'>" + timestamp() + "Peer disconnected.</span>");
    }
    m_ready = false;
    if (m_btnReady) m_btnReady->setChecked(false);
    if (!m_isHost && m_btnReady != nullptr)
    {
        m_btnReady->setEnabled(false);
    }

    // Clear peer punching state (always, regardless of trav mode)
    m_travHostPeerIp.clear();
    m_travHostPeerPort = 0;
    m_travHostPeerDeadlineMs = 0;
    m_travNextHostPunchMs = 0;

    if (m_isHost && m_travHostEnabled)
    {
        m_travHostSessionSuspended = false;
        m_travNextRegMs = 0;
        m_travNextKeepMs = 0;
    }

    // Re-enlist on public game list now that we're waiting for a new peer
    if (m_isHost && m_enlistCheck && m_enlistCheck->isChecked())
        enlistGame();
}

void KailleraP2PDialog::onPeerInfo(QString name, QString app)
{
    m_peerName = name.trimmed();
    updatePeerConnectionUI();
    emit peerNicknameResolved(name);
    m_chat->append("<span style='color:green;'>" + timestamp() + "Peer: " +
                   name.toHtmlEscaped() + " (" + app.toHtmlEscaped() + ")</span>");
}

void KailleraP2PDialog::onFodippResult(QString host)
{
    // FODIPP result serves as an additional fallback for IP display
    if (m_isHost && m_travHostIpPort.isEmpty())
    {
        int port = p2p_core_get_port();
        m_travHostIpPort = host + ":" + QString::number(port);
        m_travHostIpPending = false;
        updateHostCodeUI();
    }
    m_chat->append("<span style='color:green;'>" + timestamp() + "External IP: " + host.toHtmlEscaped() + "</span>");
}

// ---- Button actions ----

void KailleraP2PDialog::onSendChat()
{
    QString text = m_chatInput->text().trimmed();
    if (text.isEmpty()) return;

    QByteArray utf8 = text.toUtf8();
    p2p_send_chat(utf8.data());
    m_chatInput->clear();
}

void KailleraP2PDialog::onReady()
{
    if (!m_isHost && m_gameName.isEmpty())
    {
        m_ready = false;
        if (m_btnReady != nullptr)
        {
            m_btnReady->setChecked(false);
        }
        return;
    }

    if (m_isHost)
    {
        sendGameLayer();
        sendRollbackDelaySettings(true);
    }

    m_ready = (m_btnReady != nullptr) ? m_btnReady->isChecked() : !m_ready;

    p2p_set_ready(m_ready);

    if (m_ready)
    {
        m_chat->append(timestamp() + "Ready!");
    }
    else
    {
        m_chat->append(timestamp() + "Not ready.");
    }
}

void KailleraP2PDialog::onDrop()
{
    const bool rollbackWasActive = isRollbackMode() && m_rollbackGameActive;
    p2p_drop_game();
    if (rollbackWasActive && m_rollbackGameActive)
    {
        onGameEnded();
    }
}

void KailleraP2PDialog::onKickPeer()
{
    if (!m_isHost || !m_peerConnected || (isRollbackMode() && m_rollbackGameActive) || n02::isGameRunning())
    {
        updatePeerConnectionUI();
        return;
    }

    const QString kickedName = m_peerName.trimmed().isEmpty() ? "opponent" : m_peerName.trimmed();
    m_peerKickPending = true;
    if (p2p_kick_peer())
    {
        m_peerConnected = false;
        updatePeerConnectionUI();
        m_chat->append("<span style='color:red;'>" + timestamp() +
                       "Kicked " + kickedName.toHtmlEscaped() + " from the lobby.</span>");
    }
    else
    {
        m_peerKickPending = false;
        updatePeerConnectionUI();
        m_chat->append("<span style='color:red;'>" + timestamp() +
                       "Could not kick " + kickedName.toHtmlEscaped() + ".</span>");
    }
}

void KailleraP2PDialog::onCopyConnectCode()
{
    if (!m_connectCodeEdit) return;

    // Copy the best available code/address
    if (!m_travCode.isEmpty())
    {
        QApplication::clipboard()->setText(m_travCode);
        m_chat->append(timestamp() + "Copied connect code to clipboard");
    }
    else if (!m_travHostIpPort.isEmpty())
    {
        QApplication::clipboard()->setText(m_travHostIpPort);
        m_chat->append(timestamp() + "Copied " + m_travHostIpPort + " to clipboard");
    }
    else
    {
        return;
    }

    if (m_copyAction != nullptr)
    {
        m_copyAction->setIcon(themedP2PIcon("copy-check-line"));
        m_copyAction->setToolTip("Copied");
    }

    if (m_copyFeedbackTimer != nullptr)
    {
        m_copyFeedbackTimer->start(1200);
    }
}

void KailleraP2PDialog::onStepTimer()
{
    if (isRollbackMode() && m_rollbackGameActive)
    {
        p2p_rollback_process_control();
        return;
    }

    // Standard P2P gameplay polls from the emulation thread.
    if (!n02::isGameRunning())
    {
        p2p_step();
    }
    n02::processStateMachineStep();
}

// ---- NAT traversal housekeeping (1-second timer) ----

void KailleraP2PDialog::onTravTimer()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_travTimerStep++;

    // Match the old p2p lobby behavior: keep refreshing ping once per second
    // while connected, but stop once the game has actually started.
    if (!(isRollbackMode() && m_rollbackGameActive) && !n02::isGameRunning() && p2p_is_connected())
    {
        p2p_ping();
    }

    // ---- HOST: NAT traversal registration & keepalive ----
    if (m_isHost && m_travHostEnabled)
    {
        if (m_travHostSessionSuspended)
        {
            // Keep the static code identity locally, but do not advertise or keep
            // a live host session open while a peer is connected.
        }
        else if (m_travToken.isEmpty() || m_travCode.isEmpty())
        {
            if (m_travNextRegMs == 0 || now >= m_travNextRegMs)
            {
                if (m_travRegAttempts >= 4)
                {
                    // Fallback to IP-based hosting
                    m_travHostEnabled = false;
                    m_travHostFallbackActive = true;
                    m_travNextRegMs = 0;
                    m_travNextKeepMs = 0;
                    m_travHostIpPending = true;
                    m_travHostIpPort.clear();
                    updateHostCodeUI();

                    m_chat->append("<span style='color:red;'>" + timestamp() +
                                   "Failed to get a connect code from the NAT server. "
                                   "Hosting by IP instead. You may need to manually port forward.</span>");
                    ssrvWhatIsMyIp();
                }
                else
                {
                    travSendClaimAuto();
                    m_travNextRegMs = now + 2000;
                }
            }
        }
        else if (m_travLiveToken.isEmpty())
        {
            if (m_travNextRegMs == 0 || now >= m_travNextRegMs)
            {
                travSendHostOpen();
                m_travNextRegMs = now + 2000;
            }
        }
        else if (m_travNextKeepMs == 0 || now >= m_travNextKeepMs)
        {
            travSendHostKeep();
            m_travNextKeepMs = now + 10000;
        }

        // While waiting for the peer's LOGN_REQ, keep punching their endpoint
        if (!p2p_is_connected() &&
            !m_travHostPeerIp.isEmpty() &&
            m_travHostPeerPort > 0 &&
            m_travHostPeerDeadlineMs != 0 &&
            now < m_travHostPeerDeadlineMs)
        {
            if (m_travNextHostPunchMs == 0 || now >= m_travNextHostPunchMs)
            {
                travPunchEndpoint(m_travHostPeerIp, m_travHostPeerPort, m_travLiveToken);
                m_travNextHostPunchMs = now + 1000;
            }
        }
    }

    // ---- JOIN: traversal code lookup & connection retries ----
    if (!m_isHost && m_travJoinEnabled && !p2p_is_connected())
    {
        if (m_travJoinDeadlineMs != 0 && now >= m_travJoinDeadlineMs)
        {
            // Timed out
            if (m_travJoinBusy)
            {
                m_chat->append("<span style='color:red;'>" + timestamp() +
                               "NAT traversal: host is busy. Please wait and try again.</span>");
            }
            else
            {
                m_chat->append("<span style='color:red;'>" + timestamp() +
                               "NAT traversal: timed out (try direct IP/port-forwarding or server mode)</span>");
            }
            m_travJoinEnabled = false;
            if (!m_travJoinBusy)
            {
                travTryFallbackConnect("timed out");
            }
            m_travJoinBusy = false;
        }
        else if (!m_travJoinGotHost)
        {
            // Keep asking for the host's address
            if (m_travNextJoinMs == 0 || now >= m_travNextJoinMs)
            {
                travSendJoin();
                m_travNextJoinMs = now + 3000;
            }
        }
        else
        {
            // Got host endpoint — retry punch + connect
            if (m_travNextConnectMs == 0 || now >= m_travNextConnectMs)
            {
                m_travJoinPunchAttempts++;
                travPunchEndpoint(m_travJoinHostIp, m_travJoinHostPort, m_travJoinToken);

                QByteArray ipBytes = m_travJoinHostIp.toUtf8();
                p2p_core_connect(ipBytes.data(), m_travJoinHostPort);
                m_travNextConnectMs = now + 1000;

                if (m_travJoinPunchAttempts >= 3)
                {
                    travTryFallbackConnect("trying direct IP/port");
                }
            }
        }
    }
    else if (!m_isHost && m_travJoinEnabled && p2p_is_connected())
    {
        // Connected — stop traversal retries
        m_travJoinEnabled = false;
        m_travJoinDeadlineMs = 0;
    }

    // ---- Periodic re-enlist on public game list (every 30s while waiting) ----
    if (m_travTimerStep % 30 == 0 && !p2p_is_connected() &&
        m_isHost && m_enlistCheck && m_enlistCheck->isChecked())
    {
        enlistGame();
    }
}

#endif // NETPLAY
