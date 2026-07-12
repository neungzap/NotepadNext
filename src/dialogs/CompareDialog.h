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
#include <QWidget>
#include <QFont>

class QPlainTextEdit;
class QLabel;
class QAction;
class QComboBox;
class QPushButton;
class QLineEdit;
class QMouseEvent;
class QPaintEvent;
class DockedEditor;
class ScintillaNext;

// A thin vertical strip alongside a comparison pane that summarizes, top to
// bottom, where the added/removed/modified lines are in the whole document
// (like a miniature scrollbar-overview), plus the currently visible range.
// Click or drag on it to jump straight to that part of the file.
class DiffMinimap : public QWidget
{
public:
    explicit DiffMinimap(QPlainTextEdit *targetEditor, QWidget *parent = Q_NULLPTR);

    // 0 = Equal, 1 = Removed, 2 = Added, 3 = Modified -- kept as plain ints so
    // this widget doesn't need to know about CompareDialog's private enum.
    void setDiffRows(const QVector<int> &rowKinds);
    void setColors(const QColor &removed, const QColor &added, const QColor &modified);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void jumpToFraction(double fraction);

    QPlainTextEdit *targetEditor;
    QVector<int> diffRows;
    QColor removedColor;
    QColor addedColor;
    QColor modifiedColor;
};

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

    // Matches the font used by the main editor (Preferences > Default Font)
    // instead of the dialog's own hardcoded monospace font.
    void setEditorFont(const QFont &font);

    // Actions (e.g. the main window's Copy/Cut/Paste/Undo/Redo/Find) that use a
    // global menu shortcut and would otherwise steal Cmd+C/Cmd+V/Cmd+F away from
    // this dialog's own widgets while this window is active.
    void setActionsToSuspendWhileActive(const QList<QAction *> &actions);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    void applyLightTheme();
    void applyDarkTheme();
    void findNext();
    void findPrevious();
    void zoomIn();
    void zoomOut();
    void resetZoom();

private:
    enum class RowKind { Equal, Removed, Added, Modified };

    void loadFileInto(QPlainTextEdit *editor, QLabel *label, const QString &side);
    void loadTabInto(QComboBox *combo, QPlainTextEdit *editor, QLabel *label);
    void populateTabCombo(QComboBox *combo, ScintillaNext *preferredSelection);
    void highlightDifferences();
    void paintRowHighlights();
    void applySelections();
    void applyEditorColors();
    void applyThemePreset(const QColor &background, const QColor &text, const QColor &added, const QColor &removed, const QColor &modified);
    void suspendConflictingShortcuts(bool suspend);
    void performFind(bool forward);
    void updateMinimaps();
    void applyZoom();

    QPlainTextEdit *leftEditor;
    QPlainTextEdit *rightEditor;
    QLabel *leftFileLabel;
    QLabel *rightFileLabel;
    QLabel *summaryLabel;
    QComboBox *leftTabCombo;
    QComboBox *rightTabCombo;
    QLineEdit *findLineEdit;
    QComboBox *findScopeCombo;
    DiffMinimap *leftMinimap;
    DiffMinimap *rightMinimap;

    DockedEditor *dockedEditor = Q_NULLPTR;

    // The true, unpadded text for each side -- what gets copied to the
    // clipboard and what gets re-diffed on the next Compare, regardless of
    // whether the panes are currently showing the alignment-padded view.
    QString rawLeftText;
    QString rawRightText;
    bool isAligned = false;

    static const QColor DefaultBackgroundColor;
    static const QColor DefaultTextColor;
    static const QColor DefaultAddedLineColor;
    static const QColor DefaultRemovedLineColor;
    static const QColor DefaultModifiedLineColor;
    static const QColor FindHighlightColor;

    static const QColor DarkBackgroundColor;
    static const QColor DarkTextColor;
    static const QColor DarkAddedLineColor;
    static const QColor DarkRemovedLineColor;
    static const QColor DarkModifiedLineColor;

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

    QFont baseEditorFont;
    qreal zoomPointDelta = 0;
    qreal pinchAccumulator = 0;

    static const qreal MinZoomPointDelta;
    static const qreal MaxZoomPointDelta;
};

#endif // COMPAREDIALOG_H
