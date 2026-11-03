/*
    This file is part of the KDE libraries
    SPDX-FileCopyrightText: 2000 Kurt Granroth <granroth@kde.org>
    SPDX-FileCopyrightText: 2006 Hamish Rodda <rodda@kde.org>
    SPDX-FileCopyrightText: 2007 David Faure <faure@kde.org>
    SPDX-FileCopyrightText: 2026 Joseph Crowell <joseph.w.crowell@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KTOOLBAREDITOR_H
#define KTOOLBAREDITOR_H

#include <kxmlgui_export.h>

#include <QObject>
#include <QStringList>

#include <memory>

class QAbstractItemModel;
class KActionCollection;
class KXMLGUIFactory;
class KToolBarEditorPrivate;

/*!
 * \class KToolBarEditor
 * \inmodule KXmlGui
 *
 * \brief The toolbar editing of KEditToolBar, without widgets.
 *
 * Loads the toolbars of a KXMLGUIFactory's clients (or of one resource file), lists the
 * current toolbar's actions and the actions that can be added to it as item models, and
 * changes the clients' XML as KEditToolBar does: insert, remove and move actions, change
 * an action's icon and icon text, save, and restore the defaults. KEditToolBar's widget
 * and QML toolbar editors both use it.
 *
 * Rows are those of currentActions() and availableActions(); -1 means none.
 */
class KXMLGUI_EXPORT KToolBarEditor : public QObject {
    Q_OBJECT
    /*!
     * \property KToolBarEditor::toolBars
     * The user-visible names of the editable toolbars, in the factory's order.
     */
    Q_PROPERTY(QStringList toolBars READ toolBars NOTIFY toolBarsChanged)
    /*!
     * \property KToolBarEditor::currentToolBar
     * The index in toolBars() of the toolbar being edited.
     */
    Q_PROPERTY(int currentToolBar READ currentToolBar WRITE setCurrentToolBar NOTIFY currentToolBarChanged)
    /*!
     * \property KToolBarEditor::availableActions
     * The separator, the expanding spacer and the client's actions not in the toolbar,
     * sorted by text.
     */
    Q_PROPERTY(QAbstractItemModel* availableActions READ availableActions CONSTANT)
    /*!
     * \property KToolBarEditor::currentActions
     * The toolbar's elements in their order.
     */
    Q_PROPERTY(QAbstractItemModel* currentActions READ currentActions CONSTANT)

public:
    /*!
     * The item models' roles, besides Qt::DisplayRole (the text, role name "text") and
     * Qt::DecorationRole (the action's QIcon, role name "icon").
     *
     * \value NameRole The element's name.
     * \value TagRole The element's tag: Action, Separator, Spacer, Merge or ActionList.
     * \value StatusTextRole The help text shown for a selected element.
     * \value IconNameRole The name of the action's icon.
     * \value TextAlongsideIconHiddenRole Whether the text is hidden when the toolbar shows
     *        text alongside icons.
     */
    enum Role {
        NameRole = Qt::UserRole + 1,
        TagRole,
        StatusTextRole,
        IconNameRole,
        TextAlongsideIconHiddenRole,
    };
    Q_ENUM(Role)

    explicit KToolBarEditor(QObject* parent = nullptr);
    ~KToolBarEditor() override;

    /*!
     * Loads the toolbars of every client of \a factory, selecting \a defaultToolBar (by
     * name) or the first. Returns false if this editor was already loaded.
     */
    bool load(KXMLGUIFactory* factory, const QString& defaultToolBar = QString());

    /*!
     * Loads the toolbars of \a resourceFile (merged with ui_standards.rc if \a global)
     * with the actions of \a collection. Returns false if this editor was already loaded.
     */
    bool load(KActionCollection* collection, const QString& resourceFile, bool global = true, const QString& defaultToolBar = QString());

    QStringList toolBars() const;
    int currentToolBar() const;
    /*!
     * Selects the toolbar at \a index and reloads its lists, also when it is already the
     * current one.
     */
    void setCurrentToolBar(int index);

    QAbstractItemModel* availableActions() const;
    QAbstractItemModel* currentActions() const;

    /*!
     * Puts the available element at \a availableRow into the toolbar after the element at
     * \a currentRow, or at its start (\a prepend) or end when \a currentRow is -1. Returns
     * the new row of the inserted element.
     */
    Q_INVOKABLE int insertAction(int availableRow, int currentRow, bool prepend = false);
    /*!
     * Takes the element at \a currentRow out of the toolbar.
     */
    Q_INVOKABLE void removeAction(int currentRow);
    /*!
     * Moves the element at \a currentRow to after the element at \a afterRow (its row
     * before the move), or to the start when \a afterRow is -1. Returns its new row.
     */
    Q_INVOKABLE int moveAction(int currentRow, int afterRow);
    /*!
     * Moves the element at \a currentRow one place up. Returns its new row.
     */
    Q_INVOKABLE int moveUp(int currentRow);
    /*!
     * Moves the element at \a currentRow one place down. Returns its new row.
     */
    Q_INVOKABLE int moveDown(int currentRow);
    /*!
     * Gives the action at \a currentRow the icon named \a iconName.
     */
    Q_INVOKABLE void setActionIcon(int currentRow, const QString& iconName);
    /*!
     * Gives the action at \a currentRow the icon text \a text (trimmed), shown or hidden
     * alongside its icon.
     */
    Q_INVOKABLE void setActionIconText(int currentRow, const QString& text, bool textAlongsideIconHidden);

    /*!
     * Saves the changed clients' local XML files and rebuilds the factory's GUI.
     */
    Q_INVOKABLE void save();
    /*!
     * Removes and re-adds every client of the factory, so they read their XML again.
     */
    Q_INVOKABLE void rebuildClients();
    /*!
     * Deletes the local XML files, rebuilds the GUI and loads the toolbars again.
     */
    Q_INVOKABLE void restoreDefaults();

Q_SIGNALS:
    void toolBarsChanged();
    void currentToolBarChanged();
    /*!
     * Emitted for every change the user made.
     */
    void changed();

private:
    std::unique_ptr<KToolBarEditorPrivate> const d;
};

#endif
