/*
    SPDX-FileCopyrightText: 2026 Joseph Crowell <joseph.w.crowell@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <KActionCollection>
#include <KEditToolBar>
#include <KLocalizedString>
#include <KToolBar>
#include <KToolBarEditor>
#include <KXMLGUIClient>
#include <KXMLGUIFactory>
#include <KXmlGuiWindow>

#include <QAction>
#include <QDataStream>
#include <QDialogButtonBox>
#include <QDomDocument>
#include <QDropEvent>
#include <QFile>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <memory>

namespace {
const QByteArray shellXml = "<!DOCTYPE gui SYSTEM \"kpartgui.dtd\">\n"
                            "<gui version=\"1\" name=\"ktoolbareditortest\">\n"
                            "<MenuBar/>\n"
                            "<ToolBar name=\"mainToolBar\" noMerge=\"1\">\n"
                            "  <text>Main Toolbar</text>\n"
                            "  <Action name=\"alpha\"/>\n"
                            "  <Separator/>\n"
                            "  <Action name=\"beta\"/>\n"
                            "  <Spacer/>\n"
                            "  <Action name=\"missing\"/>\n"
                            "  <Merge/>\n"
                            "  <ActionList name=\"dynamic\"/>\n"
                            "</ToolBar>\n"
                            "<ToolBar name=\"extraToolBar\">\n"
                            "  <text>Extra Toolbar</text>\n"
                            "  <Action name=\"gamma\"/>\n"
                            "</ToolBar>\n"
                            "<ToolBar name=\"fixedToolBar\" noEdit=\"true\">\n"
                            "  <text>Fixed Toolbar</text>\n"
                            "</ToolBar>\n"
                            "</gui>\n";

const QByteArray partXml = "<!DOCTYPE gui SYSTEM \"kpartgui.dtd\">\n"
                           "<gui version=\"1\" name=\"ktoolbareditortest_part\">\n"
                           "<ToolBar name=\"partToolBar\">\n"
                           "  <text>Part Toolbar</text>\n"
                           "  <Action name=\"partAction\"/>\n"
                           "</ToolBar>\n"
                           "</gui>\n";

// Texts in mixed case and with accelerator markers, so the sort and the markers count.
const QList<std::pair<QString, QString>> shellActions{
    {QStringLiteral("alpha"), QStringLiteral("&Alpha")},
    {QStringLiteral("beta"), QStringLiteral("Beta")},
    {QStringLiteral("gamma"), QStringLiteral("gamma")},
    {QStringLiteral("delta"), QStringLiteral("De&lta")},
    {QStringLiteral("epsilon"), QStringLiteral("epsilon")},
    {QStringLiteral("zeta"), QStringLiteral("Zeta")},
    {QStringLiteral("eta"), QStringLiteral("Zeta")},
};

void writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

class EditorWindow : public KXmlGuiWindow {
public:
    EditorWindow(const QString& xmlFile, const QString& localXmlFile)
        : m_xmlFile(xmlFile)
    {
        setLocalXMLFile(localXmlFile);
        for (const auto& [name, text] : shellActions) {
            QAction* action = actionCollection()->addAction(name);
            action->setText(text);
            action->setToolTip(text + QStringLiteral(" tip"));
        }
        createGUI(m_xmlFile);
    }

private:
    QString m_xmlFile;
};

class PartClient : public KXMLGUIClient {
public:
    PartClient(const QString& xmlFile, const QString& localXmlFile)
    {
        actionCollection()->addAction(QStringLiteral("partAction"))->setText(QStringLiteral("Part action"));
        setLocalXMLFile(localXmlFile);
        setXMLFile(xmlFile);
    }
};

QStringList column(const QAbstractItemModel* model, int role)
{
    QStringList values;
    for (int row = 0; row < model->rowCount(); ++row) {
        values.append(model->index(row, 0).data(role).toString());
    }
    return values;
}

QStringList texts(const QListWidget* list)
{
    QStringList values;
    for (int row = 0; row < list->count(); ++row) {
        values.append(list->item(row)->text());
    }
    return values;
}

int rowOf(const QAbstractItemModel* model, const QString& name)
{
    return int(column(model, KToolBarEditor::NameRole).indexOf(name));
}

// The toolbar element named mainToolBar in a client's document.
QDomElement mainToolBarElement(const QDomDocument& document)
{
    const QDomNodeList bars = document.elementsByTagName(QStringLiteral("ToolBar"));
    for (int i = 0; i < bars.count(); ++i) {
        if (bars.item(i).toElement().attribute(QStringLiteral("name")) == QLatin1String("mainToolBar")) {
            return bars.item(i).toElement();
        }
    }
    return QDomElement();
}
}

class KToolBarEditorTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        KLocalizedString::setApplicationDomain("ktoolbareditortest");
    }

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        writeFile(m_dir->filePath(QStringLiteral("shell.rc")), shellXml);
        writeFile(m_dir->filePath(QStringLiteral("part.rc")), partXml);
        m_window = std::make_unique<EditorWindow>(m_dir->filePath(QStringLiteral("shell.rc")), localFile());
        m_part = std::make_unique<PartClient>(m_dir->filePath(QStringLiteral("part.rc")), m_dir->filePath(QStringLiteral("part-local.rc")));
        m_window->guiFactory()->addClient(m_part.get());
    }

    void cleanup()
    {
        if (m_part->factory()) {
            m_part->factory()->removeClient(m_part.get());
        }
        m_window.reset();
        m_part.reset();
        m_dir.reset();
    }

    // Every toolbar of every client, the part's named after its document; noEdit ones left out.
    void testToolBarsListed()
    {
        KToolBarEditor editor;
        QVERIFY(editor.load(m_window->guiFactory(), QStringLiteral("extraToolBar")));
        QVERIFY(!editor.load(m_window->guiFactory()));

        QStringList expected;
        for (KXMLGUIClient* client : m_window->guiFactory()->clients()) {
            if (client->xmlFile().isEmpty()) {
                continue;
            }
            const QDomDocument document = client->domDocument();
            const QDomNodeList bars = document.elementsByTagName(QStringLiteral("ToolBar"));
            for (int i = 0; i < bars.count(); ++i) {
                const QDomElement bar = bars.item(i).toElement();
                if (bar.attribute(QStringLiteral("noEdit")) != QLatin1String("true")) {
                    expected.append(bar.firstChildElement(QStringLiteral("text")).text() + QStringLiteral(" <")
                        + document.documentElement().attribute(QStringLiteral("name")) + QLatin1Char('>'));
                }
            }
        }
        QCOMPARE(editor.toolBars(), expected);
        QCOMPARE(editor.currentToolBar(), int(expected.indexOf(QStringLiteral("Extra Toolbar <ktoolbareditortest>"))));

        // The part's toolbar lists the part's actions.
        editor.setCurrentToolBar(int(expected.indexOf(QStringLiteral("Part Toolbar <ktoolbareditortest_part>"))));
        QCOMPARE(column(editor.currentActions(), KToolBarEditor::NameRole), QStringList{QStringLiteral("partAction")});
    }

    // The current list follows the toolbar element; the available list is the separator,
    // the spacer and the other actions sorted as QListWidget::sortItems() sorts them.
    void testListsFollowTheXml()
    {
        KToolBarEditor editor;
        editor.load(m_window->guiFactory());
        const QAbstractItemModel* current = editor.currentActions();
        const QAbstractItemModel* available = editor.availableActions();
        KActionCollection* collection = m_window->actionCollection();

        QStringList tags;
        QStringList names;
        int separators = 0;
        int spacers = 0;
        const QDomElement bar = mainToolBarElement(m_window->domDocument());
        for (QDomElement e = bar.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (e.tagName() == QLatin1String("text")) {
                continue;
            }
            QString name = e.attribute(QStringLiteral("name"));
            if (e.tagName() == QLatin1String("Separator")) {
                name = QStringLiteral("separator_%1").arg(separators++);
            } else if (e.tagName() == QLatin1String("Spacer")) {
                name = QStringLiteral("spacer_%1").arg(spacers++);
            } else if (e.tagName() == QLatin1String("Action") && !collection->action(name)) {
                continue;
            }
            tags.append(e.tagName());
            names.append(name);
        }
        QCOMPARE(column(current, KToolBarEditor::TagRole), tags);
        QCOMPARE(column(current, KToolBarEditor::NameRole), names);

        for (int row = 0; row < current->rowCount(); ++row) {
            const QModelIndex index = current->index(row, 0);
            if (QAction* action = collection->action(index.data(KToolBarEditor::NameRole).toString())) {
                QCOMPARE(index.data(Qt::DisplayRole).toString(), KLocalizedString::removeAcceleratorMarker(action->iconText()));
                QCOMPARE(index.data(KToolBarEditor::StatusTextRole).toString(), action->toolTip());
            }
        }

        QCOMPARE(available->index(0, 0).data(KToolBarEditor::TagRole).toString(), QStringLiteral("Separator"));
        QCOMPARE(available->index(1, 0).data(KToolBarEditor::TagRole).toString(), QStringLiteral("Spacer"));
        QListWidget sorted;
        for (QAction* action : collection->actions()) {
            if (!names.contains(action->objectName())) {
                auto* item = new QListWidgetItem(KLocalizedString::removeAcceleratorMarker(action->text()), &sorted);
                item->setData(Qt::UserRole, action->objectName());
            }
        }
        sorted.sortItems(Qt::AscendingOrder);
        QStringList expected;
        for (int row = 0; row < sorted.count(); ++row) {
            expected.append(sorted.item(row)->data(Qt::UserRole).toString());
        }
        QCOMPARE(column(available, KToolBarEditor::NameRole).mid(2), expected);
    }

    // Inserted, removed and moved elements are where they were put, and stay there in the
    // real toolbar after saving.
    void testEditsReachTheToolBar()
    {
        KToolBarEditor editor;
        editor.load(m_window->guiFactory());
        const QAbstractItemModel* current = editor.currentActions();
        const QAbstractItemModel* available = editor.availableActions();

        QCOMPARE(editor.insertAction(rowOf(available, QStringLiteral("epsilon")), 0), 1);
        QCOMPARE(rowOf(current, QStringLiteral("epsilon")), 1);
        QCOMPARE(editor.insertAction(rowOf(available, QStringLiteral("delta")), -1, true), 0);
        const int appended = editor.insertAction(rowOf(available, QStringLiteral("zeta")), -1);
        QCOMPARE(appended, current->rowCount() - 1);
        QCOMPARE(rowOf(available, QStringLiteral("zeta")), -1);

        editor.removeAction(rowOf(current, QStringLiteral("beta")));
        QCOMPARE(rowOf(current, QStringLiteral("beta")), -1);
        QVERIFY(rowOf(available, QStringLiteral("beta")) >= 2);

        // Moves keep the rows (no reset), so views keep their selection.
        QSignalSpy resets(current, &QAbstractItemModel::modelReset);
        QSignalSpy moves(current, &QAbstractItemModel::rowsMoved);
        QStringList names = column(current, KToolBarEditor::NameRole);
        QCOMPARE(editor.moveDown(0), 1);
        names.swapItemsAt(0, 1);
        QCOMPARE(column(current, KToolBarEditor::NameRole), names);
        QCOMPARE(editor.moveUp(1), 0);
        names.swapItemsAt(0, 1);
        QCOMPARE(column(current, KToolBarEditor::NameRole), names);
        const int last = current->rowCount() - 1;
        QCOMPARE(editor.moveAction(last, -1), 0);
        names.move(last, 0);
        QCOMPARE(column(current, KToolBarEditor::NameRole), names);
        QCOMPARE(editor.moveAction(0, 2), 2);
        names.move(0, 2);
        QCOMPARE(column(current, KToolBarEditor::NameRole), names);
        QCOMPARE(editor.moveUp(0), 0);
        QCOMPARE(editor.moveDown(current->rowCount() - 1), current->rowCount() - 1);
        QCOMPARE(moves.count(), 4);
        QCOMPARE(resets.count(), 0);

        editor.save();
        QVERIFY(QFile::exists(localFile()));
        compareToolBar(editor);

        // A new editor reads the same lists back.
        KToolBarEditor reloaded;
        reloaded.load(m_window->guiFactory());
        QCOMPARE(column(reloaded.currentActions(), KToolBarEditor::NameRole), column(current, KToolBarEditor::NameRole));
        QCOMPARE(column(reloaded.currentActions(), KToolBarEditor::TagRole), column(current, KToolBarEditor::TagRole));
    }

    void testIconAndText()
    {
        KToolBarEditor editor;
        editor.load(m_window->guiFactory());
        const QAbstractItemModel* current = editor.currentActions();
        const int row = rowOf(current, QStringLiteral("alpha"));
        QSignalSpy data(current, &QAbstractItemModel::dataChanged);

        editor.setActionIcon(row, QString());
        QCOMPARE(data.count(), 0);
        editor.setActionIcon(row, QStringLiteral("document-save"));
        QCOMPARE(current->index(row, 0).data(KToolBarEditor::IconNameRole).toString(), QStringLiteral("document-save"));
        editor.setActionIconText(row, QStringLiteral("  Short  "), true);
        QCOMPARE(current->index(row, 0).data(Qt::DisplayRole).toString(), QStringLiteral("Short"));
        QVERIFY(current->index(row, 0).data(KToolBarEditor::TextAlongsideIconHiddenRole).toBool());
        QCOMPARE(data.count(), 2);
        editor.setActionIconText(row, QStringLiteral("Short"), true);
        QCOMPARE(data.count(), 2);

        editor.save();
        QAction* alpha = m_window->actionCollection()->action(QStringLiteral("alpha"));
        QCOMPARE(alpha->icon().name(), QStringLiteral("document-save"));
        QCOMPARE(alpha->iconText(), QStringLiteral("Short"));
        QCOMPARE(alpha->priority(), QAction::LowPriority);
    }

    // Defaults delete the local files and show the toolbars as a new editor loads them.
    void testRestoreDefaults()
    {
        KToolBarEditor editor;
        editor.load(m_window->guiFactory(), QStringLiteral("extraToolBar"));
        const int extra = editor.currentToolBar();
        editor.setCurrentToolBar(0);
        editor.insertAction(rowOf(editor.availableActions(), QStringLiteral("epsilon")), -1);
        editor.save();
        QVERIFY(QFile::exists(localFile()));

        QSignalSpy toolBars(&editor, &KToolBarEditor::toolBarsChanged);
        editor.restoreDefaults();
        QVERIFY(!QFile::exists(localFile()));
        QCOMPARE(toolBars.count(), 1);
        QCOMPARE(editor.currentToolBar(), extra);

        KToolBarEditor fresh;
        fresh.load(m_window->guiFactory(), QStringLiteral("extraToolBar"));
        QCOMPARE(editor.toolBars(), fresh.toolBars());
        editor.setCurrentToolBar(0);
        fresh.setCurrentToolBar(0);
        QCOMPARE(column(editor.currentActions(), KToolBarEditor::NameRole), column(fresh.currentActions(), KToolBarEditor::NameRole));
        QCOMPARE(column(editor.availableActions(), KToolBarEditor::NameRole), column(fresh.availableActions(), KToolBarEditor::NameRole));
        compareToolBar(editor);
    }

    void testChangesReported()
    {
        KToolBarEditor editor;
        editor.load(m_window->guiFactory());
        QSignalSpy changed(&editor, &KToolBarEditor::changed);
        editor.setCurrentToolBar(1);
        editor.setCurrentToolBar(0);
        QCOMPARE(changed.count(), 0);

        editor.insertAction(rowOf(editor.availableActions(), QStringLiteral("epsilon")), -1);
        QCOMPARE(changed.count(), 1);
        editor.moveUp(editor.currentActions()->rowCount() - 1);
        QCOMPARE(changed.count(), 2);
        editor.setActionIcon(0, QStringLiteral("document-save"));
        QCOMPARE(changed.count(), 3);
        editor.setActionIconText(0, QStringLiteral("Other"), false);
        QCOMPARE(changed.count(), 4);
        editor.removeAction(0);
        QCOMPARE(changed.count(), 5);
        editor.removeAction(-1);
        editor.moveUp(0);
        QCOMPARE(changed.count(), 5);
    }

    // KEditToolBar's lists are the editor's, and its buttons change the toolbar as the
    // editor does.
    void testWidgetFollowsTheEditor()
    {
        KEditToolBar dialog(m_window->guiFactory());
        QShowEvent show;
        QApplication::sendEvent(&dialog, &show);
        const auto lists = dialog.findChildren<QListWidget*>();
        QCOMPARE(lists.size(), 2);
        QListWidget* inactive = lists.at(0);
        QListWidget* active = lists.at(1);

        const auto sameAsEditor = [this, inactive, active] {
            KToolBarEditor editor;
            editor.load(m_window->guiFactory());
            QCOMPARE(texts(active), column(editor.currentActions(), Qt::DisplayRole));
            QCOMPARE(texts(inactive), column(editor.availableActions(), Qt::DisplayRole));
        };
        sameAsEditor();

        // up, insert, remove and down, in the order they are made
        const auto buttons = inactive->parentWidget()->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly);
        QCOMPARE(buttons.size(), 4);
        QToolButton* up = buttons.at(0);
        QToolButton* insert = buttons.at(1);
        QToolButton* remove = buttons.at(2);

        const QString epsilon = QStringLiteral("epsilon");
        inactive->setCurrentItem(inactive->findItems(epsilon, Qt::MatchExactly).value(0));
        active->setCurrentRow(0);
        insert->click();
        QCOMPARE(active->currentItem()->text(), epsilon);
        QCOMPARE(active->currentRow(), 1);
        up->click();
        QCOMPARE(active->currentRow(), 0);
        QCOMPARE(active->item(0)->text(), epsilon);

        clickApply(&dialog);
        sameAsEditor();

        active->setCurrentRow(0);
        remove->click();
        QVERIFY(active->findItems(epsilon, Qt::MatchExactly).isEmpty());
        clickApply(&dialog);
        sameAsEditor();
    }

    // Dropped items go where they are dropped: after the item above, or out of the toolbar.
    void testWidgetDrops()
    {
        KEditToolBar dialog(m_window->guiFactory());
        QShowEvent show;
        QApplication::sendEvent(&dialog, &show);
        dialog.resize(800, 600);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        const auto lists = dialog.findChildren<QListWidget*>();
        QListWidget* inactive = lists.at(0);
        QListWidget* active = lists.at(1);

        // Onto the upper part of row 1: after row 0.
        drop(active, active->visualItemRect(active->item(1)).topLeft() + QPoint(4, 1), QStringLiteral("Action"), QStringLiteral("epsilon"), false);
        QCOMPARE(active->item(1)->text(), QStringLiteral("epsilon"));
        // Within the list, onto the upper part of row 0: first.
        drop(active, active->visualItemRect(active->item(0)).topLeft() + QPoint(4, 1), QStringLiteral("Action"), QStringLiteral("epsilon"), true);
        QCOMPARE(active->item(0)->text(), QStringLiteral("epsilon"));
        // Onto the available list: out of the toolbar.
        drop(inactive, inactive->visualItemRect(inactive->item(0)).center(), QStringLiteral("Action"), QStringLiteral("epsilon"), true);
        QVERIFY(active->findItems(QStringLiteral("epsilon"), Qt::MatchExactly).isEmpty());
        QCOMPARE(inactive->findItems(QStringLiteral("epsilon"), Qt::MatchExactly).size(), 1);

        clickApply(&dialog);
        KToolBarEditor editor;
        editor.load(m_window->guiFactory());
        QCOMPARE(texts(active), column(editor.currentActions(), Qt::DisplayRole));
    }

    // A resource file without a factory: its toolbars, saved back into it.
    void testResourceFile()
    {
        const QString file = m_dir->filePath(QStringLiteral("resource.rc"));
        writeFile(file, shellXml);
        KActionCollection collection(static_cast<QObject*>(nullptr));
        for (const auto& [name, text] : shellActions) {
            collection.addAction(name)->setText(text);
        }

        KToolBarEditor editor;
        QVERIFY(editor.load(&collection, file, false));
        QCOMPARE(editor.toolBars(), (QStringList{QStringLiteral("Main Toolbar"), QStringLiteral("Extra Toolbar")}));
        QCOMPARE(column(editor.currentActions(), KToolBarEditor::NameRole).first(), QStringLiteral("alpha"));

        editor.insertAction(rowOf(editor.availableActions(), QStringLiteral("epsilon")), -1);
        editor.save();
        QDomDocument saved;
        saved.setContent(KXMLGUIFactory::readConfigFile(file));
        QVERIFY(mainToolBarElement(saved).lastChildElement(QStringLiteral("Action")).attribute(QStringLiteral("name")) == QLatin1String("epsilon"));
    }

private:
    QString localFile() const
    {
        return m_dir->filePath(QStringLiteral("shell-local.rc"));
    }

    static void clickApply(KEditToolBar* dialog)
    {
        auto* box = dialog->findChild<QDialogButtonBox*>();
        box->button(QDialogButtonBox::Apply)->click();
    }

    static void drop(QListWidget* list, const QPoint& pos, const QString& tag, const QString& name, bool fromActiveList)
    {
        QByteArray data;
        {
            QDataStream stream(&data, QIODevice::WriteOnly);
            stream << tag << name << QString() << false << false << false;
        }
        QMimeData mime;
        mime.setData(QStringLiteral("application/x-kde-action-list"), data);
        mime.setData(QStringLiteral("application/x-kde-source-treewidget"), fromActiveList ? "active" : "inactive");
        // As a drag delivers it: QApplication sends a drop to the widget that accepted the
        // drag's entry, and a view skips a drop already accepted. Without a drag the
        // platform's default action is used; the lists take moves only.
        QDragEnterEvent enter(pos, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        enter.setDropAction(Qt::MoveAction);
        enter.ignore();
        QApplication::sendEvent(list->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent event(pos, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        event.setDropAction(Qt::MoveAction);
        event.ignore();
        QApplication::sendEvent(list->viewport(), &event);
        QVERIFY(event.isAccepted());
    }

    // The named actions of the real toolbar are the current list's, in its order.
    void compareToolBar(const KToolBarEditor& editor)
    {
        auto* bar = qobject_cast<KToolBar*>(m_window->guiFactory()->container(QStringLiteral("mainToolBar"), m_window.get()));
        QVERIFY(bar);
        QStringList shown;
        for (QAction* action : bar->actions()) {
            if (!action->objectName().isEmpty() && m_window->actionCollection()->action(action->objectName()) == action) {
                shown.append(action->objectName());
            }
        }
        QStringList listed;
        const QAbstractItemModel* current = editor.currentActions();
        for (int row = 0; row < current->rowCount(); ++row) {
            if (current->index(row, 0).data(KToolBarEditor::TagRole).toString() == QLatin1String("Action")) {
                listed.append(current->index(row, 0).data(KToolBarEditor::NameRole).toString());
            }
        }
        QCOMPARE(shown, listed);
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<EditorWindow> m_window;
    std::unique_ptr<PartClient> m_part;
};

QTEST_MAIN(KToolBarEditorTest)

#include "ktoolbareditortest.moc"
