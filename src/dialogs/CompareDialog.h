#ifndef COMPAREDIALOG_H
#define COMPAREDIALOG_H

#include <QDialog>
#include <QStringList>
#include <QColor>
#include <QList>
#include <QVector>
#include <QHash>
#include <QPair>
#include <QTextCursor>
#include <QTextEdit>

class QPlainTextEdit;
class QLabel;
class QAction;
class QComboBox;
class QPushButton;
class QLineEdit;
class DockedEditor;
class ScintillaNext;

class CompareDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CompareDialog(QWidget *parent = Q_NULLPTR);

    // Preload one side with text already open in the editor (e.g. the current tab)
    void setLeftText(const QString &text, const QString &label);
    void setRightText(const QString &text, const QString &label);

    // Lets the dialog offer "load from an already-open tab" instead of only
    // "open a file from disk" -- useful for comparing unsaved tabs (e.g. New1/New2)
    // that have no file on disk to open. preferredLeft/preferredRight, if given,
    // are pre-selected in the two tab dropdowns.
    void setAvailableTabs(DockedEditor *dockedEditor, ScintillaNext *preferredLeft = Q_NULLPTR, ScintillaNext *preferredRight = Q_NULLPTR);

    // Actions (e.g. the main window's Copy/Cut/Paste/Undo/Redo/Find) that use a
    // global menu shortcut and would otherwise steal Cmd+C/Cmd+V/Cmd+F away from
    // this dialog's own widgets while this window is active.
    void setActionsToSuspendWhileActive(const QList<QAction *> &actions);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void openLeftFile();
    void openRightFile();
    void loadLeftFromTab(int index);
    void loadRightFromTab(int index);
    void refreshLeftFromTab();
    void refreshRightFromTab();
    void copyLeftText();
    void copyRightText();
    void compare();
    void pickBackgroundColor();
    void pickTextColor();
    void pickAddedColor();
    void pickRemovedColor();
    void resetColors();
    void findNext();
    void findPrevious();

private:
    enum class RowKind { Equal, Removed, Added, Modified };

    void loadFileInto(QPlainTextEdit *editor, QLabel *label, const QString &side);
    void loadTabInto(QComboBox *combo, QPlainTextEdit *editor, QLabel *label);
    void populateTabCombo(QComboBox *combo, ScintillaNext *preferredSelection);
    void highlightDifferences();
    void paintRowHighlights();
    void applySelections();
    void applyEditorColors();
    void suspendConflictingShortcuts(bool suspend);
    void performFind(bool forward);

    QPlainTextEdit *leftEditor;
    QPlainTextEdit *rightEditor;
    QLabel *leftFileLabel;
    QLabel *rightFileLabel;
    QLabel *summaryLabel;
    QComboBox *leftTabCombo;
    QComboBox *rightTabCombo;
    QLineEdit *findLineEdit;
    QComboBox *findScopeCombo;

    DockedEditor *dockedEditor = Q_NULLPTR;

    static const QColor DefaultBackgroundColor;
    static const QColor DefaultTextColor;
    static const QColor DefaultAddedLineColor;
    static const QColor DefaultRemovedLineColor;
    static const QColor DefaultModifiedLineColor;
    static const QColor FindHighlightColor;

    QColor editorBackgroundColor { DefaultBackgroundColor };
    QColor editorTextColor { DefaultTextColor };
    QColor addedLineColor { DefaultAddedLineColor };
    QColor removedLineColor { DefaultRemovedLineColor };
    QColor modifiedLineColor { DefaultModifiedLineColor };

    QList<RowKind> lastRowKind;
    // For Modified rows only: the character ranges (start, length) that actually
    // differ within that row's left/right line, used for inline highlighting.
    QHash<int, QList<QPair<int, int>>> lastLeftModifiedRanges;
    QHash<int, QList<QPair<int, int>>> lastRightModifiedRanges;

    // The diff highlighting computed by paintRowHighlights(), kept separately so
    // a transient find-match highlight can be layered on top without being lost
    // the next time either is recomputed.
    QList<QTextEdit::ExtraSelection> leftRowSelections;
    QList<QTextEdit::ExtraSelection> rightRowSelections;
    QTextCursor findHighlightCursor;
    QPlainTextEdit *findHighlightEditor = Q_NULLPTR;

    QList<QAction *> conflictingActions;
};

#endif // COMPAREDIALOG_H
