/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "Translation.hpp"

#include <RMG-Core/Settings.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace
{
QString resolve_language()
{
    const QString configuredLanguage = QString::fromStdString(
        CoreSettingsGetStringValue(SettingsID::GUI_Language));

    if (configuredLanguage == "ja_JP" || configuredLanguage == "en")
    {
        return configuredLanguage;
    }

    if (configuredLanguage != "system")
    {
        qWarning("Unknown GUI language setting; falling back to English");
        return "en";
    }

    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString& language : uiLanguages)
    {
        const QString normalizedLanguage = language.toLower().replace('_', '-');
        if (normalizedLanguage == "ja" || normalizedLanguage.startsWith("ja-"))
        {
            return "ja_JP";
        }
    }

    return "en";
}

QString application_translation_directory()
{
    return QCoreApplication::applicationDirPath() + "/Data/Translations";
}
} // namespace

namespace Translation
{
void Install(QApplication& application, QTranslator& qtTranslator, QTranslator& applicationTranslator)
{
    if (resolve_language() != "ja_JP")
    {
        return;
    }

    const QString translationDirectory = application_translation_directory();
    const bool qtTranslationLoaded =
        qtTranslator.load("qtbase_ja.qm", translationDirectory) ||
        qtTranslator.load("qtbase_ja.qm", QLibraryInfo::path(QLibraryInfo::TranslationsPath));
    const bool applicationTranslationLoaded =
        applicationTranslator.load("rmg_ja_JP.qm", translationDirectory);

    if (qtTranslationLoaded)
    {
        application.installTranslator(&qtTranslator);
    }
    else
    {
        qWarning("Failed to load Qt base translation");
    }

    if (applicationTranslationLoaded)
    {
        application.installTranslator(&applicationTranslator);
    }
    else
    {
        qWarning("Failed to load application translation; application strings will use English");
    }
}
} // namespace Translation
