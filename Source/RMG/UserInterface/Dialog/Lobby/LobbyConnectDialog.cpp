/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 */
#ifdef NETPLAY

#include "LobbyConnectDialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QDialogButtonBox>

using namespace UserInterface::Dialog;

LobbyConnectDialog::LobbyConnectDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Connect to RMG-K Lobby");
    setModal(true);
    buildUi();
    loadSettings();
    validateInput();
}

void LobbyConnectDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);

    auto* form = new QFormLayout;

    m_serverCombo = new QComboBox(this);
    m_serverCombo->setEditable(true);
    m_serverCombo->addItem("ws://localhost:8080/ws");
    m_serverCombo->addItem("ws://lobby.rmgk.net:8080/ws"); // placeholder default prod URL
    form->addRow("Server:", m_serverCombo);

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setMaxLength(16);
    m_usernameEdit->setPlaceholderText("3-16 characters: letters, numbers, _ - .");
    auto* validator = new QRegularExpressionValidator(
        QRegularExpression(R"([A-Za-z0-9_\-\.]{1,16})"), this);
    m_usernameEdit->setValidator(validator);
    form->addRow("Username:", m_usernameEdit);

    root->addLayout(form);

    m_validationLbl = new QLabel(this);
    m_validationLbl->setStyleSheet("color: gray;");
    root->addWidget(m_validationLbl);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_connectButton = btnBox->addButton("Connect", QDialogButtonBox::AcceptRole);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_connectButton, &QPushButton::clicked, this, &LobbyConnectDialog::onConnect);

    root->addWidget(btnBox);

    connect(m_usernameEdit, &QLineEdit::textChanged, this, &LobbyConnectDialog::validateInput);
    connect(m_serverCombo,  &QComboBox::editTextChanged, this, &LobbyConnectDialog::validateInput);
}

void LobbyConnectDialog::validateInput()
{
    const QString user = m_usernameEdit->text().trimmed();
    const QString srv  = m_serverCombo->currentText().trimmed();

    QString reason;
    if (srv.isEmpty())
        reason = "Server URL required.";
    else if (!srv.startsWith("ws://") && !srv.startsWith("wss://"))
        reason = "Server URL must start with ws:// or wss://";
    else if (user.length() < 3)
        reason = "Username must be at least 3 characters.";

    m_validationLbl->setText(reason);
    m_connectButton->setEnabled(reason.isEmpty());
}

void LobbyConnectDialog::onConnect()
{
    m_serverUrl = m_serverCombo->currentText().trimmed();
    m_username  = m_usernameEdit->text().trimmed();
    saveSettings();
    accept();
}

void LobbyConnectDialog::loadSettings()
{
    QSettings s;
    const QString lastSrv  = s.value("Lobby/ServerUrl", "ws://localhost:8080/ws").toString();
    const QString lastUser = s.value("Lobby/Username").toString();

    const int idx = m_serverCombo->findText(lastSrv);
    if (idx >= 0)
        m_serverCombo->setCurrentIndex(idx);
    else
        m_serverCombo->setEditText(lastSrv);

    m_usernameEdit->setText(lastUser);
}

void LobbyConnectDialog::saveSettings()
{
    QSettings s;
    s.setValue("Lobby/ServerUrl", m_serverUrl);
    s.setValue("Lobby/Username",  m_username);
}

#endif // NETPLAY
