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
    html.replace("{cmgk_version}", QString::fromStdString(CoreGetVersion()));
    html.replace("{rmgk_base_version}", QString::fromStdString(CoreGetUpstreamBaseVersion()));
    html.replace("{rmgk_base_commit}", QString::fromStdString(CoreGetUpstreamBaseCommit()));
    // show modified html
    this->textBrowser->setHtml(html);
}
