/*
    This file is part of the KDE libraries
    SPDX-FileCopyrightText: 2000 Kurt Granroth <granroth@kde.org>
    SPDX-FileCopyrightText: 2006 Hamish Rodda <rodda@kde.org>
    SPDX-FileCopyrightText: 2007 David Faure <faure@kde.org>

    SPDX-License-Identifier: LGPL-2.0-only
*/

#include "kedittoolbar.h"
#include "debug.h"
#include "kedittoolbar_p.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QShowEvent>
#include <QToolButton>

#include <KIconDialog>
#include <KListWidgetSearchLine>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSeparator>

#include "kactioncollection.h"
#include "ktoolbar.h"
#include "ktoolbareditor.h"
#include "kxmlguifactory.h"

#include <kxmlgui_version.h>

namespace KDEPrivate
{
class ToolBarItem : public QListWidgetItem
{
public:
    ToolBarItem(QListWidget *parent, const QString &tag = QString(), const QString &name = QString(), const QString &statusText = QString())
        : QListWidgetItem(parent)
        , m_internalTag(tag)
        , m_internalName(name)
        , m_statusText(statusText)
        , m_isSeparator(false)
        , m_isSpacer(false)
        , m_isTextAlongsideIconHidden(false)
    {
        // Drop between items, not onto items
        setFlags((flags() | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
    }

    void setInternalTag(const QString &tag)
    {
        m_internalTag = tag;
    }
    void setInternalName(const QString &name)
    {
        m_internalName = name;
    }
    void setStatusText(const QString &text)
    {
        m_statusText = text;
    }
    void setSeparator(bool sep)
    {
        m_isSeparator = sep;
    }
    void setSpacer(bool spacer)
    {
        m_isSpacer = spacer;
    }
    void setTextAlongsideIconHidden(bool hidden)
    {
        m_isTextAlongsideIconHidden = hidden;
    }
    QString internalTag() const
    {
        return m_internalTag;
    }
    QString internalName() const
    {
        return m_internalName;
    }
    QString statusText() const
    {
        return m_statusText;
    }
    bool isSeparator() const
    {
        return m_isSeparator;
    }
    bool isSpacer() const
    {
        return m_isSpacer;
    }
    bool isTextAlongsideIconHidden() const
    {
        return m_isTextAlongsideIconHidden;
    }

    int index() const
    {
        return listWidget()->row(const_cast<ToolBarItem *>(this));
    }

private:
    QString m_internalTag;
    QString m_internalName;
    QString m_statusText;
    bool m_isSeparator;
    bool m_isSpacer;
    bool m_isTextAlongsideIconHidden;
};

static QDataStream &operator<<(QDataStream &s, const ToolBarItem &item)
{
    s << item.internalTag();
    s << item.internalName();
    s << item.statusText();
    s << item.isSeparator();
    s << item.isSpacer();
    s << item.isTextAlongsideIconHidden();
    return s;
}
static QDataStream &operator>>(QDataStream &s, ToolBarItem &item)
{
    QString internalTag;
    s >> internalTag;
    item.setInternalTag(internalTag);
    QString internalName;
    s >> internalName;
    item.setInternalName(internalName);
    QString statusText;
    s >> statusText;
    item.setStatusText(statusText);
    bool sep;
    s >> sep;
    item.setSeparator(sep);
    bool spacer;
    s >> spacer;
    item.setSpacer(spacer);
    bool hidden;
    s >> hidden;
    item.setTextAlongsideIconHidden(hidden);
    return s;
}

////

ToolBarListWidget::ToolBarListWidget(QWidget *parent)
    : QListWidget(parent)
    , m_activeList(true)
{
    setDragDropMode(QAbstractItemView::DragDrop); // no internal moves
}
QMimeData *ToolBarListWidget::mimeData(const QList<QListWidgetItem *> &items) const
{
    if (items.isEmpty()) {
        return nullptr;
    }
    QMimeData *mimedata = new QMimeData();

    QByteArray data;
    {
        QDataStream stream(&data, QIODevice::WriteOnly);
        // we only support single selection
        ToolBarItem *item = static_cast<ToolBarItem *>(items.first());
        stream << *item;
    }

    mimedata->setData(QStringLiteral("application/x-kde-action-list"), data);
    mimedata->setData(QStringLiteral("application/x-kde-source-treewidget"), m_activeList ? "active" : "inactive");

    return mimedata;
}

bool ToolBarListWidget::dropMimeData(int index, const QMimeData *mimeData, Qt::DropAction action)
{
    Q_UNUSED(action)
    const QByteArray data = mimeData->data(QStringLiteral("application/x-kde-action-list"));
    if (data.isEmpty()) {
        return false;
    }
    QDataStream stream(data);
    const bool sourceIsActiveList = mimeData->data(QStringLiteral("application/x-kde-source-treewidget")) == "active";
    ToolBarItem *item = new ToolBarItem(this); // needs parent, use this temporarily
    stream >> *item;
    Q_EMIT dropped(this, index, item, sourceIsActiveList);
    return true;
}

ToolBarItem *ToolBarListWidget::currentItem() const
{
    return static_cast<ToolBarItem *>(QListWidget::currentItem());
}

void ToolBarListWidget::dragEnterEvent(QDragEnterEvent *event)
{
    // no internal drag in inactive list
    if (!m_activeList && (event->source() == this)) {
        event->ignore();
        return;
    }
    QListWidget::dragEnterEvent(event);
}
void ToolBarListWidget::dragMoveEvent(QDragMoveEvent *event)
{
    // no internal drag in inactive list
    if (!m_activeList && (event->source() == this)) {
        event->ignore();
        return;
    }
    QListWidget::dragMoveEvent(event);
}

IconTextEditDialog::IconTextEditDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Change Text"));
    setModal(true);

    QVBoxLayout *layout = new QVBoxLayout(this);

    QGridLayout *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);

    m_lineEdit = new QLineEdit(this);
    m_lineEdit->setClearButtonEnabled(true);
    QLabel *label = new QLabel(i18n("Icon te&xt:"), this);
    label->setBuddy(m_lineEdit);
    grid->addWidget(label, 0, 0);
    grid->addWidget(m_lineEdit, 0, 1);

    m_cbHidden = new QCheckBox(i18nc("@option:check", "&Hide text when toolbar shows text alongside icons"), this);
    grid->addWidget(m_cbHidden, 1, 1);

    layout->addLayout(grid);

    m_buttonBox = new QDialogButtonBox(this);
    m_buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttonBox);

    connect(m_lineEdit, &QLineEdit::textChanged, this, &IconTextEditDialog::slotTextChanged);

    m_lineEdit->setFocus();
    setFixedHeight(sizeHint().height());
}

void IconTextEditDialog::setIconText(const QString &text)
{
    m_lineEdit->setText(text);
}

QString IconTextEditDialog::iconText() const
{
    return m_lineEdit->text().trimmed();
}

void IconTextEditDialog::setTextAlongsideIconHidden(bool hidden)
{
    m_cbHidden->setChecked(hidden);
}

bool IconTextEditDialog::textAlongsideIconHidden() const
{
    return m_cbHidden->isChecked();
}

void IconTextEditDialog::slotTextChanged(const QString &text)
{
    // Do not allow empty icon text
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
}

// The widgets of the toolbar editor; KToolBarEditor holds the toolbars and changes them.
class KEditToolBarWidgetPrivate
{
public:
    /*
     *
     * @param collection In the old-style constructor, this is the collection passed
     * to the KEditToolBar constructor.
     * In the xmlguifactory-based constructor, we let KXMLGUIClient create a dummy one,
     * but it probably isn't used.
     */
    KEditToolBarWidgetPrivate(KEditToolBarWidget* widget, KActionCollection* collection)
        : m_widget(widget)
        , m_collection(collection)
        , m_editor(new KToolBarEditor(widget))
        , m_helpArea(nullptr)
    {
        // We want items with an icon to align with items without icon
        // So we use an empty QPixmap for that
        const int iconSize = widget->style()->pixelMetric(QStyle::PM_SmallIconSize);
        m_emptyIcon = QPixmap(iconSize, iconSize);
        m_emptyIcon.fill(Qt::transparent);
    }
    ~KEditToolBarWidgetPrivate()
    {
    }

    // private slots
    void slotToolBarSelected(int index);

    void slotInactiveSelectionChanged();
    void slotActiveSelectionChanged();

    void slotInsertButton();
    void slotRemoveButton();
    void slotUpButton();
    void slotDownButton();

    void slotChangeIcon();
    void slotChangeIconText();

    void slotDropped(ToolBarListWidget *list, int index, ToolBarItem *item, bool sourceIsActiveList);

    void setupLayout();
    void connectEditor();

    void initOldStyle(const QString &file, bool global, const QString &defaultToolbar);
    void initFromFactory(KXMLGUIFactory *factory, const QString &defaultToolbar);
    void loadToolBarCombo();
    void loadActions();
    void fillList(ToolBarListWidget* list, const QAbstractItemModel* model);
    void updateItem(ToolBarItem* item, const QModelIndex& index);

    /*
     * The row of the item with the given item's name and tag
     */
    static int rowOf(const ToolBarListWidget* list, const ToolBarItem* item)
    {
        for (int i = 0; i < list->count(); ++i) {
            const auto* other = static_cast<const ToolBarItem*>(list->item(i));
            if (other->internalName() == item->internalName() && other->internalTag() == item->internalTag()) {
                return i;
            }
        }
        return -1;
    }

public:
    KEditToolBarWidget *const m_widget;

    QComboBox *m_toolbarCombo;

    QToolButton *m_upAction;
    QToolButton *m_removeAction;
    QToolButton *m_insertAction;
    QToolButton *m_downAction;

    KActionCollection *m_collection;
    KToolBarEditor* const m_editor;

    QPixmap m_emptyIcon;

    ToolBarListWidget *m_inactiveList;
    ToolBarListWidget *m_activeList;
    KListWidgetSearchLine* m_inactiveListSearchLine;
    KListWidgetSearchLine* m_activeListSearchLine;

    QLabel *m_comboLabel;
    KSeparator *m_comboSeparator;
    QLabel *m_helpArea;
    QPushButton *m_changeIcon;
    QPushButton *m_changeIconText;
};

}

using namespace KDEPrivate;

class KEditToolBarPrivate
{
public:
    KEditToolBarPrivate(KEditToolBar *qq)
        : q(qq)
    {
    }

    void init();

    void slotButtonClicked(QAbstractButton *button);
    void acceptOK(bool);
    void enableApply(bool);
    void okClicked();
    void applyClicked();
    void defaultClicked();

public:
    KEditToolBar *const q;

    bool m_accept = false;
    // Save parameters for recreating widget after resetting toolbar
    bool m_global = false;
    KActionCollection *m_collection = nullptr;
    QString m_file;
    QString m_defaultToolBar;
    KXMLGUIFactory *m_factory = nullptr;
    KEditToolBarWidget *m_widget = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
};

Q_GLOBAL_STATIC(QString, s_defaultToolBarName)

KEditToolBar::KEditToolBar(KActionCollection *collection, QWidget *parent)
    : QDialog(parent)
    , d(new KEditToolBarPrivate(this))
{
    d->m_widget = new KEditToolBarWidget(collection, this);
    d->init();
    d->m_collection = collection;
}

KEditToolBar::KEditToolBar(KXMLGUIFactory *factory, QWidget *parent)
    : QDialog(parent)
    , d(new KEditToolBarPrivate(this))
{
    d->m_widget = new KEditToolBarWidget(this);
    d->init();
    d->m_factory = factory;
}

void KEditToolBarPrivate::init()
{
    m_accept = false;
    m_factory = nullptr;

    q->setDefaultToolBar(QString());

    q->setWindowTitle(i18nc("@title:window", "Configure Toolbars"));
    q->setModal(false);

    m_layout = new QVBoxLayout(q);
    m_layout->addWidget(m_widget);

    m_buttonBox = new QDialogButtonBox(q);
    m_buttonBox->setStandardButtons(QDialogButtonBox::RestoreDefaults | QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    KGuiItem::assign(m_buttonBox->button(QDialogButtonBox::Ok), KStandardGuiItem::ok());
    KGuiItem::assign(m_buttonBox->button(QDialogButtonBox::Apply), KStandardGuiItem::apply());
    KGuiItem::assign(m_buttonBox->button(QDialogButtonBox::Cancel), KStandardGuiItem::cancel());
    KGuiItem::assign(m_buttonBox->button(QDialogButtonBox::RestoreDefaults), KStandardGuiItem::defaults());
    q->connect(m_buttonBox, &QDialogButtonBox::clicked, q, [this](QAbstractButton *button) {
        slotButtonClicked(button);
    });
    QObject::connect(m_buttonBox, &QDialogButtonBox::rejected, q, &QDialog::reject);
    m_layout->addWidget(m_buttonBox);

    q->connect(m_widget, &KEditToolBarWidget::enableOk, q, [this](bool state) {
        acceptOK(state);
        enableApply(state);
    });
    enableApply(false);

    q->setMinimumSize(q->sizeHint());
}

void KEditToolBar::setResourceFile(const QString &file, bool global)
{
    d->m_file = file;
    d->m_global = global;
    d->m_widget->load(d->m_file, d->m_global, d->m_defaultToolBar);
}

KEditToolBar::~KEditToolBar()
{
    s_defaultToolBarName()->clear();
}

void KEditToolBar::setDefaultToolBar(const QString &toolBarName)
{
    if (toolBarName.isEmpty()) {
        d->m_defaultToolBar = *s_defaultToolBarName();
    } else {
        d->m_defaultToolBar = toolBarName;
    }
}

void KEditToolBarPrivate::acceptOK(bool b)
{
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(b);
    m_accept = b;
}

void KEditToolBarPrivate::enableApply(bool b)
{
    m_buttonBox->button(QDialogButtonBox::Apply)->setEnabled(b);
}

void KEditToolBarPrivate::defaultClicked()
{
    if (KMessageBox::warningContinueCancel(
            q,
            i18n("Do you really want to reset all toolbars of this application to their default? The changes will be applied immediately."),
            i18n("Reset Toolbars"),
            KGuiItem(i18n("Reset")))
        != KMessageBox::Continue) {
        return;
    }

    m_accept = false;
    m_widget->restoreDefaults();
    enableApply(false);

    Q_EMIT q->newToolBarConfig();
}

void KEditToolBarPrivate::slotButtonClicked(QAbstractButton *button)
{
    QDialogButtonBox::StandardButton type = m_buttonBox->standardButton(button);

    switch (type) {
    case QDialogButtonBox::Ok:
        okClicked();
        break;
    case QDialogButtonBox::Apply:
        applyClicked();
        break;
    case QDialogButtonBox::RestoreDefaults:
        defaultClicked();
        break;
    default:
        break;
    }
}

void KEditToolBarPrivate::okClicked()
{
    if (!m_accept) {
        q->reject();
        return;
    }

    // Do not rebuild GUI and emit the "newToolBarConfig" signal again here if the "Apply"
    // button was already pressed and no further changes were made.
    if (m_buttonBox->button(QDialogButtonBox::Apply)->isEnabled()) {
        m_widget->save();
        Q_EMIT q->newToolBarConfig();
    }
    q->accept();
}

void KEditToolBarPrivate::applyClicked()
{
    (void)m_widget->save();
    enableApply(false);
    Q_EMIT q->newToolBarConfig();
}

void KEditToolBar::setGlobalDefaultToolBar(const QString &toolBarName)
{
    *s_defaultToolBarName() = toolBarName;
}

KEditToolBarWidget::KEditToolBarWidget(KActionCollection* collection, QWidget* parent)
    : QWidget(parent)
    , d(new KEditToolBarWidgetPrivate(this, collection))
{
    d->setupLayout();
    d->connectEditor();
}

KEditToolBarWidget::KEditToolBarWidget(QWidget* parent)
    : QWidget(parent)
    , d(new KEditToolBarWidgetPrivate(this, KXMLGUIClient::actionCollection() /*create new one*/))
{
    d->setupLayout();
    d->connectEditor();
}

KEditToolBarWidget::~KEditToolBarWidget()
{
    delete d;
}

void KEditToolBarWidget::load(const QString &file, bool global, const QString &defaultToolBar)
{
    d->initOldStyle(file, global, defaultToolBar);
}

void KEditToolBarWidget::load(KXMLGUIFactory *factory, const QString &defaultToolBar)
{
    d->initFromFactory(factory, defaultToolBar);
}

void KEditToolBarWidgetPrivate::initOldStyle(const QString &resourceFile, bool global, const QString &defaultToolBar)
{
    // TODO: make sure we can call this multiple times?
    if (!m_editor->load(m_collection, resourceFile, global, defaultToolBar)) {
        return;
    }

    m_widget->adjustSize();
    m_widget->setMinimumSize(m_widget->sizeHint());
}

void KEditToolBarWidgetPrivate::initFromFactory(KXMLGUIFactory *factory, const QString &defaultToolBar)
{
    // TODO: make sure we can call this multiple times?
    if (!m_editor->load(factory, defaultToolBar)) {
        return;
    }

    m_widget->adjustSize();
    m_widget->setMinimumSize(m_widget->sizeHint());

    m_widget->actionCollection()->addAssociatedWidget(m_widget);
    const auto widgetActions = m_widget->actionCollection()->actions();
    for (QAction *action : widgetActions) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }
}

void KEditToolBarWidget::save()
{
    d->m_editor->save();
}

void KEditToolBarWidget::rebuildKXMLGUIClients()
{
    d->m_editor->rebuildClients();
}

void KEditToolBarWidget::restoreDefaults()
{
    // as a new widget would show them
    d->m_inactiveListSearchLine->clear();
    d->m_activeListSearchLine->clear();
    d->m_editor->restoreDefaults();
}

void KEditToolBarWidgetPrivate::setupLayout()
{
    // the toolbar name combo
    m_comboLabel = new QLabel(i18n("&Toolbar:"), m_widget);
    m_toolbarCombo = new QComboBox(m_widget);
    m_comboLabel->setBuddy(m_toolbarCombo);
    m_comboSeparator = new KSeparator(m_widget);
    QObject::connect(m_toolbarCombo, qOverload<int>(&QComboBox::activated), m_widget, [this](int index) {
        slotToolBarSelected(index);
    });

    //  QPushButton *new_toolbar = new QPushButton(i18n("&New"), this);
    //  new_toolbar->setPixmap(BarIcon("document-new", KIconLoader::SizeSmall));
    //  new_toolbar->setEnabled(false); // disabled until implemented
    //  QPushButton *del_toolbar = new QPushButton(i18n("&Delete"), this);
    //  del_toolbar->setPixmap(BarIcon("edit-delete", KIconLoader::SizeSmall));
    //  del_toolbar->setEnabled(false); // disabled until implemented

    // our list of inactive actions
    QLabel *inactive_label = new QLabel(i18n("A&vailable actions:"), m_widget);
    m_inactiveList = new ToolBarListWidget(m_widget);
    m_inactiveList->setDragEnabled(true);
    m_inactiveList->setActiveList(false);
    m_inactiveList->setMinimumSize(180, 200);
    m_inactiveList->setDropIndicatorShown(false); // #165663
    inactive_label->setBuddy(m_inactiveList);
    QObject::connect(m_inactiveList, &QListWidget::itemSelectionChanged, m_widget, [this]() {
        slotInactiveSelectionChanged();
    });
    QObject::connect(m_inactiveList, &QListWidget::itemDoubleClicked, m_widget, [this]() {
        slotInsertButton();
    });
    QObject::connect(m_inactiveList,
                     &ToolBarListWidget::dropped,
                     m_widget,
                     [this](ToolBarListWidget *list, int index, ToolBarItem *item, bool sourceIsActiveList) {
                         slotDropped(list, index, item, sourceIsActiveList);
                     });

    m_inactiveListSearchLine = new KListWidgetSearchLine(m_widget, m_inactiveList);
    m_inactiveListSearchLine->setPlaceholderText(i18n("Filter"));

    // our list of active actions
    QLabel *active_label = new QLabel(i18n("Curr&ent actions:"), m_widget);
    m_activeList = new ToolBarListWidget(m_widget);
    m_activeList->setDragEnabled(true);
    m_activeList->setActiveList(true);
    // With Qt-4.1 only setting MiniumWidth results in a 0-width icon column ...
    m_activeList->setMinimumSize(m_inactiveList->minimumWidth(), 100);
    active_label->setBuddy(m_activeList);

    QObject::connect(m_activeList, &QListWidget::itemSelectionChanged, m_widget, [this]() {
        slotActiveSelectionChanged();
    });
    QObject::connect(m_activeList, &QListWidget::itemDoubleClicked, m_widget, [this]() {
        slotRemoveButton();
    });
    QObject::connect(m_activeList,
                     &ToolBarListWidget::dropped,
                     m_widget,
                     [this](ToolBarListWidget *list, int index, ToolBarItem *item, bool sourceIsActiveList) {
                         slotDropped(list, index, item, sourceIsActiveList);
                     });

    m_activeListSearchLine = new KListWidgetSearchLine(m_widget, m_activeList);
    m_activeListSearchLine->setPlaceholderText(i18n("Filter"));

    // "change icon" button
    m_changeIcon = new QPushButton(i18nc("@action:button", "Change &Icon…"), m_widget);
    m_changeIcon->setIcon(QIcon::fromTheme(QStringLiteral("preferences-desktop-icons")));
    m_changeIcon->setEnabled(m_activeList->currentItem());

    QObject::connect(m_changeIcon, &QPushButton::clicked, m_widget, [this]() {
        slotChangeIcon();
    });

    // "change icon text" button
    m_changeIconText = new QPushButton(i18nc("@action:button", "Change Te&xt…"), m_widget);
    m_changeIconText->setIcon(QIcon::fromTheme(QStringLiteral("edit-rename")));
    m_changeIconText->setEnabled(m_activeList->currentItem() != nullptr);

    QObject::connect(m_changeIconText, &QPushButton::clicked, m_widget, [this]() {
        slotChangeIconText();
    });

    // The buttons in the middle

    m_upAction = new QToolButton(m_widget);
    m_upAction->setIcon(QIcon::fromTheme(QStringLiteral("go-up")));
    m_upAction->setEnabled(false);
    m_upAction->setAutoRepeat(true);
    QObject::connect(m_upAction, &QToolButton::clicked, m_widget, [this]() {
        slotUpButton();
    });

    m_insertAction = new QToolButton(m_widget);
    m_insertAction->setIcon(QIcon::fromTheme(QApplication::isRightToLeft() ? QStringLiteral("go-previous") : QStringLiteral("go-next")));
    m_insertAction->setEnabled(false);
    QObject::connect(m_insertAction, &QToolButton::clicked, m_widget, [this]() {
        slotInsertButton();
    });

    m_removeAction = new QToolButton(m_widget);
    m_removeAction->setIcon(QIcon::fromTheme(QApplication::isRightToLeft() ? QStringLiteral("go-next") : QStringLiteral("go-previous")));
    m_removeAction->setEnabled(false);
    QObject::connect(m_removeAction, &QToolButton::clicked, m_widget, [this]() {
        slotRemoveButton();
    });

    m_downAction = new QToolButton(m_widget);
    m_downAction->setIcon(QIcon::fromTheme(QStringLiteral("go-down")));
    m_downAction->setEnabled(false);
    m_downAction->setAutoRepeat(true);
    QObject::connect(m_downAction, &QToolButton::clicked, m_widget, [this]() {
        slotDownButton();
    });

    m_helpArea = new QLabel(m_widget);
    m_helpArea->setWordWrap(true);

    // now start with our layouts
    QVBoxLayout *top_layout = new QVBoxLayout(m_widget);
    top_layout->setContentsMargins(0, 0, 0, 0);

    QVBoxLayout *name_layout = new QVBoxLayout();
    QHBoxLayout *list_layout = new QHBoxLayout();

    QVBoxLayout *inactive_layout = new QVBoxLayout();
    QVBoxLayout *active_layout = new QVBoxLayout();
    QHBoxLayout *changeIcon_layout = new QHBoxLayout();

    QGridLayout *button_layout = new QGridLayout();

    name_layout->addWidget(m_comboLabel);
    name_layout->addWidget(m_toolbarCombo);
    //  name_layout->addWidget(new_toolbar);
    //  name_layout->addWidget(del_toolbar);

    button_layout->setSpacing(0);
    button_layout->setRowStretch(0, 10);
    button_layout->addWidget(m_upAction, 1, 1);
    button_layout->addWidget(m_removeAction, 2, 0);
    button_layout->addWidget(m_insertAction, 2, 2);
    button_layout->addWidget(m_downAction, 3, 1);
    button_layout->setRowStretch(4, 10);

    inactive_layout->addWidget(inactive_label);
    inactive_layout->addWidget(m_inactiveListSearchLine);
    inactive_layout->addWidget(m_inactiveList, 1);

    active_layout->addWidget(active_label);
    active_layout->addWidget(m_activeListSearchLine);
    active_layout->addWidget(m_activeList, 1);
    active_layout->addLayout(changeIcon_layout);

    changeIcon_layout->addWidget(m_changeIcon);
    changeIcon_layout->addStretch(1);
    changeIcon_layout->addWidget(m_changeIconText);

    list_layout->addLayout(inactive_layout);
    list_layout->addLayout(button_layout);
    list_layout->addLayout(active_layout);

    top_layout->addLayout(name_layout);
    top_layout->addWidget(m_comboSeparator);
    top_layout->addLayout(list_layout, 10);
    top_layout->addWidget(m_helpArea);
    top_layout->addWidget(new KSeparator(m_widget));
}

void KEditToolBarWidgetPrivate::connectEditor()
{
    // The editor resets both lists together, the current actions last.
    QObject::connect(m_editor, &KToolBarEditor::toolBarsChanged, m_widget, [this]() {
        loadToolBarCombo();
    });
    QObject::connect(m_editor, &KToolBarEditor::currentToolBarChanged, m_widget, [this]() {
        m_toolbarCombo->setCurrentIndex(m_editor->currentToolBar());
    });
    QObject::connect(m_editor->currentActions(), &QAbstractItemModel::modelReset, m_widget, [this]() {
        loadActions();
    });
    QObject::connect(m_editor->currentActions(),
        &QAbstractItemModel::rowsMoved,
        m_widget,
        [this](const QModelIndex&, int start, int, const QModelIndex&, int row) {
            // make it selected again
            QListWidgetItem* item = m_activeList->takeItem(start);
            m_activeList->insertItem(row > start ? row - 1 : row, item);
            m_activeList->setCurrentItem(item);
        });
    QObject::connect(m_editor->currentActions(), &QAbstractItemModel::dataChanged, m_widget, [this](const QModelIndex& topLeft) {
        updateItem(static_cast<ToolBarItem*>(m_activeList->item(topLeft.row())), topLeft);
    });
    QObject::connect(m_editor, &KToolBarEditor::changed, m_widget, [this]() {
        // we're modified, so let this change
        Q_EMIT m_widget->enableOk(true);
    });
}

void KEditToolBarWidgetPrivate::loadToolBarCombo()
{
    // just in case, we clear our combo
    m_toolbarCombo->clear();
    m_toolbarCombo->addItems(m_editor->toolBars());

    const bool showCombo = (m_toolbarCombo->count() > 1);
    m_comboLabel->setVisible(showCombo);
    m_comboSeparator->setVisible(showCombo);
    m_toolbarCombo->setVisible(showCombo);
}

void KEditToolBarWidgetPrivate::loadActions()
{
    // clear our lists
    m_inactiveList->clear();
    m_activeList->clear();
    m_insertAction->setEnabled(false);
    m_removeAction->setEnabled(false);
    m_upAction->setEnabled(false);
    m_downAction->setEnabled(false);

    fillList(m_activeList, m_editor->currentActions());
    fillList(m_inactiveList, m_editor->availableActions());
}

void KEditToolBarWidgetPrivate::fillList(ToolBarListWidget* list, const QAbstractItemModel* model)
{
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        const QString tag = index.data(KToolBarEditor::TagRole).toString();
        ToolBarItem* item = new ToolBarItem(list, tag, index.data(KToolBarEditor::NameRole).toString(), index.data(KToolBarEditor::StatusTextRole).toString());
        item->setSeparator(tag == QLatin1String("Separator"));
        item->setSpacer(tag == QLatin1String("Spacer"));
        updateItem(item, index);
    }
}

void KEditToolBarWidgetPrivate::updateItem(ToolBarItem* item, const QModelIndex& index)
{
    if (!item) {
        return;
    }
    item->setText(index.data(Qt::DisplayRole).toString());
    item->setTextAlongsideIconHidden(index.data(KToolBarEditor::TextAlongsideIconHiddenRole).toBool());
    if (item->internalTag() == QLatin1String("Action")) {
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        item->setIcon(!icon.isNull() ? icon : m_emptyIcon);
    }
}

KActionCollection *KEditToolBarWidget::actionCollection() const
{
    return d->m_collection;
}

void KEditToolBarWidgetPrivate::slotToolBarSelected(int index)
{
    m_editor->setCurrentToolBar(index);
}

void KEditToolBarWidgetPrivate::slotInactiveSelectionChanged()
{
    if (!m_inactiveList->selectedItems().isEmpty()) {
        m_insertAction->setEnabled(true);
        m_helpArea->setText(static_cast<ToolBarItem*>(m_inactiveList->selectedItems().first())->statusText());
    } else {
        m_insertAction->setEnabled(false);
        m_helpArea->setText(QString());
    }
}

void KEditToolBarWidgetPrivate::slotActiveSelectionChanged()
{
    ToolBarItem *toolitem = nullptr;
    if (!m_activeList->selectedItems().isEmpty()) {
        toolitem = static_cast<ToolBarItem *>(m_activeList->selectedItems().first());
    }

    m_removeAction->setEnabled(toolitem);

    m_changeIcon->setEnabled(toolitem && toolitem->internalTag() == QLatin1String("Action"));

    m_changeIconText->setEnabled(toolitem && toolitem->internalTag() == QLatin1String("Action"));

    if (toolitem) {
        m_upAction->setEnabled(toolitem->index() != 0);
        m_downAction->setEnabled(toolitem->index() != toolitem->listWidget()->count() - 1);

        m_helpArea->setText(toolitem->statusText());
    } else {
        m_upAction->setEnabled(false);
        m_downAction->setEnabled(false);
        m_helpArea->setText(QString());
    }
}

void KEditToolBarWidgetPrivate::slotInsertButton()
{
    if (!m_inactiveList->currentItem()) {
        return;
    }
    const int row = m_editor->insertAction(m_inactiveList->currentRow(), m_activeList->currentRow(), false);
    if (row >= 0) {
        m_activeList->setCurrentRow(row);
    }
}

void KEditToolBarWidgetPrivate::slotRemoveButton()
{
    m_editor->removeAction(m_activeList->currentRow());
}

void KEditToolBarWidgetPrivate::slotUpButton()
{
    m_editor->moveUp(m_activeList->currentRow());
}

void KEditToolBarWidgetPrivate::slotDownButton()
{
    m_editor->moveDown(m_activeList->currentRow());
}

void KEditToolBarWidgetPrivate::slotChangeIcon()
{
    QString icon = KIconDialog::getIcon(KIconLoader::Toolbar,
                                        KIconLoader::Action,
                                        false,
                                        0,
                                        false, // all defaults
                                        m_widget,
                                        i18n("Change Icon"));

    m_editor->setActionIcon(m_activeList->currentRow(), icon);
}

void KEditToolBarWidgetPrivate::slotChangeIconText()
{
    ToolBarItem *item = m_activeList->currentItem();

    if (item) {
        IconTextEditDialog dialog(m_widget);
        dialog.setIconText(item->text());
        dialog.setTextAlongsideIconHidden(item->isTextAlongsideIconHidden());

        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        m_editor->setActionIconText(m_activeList->row(item), dialog.iconText(), dialog.textAlongsideIconHidden());
    }
}

void KEditToolBarWidgetPrivate::slotDropped(ToolBarListWidget *list, int index, ToolBarItem *item, bool sourceIsActiveList)
{
    // The dropped copy was put in the list; the editor reloads the lists as it changes.
    list->takeItem(list->row(item));
    if (list == m_activeList) {
        const int after = index > 0 ? index - 1 : -1;
        if (sourceIsActiveList) {
            // has been dragged within the active list (moved).
            m_editor->moveAction(rowOf(m_activeList, item), after);
        } else {
            // dragged from the inactive list to the active list
            m_editor->insertAction(rowOf(m_inactiveList, item), after, true);
        }
    } else if (list == m_inactiveList) {
        // has been dragged to the inactive list -> remove from the active list.
        m_editor->removeAction(rowOf(m_activeList, item));
    }

    delete item; // not needed anymore. must be deleted before slotToolBarSelected clears the lists

    // we're modified, so let this change
    Q_EMIT m_widget->enableOk(true);

    slotToolBarSelected(m_toolbarCombo->currentIndex());
}

void KEditToolBar::showEvent(QShowEvent *event)
{
    if (!event->spontaneous()) {
        // The dialog has been shown, enable toolbar editing
        if (d->m_factory) {
            // call the xmlgui-factory version
            d->m_widget->load(d->m_factory, d->m_defaultToolBar);
        } else {
            // call the action collection version
            d->m_widget->load(d->m_file, d->m_global, d->m_defaultToolBar);
        }

        KToolBar::setToolBarsEditable(true);
    }
    QDialog::showEvent(event);
}

void KEditToolBar::hideEvent(QHideEvent *event)
{
    // The dialog has been hidden, disable toolbar editing
    KToolBar::setToolBarsEditable(false);

    QDialog::hideEvent(event);
}

#include "moc_kedittoolbar.cpp"
#include "moc_kedittoolbar_p.cpp"
