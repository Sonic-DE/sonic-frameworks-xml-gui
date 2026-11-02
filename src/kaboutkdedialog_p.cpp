/*
    This file is part of the KDE libraries
    SPDX-FileCopyrightText: 2007 Urs Wolfer <uwolfer at kde.org>

    Parts of this class have been take from the KAboutKDE class, which was
    SPDX-FileCopyrightText: 2000 Espen Sand <espen@kde.org>

    SPDX-License-Identifier: LGPL-2.0-only
*/

#include "kaboutkdedialog_p.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

#include <KLocalizedString>
#include <KTitleWidget>

#include <kxmlgui_version.h>

namespace KDEPrivate
{
KAboutKdeDialog::KAboutKdeDialog(QWidget *parent)
    : QDialog(parent)
    , d(nullptr)
{
    setWindowTitle(i18nc("@title:window", "About SonicDE"));

    KTitleWidget *titleWidget = new KTitleWidget(this);
    titleWidget->setText(i18n("<html><font size=\"5\">SonicDE</font><br />Be Free!</html>"));
    titleWidget->setIconSize(QSize(48, 48));
    // Bundled, so icon themes without a SonicDE icon don't show another logo.
    titleWidget->setIcon(QIcon(QStringLiteral(":/kxmlgui5/sonicde.svg")), KTitleWidget::ImageLeft);

    QLabel *about = new QLabel;
    about->setMargin(10);
    about->setAlignment(Qt::AlignTop);
    about->setWordWrap(true);
    about->setOpenExternalLinks(true);
    about->setTextInteractionFlags(Qt::TextBrowserInteraction);
    about->setText(
        i18n("<html>"
             "<b>SonicDE</b> is a world-wide community of software engineers, artists, writers, "
             "translators and creators who are committed to <a href=\"%1\">Free Software</a> "
             "development. SonicDE produces the Sonic Desktop Environment, its applications, "
             "and the many software libraries that support them.<br /><br />"
             "SonicDE is a cooperative enterprise: no single entity controls its direction or products. "
             "Instead, we work together to achieve the common goal of building the world's finest "
             "Free Software. Everyone is welcome to <a href=\"%2\">join and contribute</a> to SonicDE, including you.<br /><br />"
             "Visit <a href=\"%3\">%3</a> for "
             "more information about the SonicDE community and the software we produce.</html>",
            QStringLiteral("https://www.gnu.org/philosophy/free-sw.html"),
            QStringLiteral("https://github.com/Sonic-DE"),
            QStringLiteral("https://sonicde.org")));

    QLabel *report = new QLabel;
    report->setMargin(10);
    report->setAlignment(Qt::AlignTop);
    report->setWordWrap(true);
    report->setOpenExternalLinks(true);
    report->setTextInteractionFlags(Qt::TextBrowserInteraction);
    report->setText(
        i18n("<html>"
             "Software can always be improved, and the SonicDE team is ready to "
             "do so. However, you - the user - must tell us when "
             "something does not work as expected or could be done better.<br /><br />"
             "SonicDE tracks bugs in the issue tracker of each project. Visit "
             "<a href=\"%1\">%1</a> and open an issue on the project concerned "
             "to report bugs.<br /><br />"
             "If you have a suggestion for improvement then you are welcome to "
             "open an issue for your wish as well.</html>",
            QStringLiteral("https://github.com/Sonic-DE")));

    QLabel *join = new QLabel;
    join->setMargin(10);
    join->setAlignment(Qt::AlignTop);
    join->setWordWrap(true);
    join->setOpenExternalLinks(true);
    join->setTextInteractionFlags(Qt::TextBrowserInteraction);
    join->setText(
        i18n("<html>"
             "You do not have to be a software developer to be a member of the "
             "SonicDE team. You can help translate "
             "program interfaces. You can provide graphics, themes, sounds, and "
             "improved documentation. You decide!"
             "<br /><br />"
             "Visit "
             "<a href=\"%1\">%1</a> "
             "for the projects in which you can participate."
             "<br /><br />"
             "If you need more information or documentation, then a visit to "
             "<a href=\"%2\">%2</a> "
             "will provide you with what you need.</html>",
            QStringLiteral("https://github.com/Sonic-DE"),
            QStringLiteral("https://sonicde.org")));

    QLabel *support = new QLabel;
    support->setMargin(10);
    support->setAlignment(Qt::AlignTop);
    support->setWordWrap(true);
    support->setOpenExternalLinks(true);
    support->setTextInteractionFlags(Qt::TextBrowserInteraction);
    support->setText(
        i18n("<html>"
             "SonicDE software is and will always be available free of charge, however creating it is not free.<br /><br />"
             "No way to donate to SonicDE has been set up yet, but we will be creating one. "
             "Until then, you can support SonicDE by contributing; see "
             "<a href=\"%1\">%1</a>."
             "<br /><br />Thank you very much in advance for your support.</html>",
            QStringLiteral("https://sonicde.org")));
    support->setMinimumSize(support->sizeHint());

    QTabWidget *tabWidget = new QTabWidget;
    tabWidget->setUsesScrollButtons(false);
    tabWidget->addTab(about, i18nc("@title:tab About SonicDE", "&About"));
    tabWidget->addTab(report, i18nc("@title:tab", "&Report Bugs or Wishes"));
    tabWidget->addTab(join, i18nc("@title:tab", "&Join SonicDE"));
    tabWidget->addTab(support, i18nc("@title:tab", "&Support SonicDE"));

    QLabel *image = new QLabel;
    QIcon icon(QStringLiteral(":/kxmlgui5/aboutsonicde.svg"));
    image->setPixmap(icon.pixmap(150, 250));

    QVBoxLayout *sideLayout = new QVBoxLayout;
    sideLayout->addWidget(titleWidget);
    sideLayout->addWidget(image, 1000);

    QHBoxLayout *midLayout = new QHBoxLayout;
    midLayout->addLayout(sideLayout);
    midLayout->addWidget(tabWidget, 1000);

    QDialogButtonBox *buttonBox = new QDialogButtonBox;
    buttonBox->setStandardButtons(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addLayout(midLayout);
    mainLayout->addWidget(buttonBox);

    setLayout(mainLayout);
}

}

#include "moc_kaboutkdedialog_p.cpp"
