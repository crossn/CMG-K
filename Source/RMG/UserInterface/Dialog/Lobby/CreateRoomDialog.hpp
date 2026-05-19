/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 */
#ifndef CREATEROOMDIALOG_HPP
#define CREATEROOMDIALOG_HPP

#ifdef NETPLAY

#include <QDialog>
#include <QString>
#include <QMap>

#include <RMG-Core/RomSettings.hpp>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;

namespace UserInterface
{
namespace Dialog
{

// Modal form for creating a lobby room. Returns the chosen settings to the
// caller via accessors; caller is responsible for sending the protocol message.
class CreateRoomDialog : public QDialog
{
    Q_OBJECT

public:
    CreateRoomDialog(const QString& defaultUsername,
                     const QMap<QString, CoreRomSettings>& roms,
                     QWidget* parent = nullptr);
    ~CreateRoomDialog() override = default;

    // Accessors valid only after exec() returns Accepted.
    QString name()       const { return m_name; }
    QString romName()    const { return m_romName; }
    QString romMd5()     const { return m_romMd5; }
    QString romRegion()  const { return m_romRegion; }
    int     maxPlayers() const { return m_maxPlayers; }
    int     delay()      const { return m_delay; }
    int     prediction() const { return m_prediction; }
    QString password()   const { return m_password; }

    // Called by the lobby when the server replies ROOM_CREATE_FAIL.
    // Re-enables the form so the user can edit and retry.
    void showCreateFailure(const QString& reason);

signals:
    // Emitted when user clicks Create with valid input. The dialog stays
    // open in a "Creating..." state until either showCreateFailure() is
    // called (re-enables form) or the caller calls accept().
    void createRequested();

private slots:
    void onCreateClicked();
    void onPasswordToggled(bool enabled);
    void validateInput();

private:
    void buildUi(const QString& defaultUsername);
    void populateRoms(const QMap<QString, CoreRomSettings>& roms);
    void loadDefaults();
    void saveDefaults();
    void setFormEnabled(bool enabled);
    static QString displayGameName(const QString& goodName, const QString& filePath);

    // UI — delay/prediction spinners live in the in-room view now, not here.
    QLineEdit*   m_nameEdit       = nullptr;
    QComboBox*   m_romCombo       = nullptr;
    QSpinBox*    m_maxPlayersSpin = nullptr;
    QCheckBox*   m_passwordCheck  = nullptr;
    QLineEdit*   m_passwordEdit   = nullptr;
    QPushButton* m_createButton   = nullptr;
    QPushButton* m_cancelButton   = nullptr;
    QLabel*      m_statusLabel    = nullptr;

    // Accessor backing
    QString m_name;
    QString m_romName;
    QString m_romMd5;
    QString m_romRegion;
    int     m_maxPlayers = 2;
    int     m_delay      = 2;
    int     m_prediction = 7;
    QString m_password;
};

} // namespace Dialog
} // namespace UserInterface

#endif // NETPLAY
#endif // CREATEROOMDIALOG_HPP
