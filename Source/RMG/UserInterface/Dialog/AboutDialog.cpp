/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "AboutDialog.hpp"

#include <RMG-Core/Version.hpp>

using namespace UserInterface::Dialog;

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    this->setupUi(this);
    // prepare modified html with CMG-K and upstream base version metadata
    QString html = this->textBrowser->toHtml();
    html.replace("{cmgk_intro}", tr("is a community build based on RMG-K, with Japanese UI and Kaillera support. An easy to use & cross-platform mupen64plus front-end written in C++ & Qt.").toHtmlEscaped());
    html.replace("{cmgk_version_label}", tr("CMG-K Version:"));
    html.replace("{rmgk_base_version_label}", tr("Based on RMG-K:"));
    html.replace("{rmgk_base_commit_label}", tr("RMG-K Base Commit:"));
    html.replace("{license_label}", tr("License:"));
    html.replace("{contributors_label}", tr("Contributors:"));
    html.replace("{special_thanks_label}", tr("Special thanks:"));
    html.replace("{custom_cheats_text}", tr("for providing custom cheats"));
    html.replace("{controller_image_text}", tr("for making the controller image for RMG-Input"));
    html.replace("{uses_projects_label}", tr("It uses the following third party projects:"));
    html.replace("{includes_projects_label}", tr("It includes the following third party projects:"));
    html.replace("{cmgk_version}", QString::fromStdString(CoreGetVersion()));
    html.replace("{rmgk_base_version}", QString::fromStdString(CoreGetUpstreamBaseVersion()));
    html.replace("{rmgk_base_commit}", QString::fromStdString(CoreGetUpstreamBaseCommit()));
    // show modified html
    this->textBrowser->setHtml(html);
}
