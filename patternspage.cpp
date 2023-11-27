//
// Created by esorochinskiy on 26.11.23.
//

// You may need to build the project (run Qt uic code generator) to get "ui_PatternsPage.h" resolved

#include <QDir>
#include <QStandardPaths>
#include <KConfig>
#include <KConfigGroup>
#include "patternspage.h"
#ifdef _DEBUG
#include <QDebug>
#include <QPushButton>
#include <KConfigDialog>

#endif


PatternsPage::PatternsPage(KConfigSkeleton *pSkeleton, KConfigDialog *cdialog, QWidget *parent) :
        QWidget(parent), Ui::PatternsPage() {
    mConfig = pSkeleton;
    setupUi(this);
    connect(patternsList, &QListWidget::currentRowChanged, this, &PatternsPage::patternChanged);
    connect(cdialog->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, &PatternsPage::restoreSettingsClicked);
    fillPatternList();
    setupData();

}


void PatternsPage::fillPatternList() {

    patternsList->clear();

    auto *firstItem = new QListWidgetItem();
    firstItem->setData(Qt::DisplayRole, i18n("<Not selected>"));
    firstItem->setData(Qt::UserRole, "");
    firstItem->setData(Qt::UserRole + 100, "");
    firstItem->setData(Qt::UserRole + 101, "");
    firstItem->setData(Qt::UserRole + 102, "");
    patternsList->addItem(firstItem);

    const QStringList &fnlist = QStandardPaths::locateAll(QStandardPaths::DataLocation, "templates",
                                                          QStandardPaths::LocateDirectory);
    for(const QString &fn : fnlist) {
        const QDir &fndir = QDir(fn);
        if (!fndir.exists() || fndir.isEmpty()) {
            return;
        }

        for (QFileInfo &finfo: fndir.entryInfoList(QDir::nameFiltersFromString("*.desktop"))) {
            const QString &fabsPath = finfo.absoluteFilePath();

            KConfig kdfile(fabsPath, KConfig::SimpleConfig);

            KConfigGroup group = kdfile.group(QStringLiteral("GOLColony"));
            const QString &kgolFilePath = finfo.absolutePath() +
                                          QDir::separator() + group.readEntry("FileName");
            const QString &displayTitle = group.readEntry("Name");

            auto *item = new QListWidgetItem();
            item->setData(Qt::DisplayRole, displayTitle);
            item->setData(Qt::UserRole, kgolFilePath);
            item->setData(Qt::UserRole + 100, group.readEntry("Description"));
            item->setData(Qt::UserRole + 101, group.readEntry("Author"));
            item->setData(Qt::UserRole + 102, group.readEntry("AuthorEmail"));
            item->setSizeHint(QSize(item->sizeHint().width(), 72));
            patternsList->addItem(item);

        }
    }
}

void PatternsPage::patternChanged(int cRow) {
    QListWidgetItem *item = patternsList->item(cRow);

    descData->setText(item->data(Qt::UserRole + 100).value<QString>());
    authorData->setText(item->data(Qt::UserRole + 101).value<QString>());
    emailData->setText(item->data(Qt::UserRole + 102).value<QString>());
    kcfg_templatefile->setText(item->data(Qt::UserRole).value<QString>());
}

void PatternsPage::setupData() {

    kcfg_templatefile->hide();
    if (patternsList->count() > 1) {

        KConfig * config = mConfig->config();
        KConfigGroup group = config->group(QStringLiteral("General"));
        QString initialGroup = group.readEntry("templatefile");

        for (int i = 0; i <  patternsList->count(); i++) {
            QListWidgetItem *item = patternsList->item(i);
            const QString &cPath = item->data(Qt::UserRole).value<QString>();
            if (cPath == initialGroup) {
                patternsList->setCurrentRow(i);
                return;
            }
        }

    }
    cleanup();
}

void PatternsPage::restoreSettingsClicked(bool) {
    cleanup();
}

void PatternsPage::cleanup() {
    patternsList->setCurrentRow(0);
}
