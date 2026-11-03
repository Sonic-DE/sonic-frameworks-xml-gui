/*
    This file is part of the KDE libraries
    SPDX-FileCopyrightText: 2000 Kurt Granroth <granroth@kde.org>
    SPDX-FileCopyrightText: 2006 Hamish Rodda <rodda@kde.org>
    SPDX-FileCopyrightText: 2007 David Faure <faure@kde.org>
    SPDX-FileCopyrightText: 2026 Joseph Crowell <joseph.w.crowell@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ktoolbareditor.h"
#include "debug.h"

#include <QAbstractListModel>
#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QIcon>
#include <QSet>
#include <QStandardPaths>

#include <KLocalizedString>

#include "kactioncollection.h"
#include "ktoolbarhelper_p.h"
#include "kxmlguiclient.h"
#include "kxmlguifactory.h"

#include <algorithm>

// Named: KToolBarEditorPrivate, which has external linkage, holds these.
namespace KToolBarEditorData {
typedef QList<QDomElement> ToolBarList;

/*
 * Return a list of toolbar elements given a toplevel element
 */
static ToolBarList findToolBars(const QDomElement& start)
{
    ToolBarList list;

    for (QDomElement elem = start; !elem.isNull(); elem = elem.nextSiblingElement()) {
        if (elem.tagName() == QLatin1String("ToolBar")) {
            if (elem.attribute(QStringLiteral("noEdit")) != QLatin1String("true")) {
                list.append(elem);
            }
        } else {
            if (elem.tagName() != QLatin1String("MenuBar")) { // there are no toolbars inside the menubar :)
                list += findToolBars(elem.firstChildElement()); // recursive
            }
        }
    }

    return list;
}

class XmlData {
public:
    enum XmlType {
        Shell = 0,
        Part,
        Local,
        Merged
    };

    explicit XmlData(XmlType xmlType, const QString& xmlFile, KActionCollection* collection)
        : m_xmlFile(xmlFile)
        , m_type(xmlType)
        , m_actionCollection(collection)
    {
    }
    QString xmlFile() const
    {
        return m_xmlFile;
    }
    XmlType type() const
    {
        return m_type;
    }
    KActionCollection* actionCollection() const
    {
        return m_actionCollection;
    }
    void setDomDocument(const QDomDocument& domDoc)
    {
        m_document = domDoc.cloneNode().toDocument();
        m_barList = findToolBars(m_document.documentElement());
    }
    // Return reference, for e.g. actionPropertiesElement() to modify the document
    QDomDocument& domDocument()
    {
        return m_document;
    }
    const QDomDocument& domDocument() const
    {
        return m_document;
    }

    /*
     * Return the text (user-visible name) of a given toolbar
     */
    QString toolBarText(const QDomElement& it) const
    {
        QString name = KToolbarHelper::i18nToolBarName(it);

        // the name of the toolbar might depend on whether or not
        // it is in kparts
        if ((m_type == XmlData::Shell) || (m_type == XmlData::Part)) {
            QString doc_name(m_document.documentElement().attribute(QStringLiteral("name")));
            name += QLatin1String(" <") + doc_name + QLatin1Char('>');
        }
        return name;
    }

    bool m_isModified = false;
    const ToolBarList& barList() const
    {
        return m_barList;
    }

private:
    ToolBarList m_barList;
    QString m_xmlFile;
    QDomDocument m_document;
    XmlType m_type;
    KActionCollection* m_actionCollection;
};

// One element of a list: an action, separator, spacer, merge point or action list.
struct Entry {
    Entry(const QString& tag, const QString& name, const QString& statusText, const QString& text = QString())
        : tag(tag)
        , name(name)
        , statusText(statusText)
        , text(text)
    {
    }

    QString tag;
    QString name;
    QString statusText;
    QString text;
    QIcon icon;
    QString iconName;
    bool textAlongsideIconHidden = false;
};

class EntryList : public QAbstractListModel {
public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(entries.size());
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
            return QVariant();
        }
        const Entry& entry = entries.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            return entry.text;
        case Qt::DecorationRole:
            return entry.icon;
        case KToolBarEditor::NameRole:
            return entry.name;
        case KToolBarEditor::TagRole:
            return entry.tag;
        case KToolBarEditor::StatusTextRole:
            return i18nc("@label Action tooltip in toolbar editor, below the action list", "%1", entry.statusText);
        case KToolBarEditor::IconNameRole:
            return entry.iconName;
        case KToolBarEditor::TextAlongsideIconHiddenRole:
            return entry.textAlongsideIconHidden;
        }
        return QVariant();
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {Qt::DisplayRole, "text"},
            {Qt::DecorationRole, "icon"},
            {KToolBarEditor::NameRole, "name"},
            {KToolBarEditor::TagRole, "tag"},
            {KToolBarEditor::StatusTextRole, "statusText"},
            {KToolBarEditor::IconNameRole, "iconName"},
            {KToolBarEditor::TextAlongsideIconHiddenRole, "textAlongsideIconHidden"},
        };
    }

    bool isValidRow(int row) const
    {
        return row >= 0 && row < entries.size();
    }

    void reset(QList<Entry>&& list)
    {
        beginResetModel();
        entries = std::move(list);
        endResetModel();
    }

    void move(int from, int to)
    {
        if (from == to) {
            return;
        }
        beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
        entries.move(from, to);
        endMoveRows();
    }

    void changedAt(int row)
    {
        Q_EMIT dataChanged(index(row), index(row));
    }

    QList<Entry> entries;
};

// loadStandardsXmlFile() and setXML() are protected.
class EditorClient : public KXMLGUIClient {
public:
    using KXMLGUIClient::loadStandardsXmlFile;
    using KXMLGUIClient::setXML;
};
}

using namespace KToolBarEditorData;

class KToolBarEditorPrivate {
public:
    explicit KToolBarEditorPrivate(KToolBarEditor* editor)
        : q(editor)
        , available(new EntryList(editor))
        , current(new EntryList(editor))
    {
    }

    QString componentName() const
    {
        return client->componentName();
    }

    QString xmlFile(const QString& xml_file) const
    {
        return xml_file.isEmpty() ? componentName() + QLatin1String("ui.rc") : xml_file;
    }

    /*
     * Load in the specified XML file and dump the raw xml
     */
    QString loadXMLFile(const QString& _xml_file) const
    {
        QString xml_file = xmlFile(_xml_file);
        if (!QDir::isRelativePath(xml_file)) {
            return KXMLGUIFactory::readConfigFile(xml_file);
        }
        return KXMLGUIFactory::readConfigFile(xml_file, componentName());
    }

    /*
     * Look for a given item in the current toolbar
     */
    QDomElement findElement(const Entry& item) const
    {
        for (QDomNode n = currentToolBarElem.firstChild(); !n.isNull(); n = n.nextSibling()) {
            QDomElement elem = n.toElement();
            if ((elem.attribute(QStringLiteral("name")) == item.name) && (elem.tagName() == item.tag)) {
                return elem;
            }
        }
        return QDomElement();
    }

    void loadToolBars(const QString& defaultToolBar);
    bool selectToolBar(int index);
    void loadActions(const QDomElement& elem);
    void insertActive(const Entry& item, const Entry* before, bool prepend);
    bool removeActive(const Entry& item);
    void updateLocal(QDomElement& elem);
    void unload();

    KToolBarEditor* const q;
    EntryList* const available;
    EntryList* const current;

    std::unique_ptr<EditorClient> client = std::make_unique<EditorClient>();
    KXMLGUIFactory* factory = nullptr;
    KActionCollection* collection = nullptr;
    QString resourceFile;
    bool global = true;
    QString defaultToolBar;

    QList<XmlData> xmlFiles;
    XmlData* currentXmlData = nullptr;
    QDomElement currentToolBarElem;
    // The document new elements are created in: the current client's, or the merged one.
    QDomDocument elementDocument;
    QStringList toolBarNames;
    int currentToolBar = -1;
    bool loadedOnce = false;
};

void KToolBarEditorPrivate::loadToolBars(const QString& defaultToolBar)
{
    const QLatin1String attrName("name");
    toolBarNames.clear();

    int defaultToolBarId = -1;
    // load in all of the toolbar names
    for (const auto& xmlFile : std::as_const(xmlFiles)) {
        // skip the merged one in favor of the local one,
        // so that we can change icons
        // This also makes the app-defined named for "mainToolBar" appear rather than the ui_standards-defined name.
        if (xmlFile.type() == XmlData::Merged) {
            continue;
        }

        // each xml file may have any number of toolbars
        for (const auto& bar : xmlFile.barList()) {
            if (defaultToolBarId == -1 && bar.attribute(attrName) == defaultToolBar) {
                defaultToolBarId = toolBarNames.size();
            }
            toolBarNames.append(xmlFile.toolBarText(bar));
        }
    }
    Q_EMIT q->toolBarsChanged();

    // we want the specified item selected and its actions loaded
    currentToolBar = toolBarNames.isEmpty() ? -1 : std::max(defaultToolBarId, 0);
    if (!selectToolBar(currentToolBar)) {
        available->reset({});
        current->reset({});
    }
    Q_EMIT q->currentToolBarChanged();
}

bool KToolBarEditorPrivate::selectToolBar(int index)
{
    // We need to find the XmlData and toolbar element for this index
    // To do that, we do the same iteration as the one which filled in the names.
    int toolbarNumber = 0;
    for (auto& xmlFile : xmlFiles) {
        // skip the merged one in favor of the local one,
        // so that we can change icons
        if (xmlFile.type() == XmlData::Merged) {
            continue;
        }

        // each xml file may have any number of toolbars
        for (const auto& bar : xmlFile.barList()) {
            // is this our toolbar?
            if (toolbarNumber == index) {
                // save our current settings
                currentXmlData = &xmlFile;
                currentToolBarElem = bar;

                // load in our values
                loadActions(currentToolBarElem);

                if (xmlFile.type() == XmlData::Part || xmlFile.type() == XmlData::Shell) {
                    elementDocument = xmlFile.domDocument();
                }
                return true;
            }
            ++toolbarNumber;
        }
    }
    return false;
}

void KToolBarEditorPrivate::loadActions(const QDomElement& elem)
{
    const QLatin1String tagSeparator("Separator");
    const QLatin1String tagSpacer("Spacer");
    const QLatin1String tagMerge("Merge");
    const QLatin1String tagActionList("ActionList");
    const QLatin1String tagAction("Action");
    const QLatin1String attrName("name");

    const QString separatorstring = i18n("--- separator ---");
    const QString spacerstring = i18n("--- expanding spacer ---");

    int sep_num = 0;
    QString sep_name(QStringLiteral("separator_%1"));
    int spacer_num = 0;
    QString spacer_name(QStringLiteral("spacer_%1"));

    QList<Entry> inactiveList;
    QList<Entry> activeList;

    // We'll use this action collection
    KActionCollection* actionCollection = currentXmlData->actionCollection();

    // store the names of our active actions
    QSet<QString> active_list;

    // Filtering message requested by translators (scripting).
    KLocalizedString nameFilter = ki18nc("@item:intable Action name in toolbar editor", "%1");

    // see if our current action is in this toolbar
    QDomNode n = elem.firstChild();
    for (; !n.isNull(); n = n.nextSibling()) {
        QDomElement it = n.toElement();
        if (it.isNull()) {
            continue;
        }
        if (it.tagName() == tagSeparator) {
            Entry act{tagSeparator, sep_name.arg(sep_num++), QString(), separatorstring};
            it.setAttribute(attrName, act.name);
            activeList.append(act);
            continue;
        }
        if (it.tagName() == tagSpacer) {
            Entry act{tagSpacer, spacer_name.arg(spacer_num++), QString(), spacerstring};
            it.setAttribute(attrName, act.name);
            activeList.append(act);
            continue;
        }

        if (it.tagName() == tagMerge) {
            // Merge can be named or not - use the name if there is one
            QString name = it.attribute(attrName);
            Entry act{tagMerge, name, i18n("This element will be replaced with all the elements of an embedded component.")};
            if (name.isEmpty()) {
                act.text = i18n("<Merge>");
            } else {
                act.text = i18n("<Merge %1>", name);
            }
            activeList.append(act);
            continue;
        }

        if (it.tagName() == tagActionList) {
            Entry act{tagActionList,
                it.attribute(attrName),
                i18n("This is a dynamic list of actions. You can move it, but if you remove it you will not be able to re-add it.")};
            act.text = i18n("ActionList: %1", it.attribute(attrName));
            activeList.append(act);
            continue;
        }

        // iterate through this client's actions
        // This used to iterate through _all_ actions, but we don't support
        // putting any action into any client...
        const auto actions = actionCollection->actions();
        for (QAction* action : actions) {
            // do we have a match?
            if (it.attribute(attrName) == action->objectName()) {
                // we have a match!
                Entry act{it.tagName(), action->objectName(), action->toolTip()};
                act.text = nameFilter.subs(KLocalizedString::removeAcceleratorMarker(action->iconText())).toString();
                act.icon = action->icon();
                act.iconName = action->icon().name();
                act.textAlongsideIconHidden = action->priority() < QAction::NormalPriority;
                activeList.append(act);

                active_list.insert(action->objectName());
                break;
            }
        }
    }

    // go through the rest of the collection
    const auto actions = actionCollection->actions();
    for (QAction* action : actions) {
        // skip our active ones
        if (active_list.contains(action->objectName())) {
            continue;
        }

        Entry act{tagAction, action->objectName(), action->toolTip()};
        act.text = nameFilter.subs(KLocalizedString::removeAcceleratorMarker(action->text())).toString();
        act.icon = action->icon();
        act.iconName = action->icon().name();
        inactiveList.append(act);
    }

    // As QListWidget::sortItems(): by text, case-sensitively, equal texts keeping their order.
    std::stable_sort(inactiveList.begin(), inactiveList.end(), [](const Entry& a, const Entry& b) {
        return a.text < b.text;
    });

    // finally, add default separators and spacers to the inactive list
    inactiveList.insert(0, Entry{tagSeparator, sep_name.arg(sep_num++), QString(), separatorstring});
    inactiveList.insert(1, Entry{tagSpacer, spacer_name.arg(spacer_num++), QString(), spacerstring});

    available->reset(std::move(inactiveList));
    current->reset(std::move(activeList));
}

void KToolBarEditorPrivate::insertActive(const Entry& item, const Entry* before, bool prepend)
{
    if (currentToolBarElem.isNull()) {
        return;
    }

    QDomElement new_item;
    // let's handle the separator and spacer specially
    if (item.tag == QLatin1String("Separator")) {
        new_item = elementDocument.createElement(QStringLiteral("Separator"));
    } else if (item.tag == QLatin1String("Spacer")) {
        new_item = elementDocument.createElement(QStringLiteral("Spacer"));
    } else {
        new_item = elementDocument.createElement(QStringLiteral("Action"));
    }

    new_item.setAttribute(QStringLiteral("name"), item.name);

    if (before) {
        // we have the item in the active list which is before the new
        // item.. so let's try our best to add our new item right after it
        QDomElement elem = findElement(*before);
        Q_ASSERT(!elem.isNull());
        currentToolBarElem.insertAfter(new_item, elem);
    } else {
        // simply put it at the beginning or the end of the list.
        if (prepend) {
            currentToolBarElem.insertBefore(new_item, currentToolBarElem.firstChild());
        } else {
            currentToolBarElem.appendChild(new_item);
        }
    }

    // and set this container as a noMerge
    currentToolBarElem.setAttribute(QStringLiteral("noMerge"), QStringLiteral("1"));

    // update the local doc
    updateLocal(currentToolBarElem);
}

bool KToolBarEditorPrivate::removeActive(const Entry& item)
{
    // now iterate through to find the child to nuke
    QDomElement elem = findElement(item);
    if (elem.isNull()) {
        return false;
    }

    // nuke myself!
    currentToolBarElem.removeChild(elem);

    // and set this container as a noMerge
    currentToolBarElem.setAttribute(QStringLiteral("noMerge"), QStringLiteral("1"));

    // update the local doc
    updateLocal(currentToolBarElem);
    return true;
}

void KToolBarEditorPrivate::updateLocal(QDomElement& elem)
{
    for (auto& xmlFile : xmlFiles) {
        if (xmlFile.type() == XmlData::Merged) {
            continue;
        }

        if (xmlFile.type() == XmlData::Shell || xmlFile.type() == XmlData::Part) {
            if (currentXmlData->xmlFile() == xmlFile.xmlFile()) {
                xmlFile.m_isModified = true;
                return;
            }

            continue;
        }

        xmlFile.m_isModified = true;
        const QLatin1String attrName("name");
        for (const auto& bar : xmlFile.barList()) {
            const QString name(bar.attribute(attrName));
            const QString tag(bar.tagName());
            if ((tag != elem.tagName()) || (name != elem.attribute(attrName))) {
                continue;
            }

            QDomElement toolbar = xmlFile.domDocument().documentElement().toElement();
            toolbar.replaceChild(elem, bar);
            return;
        }

        // just append it
        QDomElement toolbar = xmlFile.domDocument().documentElement().toElement();
        Q_ASSERT(!toolbar.isNull());
        toolbar.appendChild(elem);
    }
}

void KToolBarEditorPrivate::unload()
{
    xmlFiles.clear();
    currentXmlData = nullptr;
    currentToolBarElem = QDomElement();
    elementDocument = QDomDocument();
    client = std::make_unique<EditorClient>();
    factory = nullptr;
    collection = nullptr;
    loadedOnce = false;
}

KToolBarEditor::KToolBarEditor(QObject* parent)
    : QObject(parent)
    , d(new KToolBarEditorPrivate(this))
{
}

KToolBarEditor::~KToolBarEditor() = default;

bool KToolBarEditor::load(KXMLGUIFactory* factory, const QString& defaultToolBar)
{
    if (d->loadedOnce) {
        return false;
    }

    d->loadedOnce = true;
    d->factory = factory;
    d->defaultToolBar = defaultToolBar;

    // add all of the client data
    bool first = true;
    const auto clients = factory->clients();
    for (KXMLGUIClient* client : clients) {
        if (client->xmlFile().isEmpty()) {
            continue;
        }

        XmlData::XmlType type = XmlData::Part;
        if (first) {
            type = XmlData::Shell;
            first = false;
            Q_ASSERT(!client->localXMLFile().isEmpty()); // where would we save changes??
        }

        XmlData data(type, client->localXMLFile(), client->actionCollection());
        QDomDocument domDoc = client->domDocument();
        data.setDomDocument(domDoc);
        d->xmlFiles.append(data);
    }

    d->loadToolBars(defaultToolBar);
    return true;
}

bool KToolBarEditor::load(KActionCollection* collection, const QString& resourceFile, bool global, const QString& defaultToolBar)
{
    if (d->loadedOnce) {
        return false;
    }

    d->loadedOnce = true;
    d->collection = collection;
    d->resourceFile = resourceFile;
    d->global = global;
    d->defaultToolBar = defaultToolBar;

    // handle the merging
    if (global) {
        d->client->loadStandardsXmlFile(); // ui_standards.rc
    }
    const QString localXML = d->loadXMLFile(resourceFile);
    d->client->setXML(localXML, global ? true /*merge*/ : false);

    // first, get all of the necessary info for our local xml
    XmlData local(XmlData::Local, d->xmlFile(resourceFile), collection);
    QDomDocument domDoc;
    domDoc.setContent(localXML);
    local.setDomDocument(domDoc);
    d->xmlFiles.append(local);

    // then, the merged one (ui_standards + local xml)
    XmlData merge(XmlData::Merged, QString(), collection);
    merge.setDomDocument(d->client->domDocument());
    d->xmlFiles.append(merge);

    d->elementDocument = d->client->domDocument();
    d->loadToolBars(defaultToolBar);
    return true;
}

QStringList KToolBarEditor::toolBars() const
{
    return d->toolBarNames;
}

int KToolBarEditor::currentToolBar() const
{
    return d->currentToolBar;
}

void KToolBarEditor::setCurrentToolBar(int index)
{
    if (!d->selectToolBar(index)) {
        return;
    }
    if (index != d->currentToolBar) {
        d->currentToolBar = index;
        Q_EMIT currentToolBarChanged();
    }
}

QAbstractItemModel* KToolBarEditor::availableActions() const
{
    return d->available;
}

QAbstractItemModel* KToolBarEditor::currentActions() const
{
    return d->current;
}

int KToolBarEditor::insertAction(int availableRow, int currentRow, bool prepend)
{
    if (!d->available->isValidRow(availableRow)) {
        return -1;
    }
    const Entry item = d->available->entries.at(availableRow);
    const Entry* before = d->current->isValidRow(currentRow) ? &d->current->entries.at(currentRow) : nullptr;
    d->insertActive(item, before, prepend);
    // we're modified, so let this change
    Q_EMIT changed();

    d->selectToolBar(d->currentToolBar);

    const auto& entries = d->current->entries;
    const auto it = std::find_if(entries.cbegin(), entries.cend(), [&item](const Entry& entry) {
        return entry.name == item.name;
    });
    return it == entries.cend() ? -1 : int(it - entries.cbegin());
}

void KToolBarEditor::removeAction(int currentRow)
{
    if (!d->current->isValidRow(currentRow)) {
        return;
    }
    // we're modified, so let this change
    Q_EMIT changed();
    d->removeActive(d->current->entries.at(currentRow));

    d->selectToolBar(d->currentToolBar);
}

int KToolBarEditor::moveAction(int currentRow, int afterRow)
{
    auto& entries = d->current->entries;
    // Moving an element after itself leaves it where it is.
    if (!d->current->isValidRow(currentRow) || afterRow == currentRow || afterRow < -1 || afterRow >= entries.size()) {
        return currentRow;
    }
    QDomElement e = d->findElement(entries.at(currentRow));
    if (e.isNull()) {
        return currentRow;
    }

    // we're modified, so let this change
    Q_EMIT changed();

    // put it where it's supposed to go: after the row's element once taken out
    const int newRow = afterRow < currentRow ? afterRow + 1 : afterRow;
    if (afterRow == -1) {
        d->currentToolBarElem.insertBefore(e, d->currentToolBarElem.firstChild());
    } else {
        d->currentToolBarElem.insertAfter(e, d->findElement(entries.at(afterRow)));
    }
    d->current->move(currentRow, newRow);

    // and set this container as a noMerge
    d->currentToolBarElem.setAttribute(QStringLiteral("noMerge"), QStringLiteral("1"));

    // update the local doc
    d->updateLocal(d->currentToolBarElem);
    return newRow;
}

int KToolBarEditor::moveUp(int currentRow)
{
    // make sure we're not the top item already
    if (currentRow < 1 || !d->current->isValidRow(currentRow)) {
        return currentRow;
    }
    return moveAction(currentRow, currentRow - 2);
}

int KToolBarEditor::moveDown(int currentRow)
{
    // make sure we're not the bottom item already
    if (currentRow < 0 || !d->current->isValidRow(currentRow + 1)) {
        return currentRow;
    }
    return moveAction(currentRow, currentRow + 1);
}

void KToolBarEditor::setActionIcon(int currentRow, const QString& iconName)
{
    if (iconName.isEmpty() || !d->current->isValidRow(currentRow) || !d->currentXmlData) {
        return;
    }
    Q_ASSERT(d->currentXmlData->type() != XmlData::Merged);

    Entry& item = d->current->entries[currentRow];
    item.icon = QIcon::fromTheme(iconName);
    item.iconName = iconName;
    d->current->changedAt(currentRow);

    d->currentXmlData->m_isModified = true;

    // Get hold of ActionProperties tag
    QDomElement elem = KXMLGUIFactory::actionPropertiesElement(d->currentXmlData->domDocument());
    // Find or create an element for this action
    QDomElement act_elem = KXMLGUIFactory::findActionByName(elem, item.name, true /*create*/);
    Q_ASSERT(!act_elem.isNull());
    act_elem.setAttribute(QStringLiteral("icon"), iconName);

    // we're modified, so let this change
    Q_EMIT changed();
}

void KToolBarEditor::setActionIconText(int currentRow, const QString& text, bool textAlongsideIconHidden)
{
    if (!d->current->isValidRow(currentRow) || !d->currentXmlData) {
        return;
    }
    Entry& item = d->current->entries[currentRow];
    const QString iconText = text.trimmed();
    const bool hiddenChanged = textAlongsideIconHidden != item.textAlongsideIconHidden;
    const bool iconTextChanged = iconText != item.text;
    if (!hiddenChanged && !iconTextChanged) {
        return;
    }

    item.text = iconText;
    item.textAlongsideIconHidden = textAlongsideIconHidden;
    d->current->changedAt(currentRow);

    Q_ASSERT(d->currentXmlData->type() != XmlData::Merged);

    d->currentXmlData->m_isModified = true;

    // Get hold of ActionProperties tag
    QDomElement elem = KXMLGUIFactory::actionPropertiesElement(d->currentXmlData->domDocument());
    // Find or create an element for this action
    QDomElement act_elem = KXMLGUIFactory::findActionByName(elem, item.name, true /*create*/);
    Q_ASSERT(!act_elem.isNull());
    if (iconTextChanged) {
        act_elem.setAttribute(QStringLiteral("iconText"), iconText);
    }
    if (hiddenChanged) {
        act_elem.setAttribute(QStringLiteral("priority"), textAlongsideIconHidden ? QAction::LowPriority : QAction::NormalPriority);
    }

    // we're modified, so let this change
    Q_EMIT changed();
}

void KToolBarEditor::save()
{
    for (const auto& xmlFile : std::as_const(d->xmlFiles)) {
        // let's not save non-modified files
        if (!xmlFile.m_isModified) {
            continue;
        }

        // let's also skip (non-existent) merged files
        if (xmlFile.type() == XmlData::Merged) {
            continue;
        }

        if (xmlFile.type() == XmlData::Local) {
            // Add noMerge="1" to all the menus since we are saving the merged data
            QDomNodeList menuNodes = xmlFile.domDocument().elementsByTagName(QStringLiteral("Menu"));
            for (int i = 0; i < menuNodes.length(); ++i) {
                QDomNode menuNode = menuNodes.item(i);
                QDomElement menuElement = menuNode.toElement();
                if (menuElement.isNull()) {
                    continue;
                }
                menuElement.setAttribute(QStringLiteral("noMerge"), QStringLiteral("1"));
            }
        }

        // if we got this far, we might as well just save it
        KXMLGUIFactory::saveConfigFile(xmlFile.domDocument(), xmlFile.xmlFile());
    }

    if (!d->factory) {
        return;
    }

    rebuildClients();
}

void KToolBarEditor::rebuildClients()
{
    if (!d->factory) {
        return;
    }

    const QList<KXMLGUIClient*> clients = d->factory->clients();

    if (clients.isEmpty()) {
        return;
    }

    // remove the elements starting from the last going to the first
    for (auto it = clients.crbegin(); it != clients.crend(); ++it) {
        d->factory->removeClient(*it);
    }

    KXMLGUIClient* firstClient = clients.first();

    // now, rebuild the gui from the first to the last
    for (KXMLGUIClient* client : clients) {
        QString file(client->xmlFile()); // before setting ui_standards!
        if (!file.isEmpty()) {
            // passing an empty stream forces the clients to reread the XML
            client->setXMLGUIBuildDocument(QDomDocument());

            // for the shell, merge in ui_standards.rc
            if (client == firstClient) { // same assumption as in the ctor: first==shell
                client->loadStandardsXmlFile();
            }

            // and this forces it to use the *new* XML file
            client->setXMLFile(file, client == firstClient /* merge if shell */);

            // [we can't use reloadXML, it doesn't load ui_standards.rc]
        }
    }

    // Now we can add the clients to the factory
    // We don't do it in the loop above because adding a part automatically
    // adds its plugins, so we must make sure the plugins were updated first.
    for (KXMLGUIClient* client : clients) {
        d->factory->addClient(client);
    }
}

void KToolBarEditor::restoreDefaults()
{
    const QString defaultToolBar = d->defaultToolBar;
    if (KXMLGUIFactory* factory = d->factory) {
        const auto clients = factory->clients();
        for (KXMLGUIClient* client : clients) {
            const QString file = client->localXMLFile();
            if (file.isEmpty()) {
                continue;
            }
            if (QFile::exists(file)) {
                if (!QFile::remove(file)) {
                    qCWarning(DEBUG_KXMLGUI) << "Could not delete" << file;
                }
            }
        }

        // Reload the xml files in all clients, now that the local files are gone
        rebuildClients();

        d->unload();
        load(factory, defaultToolBar);
        return;
    }

    if (!d->loadedOnce) {
        return;
    }
    QString file = d->resourceFile;
    int slash = file.lastIndexOf(QLatin1Char('/')) + 1;
    if (slash) {
        file.remove(0, slash);
    }
    const QString xml_file = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1String("/kxmlgui5/")
        + QCoreApplication::instance()->applicationName() + QLatin1Char('/') + file;

    if (QFile::exists(xml_file)) {
        if (!QFile::remove(xml_file)) {
            qCWarning(DEBUG_KXMLGUI) << "Could not delete " << xml_file;
        }
    }

    KActionCollection* collection = d->collection;
    const bool global = d->global;
    d->unload();
    load(collection, file, global, defaultToolBar);
}

#include "moc_ktoolbareditor.cpp"
