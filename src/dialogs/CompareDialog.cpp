#include "CompareDialog.h"

#include "DockedEditor.h"
#include "ScintillaNext.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QFileDialog>
#include <QColorDialog>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QAction>
#include <QEvent>
#include <QCloseEvent>
#include <QComboBox>
#include <QLineEdit>
#include <QClipboard>
#include <QGuiApplication>
#include <QShortcut>
#include <QPainter>
#include <QMouseEvent>
#include <vector>

namespace {

enum class OpType { Equal, Delete, Insert };

struct DiffOp {
    OpType type;
    QString leftLine;
    QString rightLine;
};

// Simple LCS diff over any sequence of comparable strings, good enough for
// reasonably sized files/lines. Used both for line-level and (by treating each
// character as its own "line") character-level diffing.
QList<DiffOp> computeDiff(const QStringList &a, const QStringList &b)
{
    const int n = a.size();
    const int m = b.size();

    std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1, 0));
    for (int i = n - 1; i >= 0; --i) {
        for (int j = m - 1; j >= 0; --j) {
            if (a[i] == b[j]) {
                lcs[i][j] = lcs[i + 1][j + 1] + 1;
            }
            else {
                lcs[i][j] = qMax(lcs[i + 1][j], lcs[i][j + 1]);
            }
        }
    }

    QList<DiffOp> ops;
    int i = 0;
    int j = 0;
    while (i < n && j < m) {
        if (a[i] == b[j]) {
            ops.append({ OpType::Equal, a[i], b[j] });
            ++i;
            ++j;
        }
        else if (lcs[i + 1][j] >= lcs[i][j + 1]) {
            ops.append({ OpType::Delete, a[i], QString() });
            ++i;
        }
        else {
            ops.append({ OpType::Insert, QString(), b[j] });
            ++j;
        }
    }
    while (i < n) {
        ops.append({ OpType::Delete, a[i], QString() });
        ++i;
    }
    while (j < m) {
        ops.append({ OpType::Insert, QString(), b[j] });
        ++j;
    }

    return ops;
}

QStringList toChars(const QString &s)
{
    QStringList result;
    result.reserve(s.size());
    for (const QChar &c : s) {
        result << QString(c);
    }
    return result;
}

// Limit how big a line pair can be before we skip the (expensive) char-level diff
// and just treat the whole line as changed with no inline highlighting.
const int MaxCharDiffLineLength = 2000;

// A line pair is only worth highlighting as "modified" (vs. treating them as a
// separate pure remove + pure add) if they're reasonably similar.
const double ModifiedSimilarityThreshold = 0.3;

struct CharDiffResult {
    QList<QPair<int, int>> leftRanges;  // removed character ranges in the left line
    QList<QPair<int, int>> rightRanges; // added character ranges in the right line
    double similarity = 0.0;
};

CharDiffResult computeCharDiff(const QString &left, const QString &right)
{
    CharDiffResult result;

    if (left.size() > MaxCharDiffLineLength || right.size() > MaxCharDiffLineLength) {
        result.similarity = 0.0;
        return result;
    }

    const QList<DiffOp> ops = computeDiff(toChars(left), toChars(right));

    int equalCount = 0;
    int leftPos = 0;
    int rightPos = 0;
    int runStart = -1;

    for (const DiffOp &op : ops) {
        if (op.type == OpType::Equal) {
            if (runStart != -1) {
                result.leftRanges << qMakePair(runStart, leftPos - runStart);
                runStart = -1;
            }
            ++equalCount;
            ++leftPos;
            ++rightPos;
        }
        else if (op.type == OpType::Delete) {
            if (runStart == -1) runStart = leftPos;
            ++leftPos;
        }
        else {
            ++rightPos;
        }
    }
    if (runStart != -1) {
        result.leftRanges << qMakePair(runStart, leftPos - runStart);
    }

    leftPos = 0;
    rightPos = 0;
    runStart = -1;
    for (const DiffOp &op : ops) {
        if (op.type == OpType::Equal) {
            if (runStart != -1) {
                result.rightRanges << qMakePair(runStart, rightPos - runStart);
                runStart = -1;
            }
            ++leftPos;
            ++rightPos;
        }
        else if (op.type == OpType::Insert) {
            if (runStart == -1) runStart = rightPos;
            ++rightPos;
        }
        else {
            ++leftPos;
        }
    }
    if (runStart != -1) {
        result.rightRanges << qMakePair(runStart, rightPos - runStart);
    }

    const int maxLen = qMax(left.size(), right.size());
    result.similarity = maxLen == 0 ? 1.0 : double(equalCount) / double(maxLen);

    return result;
}

QPushButton *createColorSwatchButton(const QString &text, QWidget *parent)
{
    QPushButton *button = new QPushButton(text, parent);
    return button;
}

}

// ---------------------------------------------------------------- DiffMinimap

DiffMinimap::DiffMinimap(QPlainTextEdit *targetEditor, QWidget *parent)
    : QWidget(parent), targetEditor(targetEditor)
{
    setFixedWidth(14);
    setCursor(Qt::PointingHandCursor);
}

void DiffMinimap::setDiffRows(const QVector<int> &rowKinds)
{
    diffRows = rowKinds;
    update();
}

void DiffMinimap::setColors(const QColor &removed, const QColor &added, const QColor &modified)
{
    removedColor = removed;
    addedColor = added;
    modifiedColor = modified;
    update();
}

void DiffMinimap::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), palette().window());

    const int n = diffRows.size();
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            if (diffRows[i] == 0) continue; // Equal

            QColor color = removedColor;
            if (diffRows[i] == 2) color = addedColor;
            else if (diffRows[i] == 3) color = modifiedColor;

            const int y = i * height() / n;
            const int h = qMax(1, height() / n);
            painter.fillRect(2, y, width() - 4, h, color);
        }
    }

    // Viewport indicator: what part of the document is currently visible.
    if (targetEditor != Q_NULLPTR) {
        QScrollBar *vbar = targetEditor->verticalScrollBar();
        const int totalRange = vbar->maximum() + vbar->pageStep();
        if (totalRange > 0) {
            const double topFraction = double(vbar->value()) / totalRange;
            const double heightFraction = double(vbar->pageStep()) / totalRange;
            const int y = int(topFraction * height());
            const int h = qMax(3, int(heightFraction * height()));

            QColor indicatorColor = palette().highlight().color();
            indicatorColor.setAlpha(90);
            painter.fillRect(0, y, width(), h, indicatorColor);
            painter.setPen(palette().highlight().color());
            painter.drawRect(0, y, width() - 1, h - 1);
        }
    }
}

void DiffMinimap::mousePressEvent(QMouseEvent *event)
{
    jumpToFraction(double(event->position().y()) / qMax(1, height()));
}

void DiffMinimap::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        jumpToFraction(double(event->position().y()) / qMax(1, height()));
    }
}

void DiffMinimap::jumpToFraction(double fraction)
{
    if (targetEditor == Q_NULLPTR || diffRows.isEmpty()) {
        return;
    }

    const int line = qBound(0, int(fraction * diffRows.size()), diffRows.size() - 1);
    const QTextBlock block = targetEditor->document()->findBlockByNumber(line);
    if (!block.isValid()) {
        return;
    }

    QTextCursor cursor(block);
    targetEditor->setTextCursor(cursor);
    targetEditor->centerCursor();
}

// -------------------------------------------------------------- CompareDialog

const QColor CompareDialog::DefaultBackgroundColor = Qt::white;
const QColor CompareDialog::DefaultTextColor = Qt::black;
const QColor CompareDialog::DefaultAddedLineColor = QColor(205, 255, 205);
const QColor CompareDialog::DefaultRemovedLineColor = QColor(255, 205, 205);
const QColor CompareDialog::DefaultModifiedLineColor = QColor(255, 245, 200);
const QColor CompareDialog::FindHighlightColor = QColor(255, 230, 80);

const QColor CompareDialog::DarkBackgroundColor = QColor(0x1e, 0x1e, 0x1e);
const QColor CompareDialog::DarkTextColor = QColor(0xd4, 0xd4, 0xd4);
const QColor CompareDialog::DarkAddedLineColor = QColor(0x2d, 0x4a, 0x2d);
const QColor CompareDialog::DarkRemovedLineColor = QColor(0x4a, 0x2d, 0x2d);
const QColor CompareDialog::DarkModifiedLineColor = QColor(0x4a, 0x45, 0x2d);

CompareDialog::CompareDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Compare Files"));
    resize(1050, 700);

    QFont monoFont("Menlo");
    monoFont.setStyleHint(QFont::Monospace);

    leftFileLabel = new QLabel(tr("(no file)"), this);
    rightFileLabel = new QLabel(tr("(no file)"), this);

    leftTabCombo = new QComboBox(this);
    rightTabCombo = new QComboBox(this);
    leftTabCombo->addItem(tr("(choose an open tab)"), QVariant::fromValue<void *>(Q_NULLPTR));
    rightTabCombo->addItem(tr("(choose an open tab)"), QVariant::fromValue<void *>(Q_NULLPTR));

    QPushButton *openLeftButton = new QPushButton(tr("Open File..."), this);
    QPushButton *openRightButton = new QPushButton(tr("Open File..."), this);
    QPushButton *refreshLeftButton = new QPushButton(tr("Refresh"), this);
    QPushButton *refreshRightButton = new QPushButton(tr("Refresh"), this);
    QPushButton *copyLeftButton = new QPushButton(tr("Copy"), this);
    QPushButton *copyRightButton = new QPushButton(tr("Copy"), this);
    QPushButton *compareButton = new QPushButton(tr("Compare"), this);
    QPushButton *closeButton = new QPushButton(tr("Close"), this);

    QPushButton *bgColorButton = createColorSwatchButton(tr("Background Color..."), this);
    QPushButton *textColorButton = createColorSwatchButton(tr("Text Color..."), this);
    QPushButton *addedColorButton = createColorSwatchButton(tr("Added Color..."), this);
    QPushButton *removedColorButton = createColorSwatchButton(tr("Removed Color..."), this);
    QPushButton *lightThemeButton = new QPushButton(tr("Light Theme"), this);
    QPushButton *darkThemeButton = new QPushButton(tr("Dark Theme"), this);
    QPushButton *resetColorsButton = new QPushButton(tr("Reset to Defaults"), this);

    leftEditor = new QPlainTextEdit(this);
    rightEditor = new QPlainTextEdit(this);
    leftEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    rightEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    leftEditor->setFont(monoFont);
    rightEditor->setFont(monoFont);

    leftMinimap = new DiffMinimap(leftEditor, this);
    rightMinimap = new DiffMinimap(rightEditor, this);

    connect(leftEditor->verticalScrollBar(), &QScrollBar::valueChanged, rightEditor->verticalScrollBar(), &QScrollBar::setValue);
    connect(rightEditor->verticalScrollBar(), &QScrollBar::valueChanged, leftEditor->verticalScrollBar(), &QScrollBar::setValue);
    connect(leftEditor->horizontalScrollBar(), &QScrollBar::valueChanged, rightEditor->horizontalScrollBar(), &QScrollBar::setValue);
    connect(rightEditor->horizontalScrollBar(), &QScrollBar::valueChanged, leftEditor->horizontalScrollBar(), &QScrollBar::setValue);

    connect(leftEditor->verticalScrollBar(), &QScrollBar::valueChanged, leftMinimap, QOverload<>::of(&QWidget::update));
    connect(rightEditor->verticalScrollBar(), &QScrollBar::valueChanged, rightMinimap, QOverload<>::of(&QWidget::update));

    connect(leftEditor, &QPlainTextEdit::textChanged, this, [this]() {
        if (!isAligned) rawLeftText = leftEditor->toPlainText();
    });
    connect(rightEditor, &QPlainTextEdit::textChanged, this, [this]() {
        if (!isAligned) rawRightText = rightEditor->toPlainText();
    });

    QHBoxLayout *leftHeader = new QHBoxLayout();
    leftHeader->addWidget(new QLabel(tr("Tab:"), this));
    leftHeader->addWidget(leftTabCombo, 1);
    leftHeader->addWidget(refreshLeftButton);
    leftHeader->addWidget(openLeftButton);
    leftHeader->addWidget(copyLeftButton);

    QHBoxLayout *rightHeader = new QHBoxLayout();
    rightHeader->addWidget(new QLabel(tr("Tab:"), this));
    rightHeader->addWidget(rightTabCombo, 1);
    rightHeader->addWidget(refreshRightButton);
    rightHeader->addWidget(openRightButton);
    rightHeader->addWidget(copyRightButton);

    QHBoxLayout *leftEditorRow = new QHBoxLayout();
    leftEditorRow->addWidget(leftEditor);
    leftEditorRow->addWidget(leftMinimap);

    QHBoxLayout *rightEditorRow = new QHBoxLayout();
    rightEditorRow->addWidget(rightEditor);
    rightEditorRow->addWidget(rightMinimap);

    QVBoxLayout *leftPane = new QVBoxLayout();
    leftPane->addLayout(leftHeader);
    leftPane->addWidget(leftFileLabel);
    leftPane->addLayout(leftEditorRow);

    QVBoxLayout *rightPane = new QVBoxLayout();
    rightPane->addLayout(rightHeader);
    rightPane->addWidget(rightFileLabel);
    rightPane->addLayout(rightEditorRow);

    QHBoxLayout *panes = new QHBoxLayout();
    panes->addLayout(leftPane);
    panes->addLayout(rightPane);

    QHBoxLayout *colorBar = new QHBoxLayout();
    colorBar->addWidget(new QLabel(tr("Colors:"), this));
    colorBar->addWidget(bgColorButton);
    colorBar->addWidget(textColorButton);
    colorBar->addWidget(addedColorButton);
    colorBar->addWidget(removedColorButton);
    colorBar->addWidget(lightThemeButton);
    colorBar->addWidget(darkThemeButton);
    colorBar->addWidget(resetColorsButton);
    colorBar->addStretch(1);

    findLineEdit = new QLineEdit(this);
    findLineEdit->setPlaceholderText(tr("Find..."));
    findScopeCombo = new QComboBox(this);
    findScopeCombo->addItem(tr("Both"));
    findScopeCombo->addItem(tr("Left"));
    findScopeCombo->addItem(tr("Right"));
    QPushButton *findPreviousButton = new QPushButton(tr("Previous"), this);
    QPushButton *findNextButton = new QPushButton(tr("Next"), this);

    QHBoxLayout *findBar = new QHBoxLayout();
    findBar->addWidget(new QLabel(tr("Find:"), this));
    findBar->addWidget(findLineEdit, 1);
    findBar->addWidget(findScopeCombo);
    findBar->addWidget(findPreviousButton);
    findBar->addWidget(findNextButton);

    summaryLabel = new QLabel(this);

    QHBoxLayout *bottomBar = new QHBoxLayout();
    bottomBar->addWidget(summaryLabel, 1);
    bottomBar->addWidget(compareButton);
    bottomBar->addWidget(closeButton);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(colorBar);
    mainLayout->addLayout(panes);
    mainLayout->addLayout(findBar);
    mainLayout->addLayout(bottomBar);

    connect(openLeftButton, &QPushButton::clicked, this, &CompareDialog::openLeftFile);
    connect(openRightButton, &QPushButton::clicked, this, &CompareDialog::openRightFile);
    connect(refreshLeftButton, &QPushButton::clicked, this, &CompareDialog::refreshLeftFromTab);
    connect(refreshRightButton, &QPushButton::clicked, this, &CompareDialog::refreshRightFromTab);
    connect(copyLeftButton, &QPushButton::clicked, this, &CompareDialog::copyLeftText);
    connect(copyRightButton, &QPushButton::clicked, this, &CompareDialog::copyRightText);
    connect(leftTabCombo, QOverload<int>::of(&QComboBox::activated), this, &CompareDialog::loadLeftFromTab);
    connect(rightTabCombo, QOverload<int>::of(&QComboBox::activated), this, &CompareDialog::loadRightFromTab);
    connect(compareButton, &QPushButton::clicked, this, &CompareDialog::compare);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(bgColorButton, &QPushButton::clicked, this, &CompareDialog::pickBackgroundColor);
    connect(textColorButton, &QPushButton::clicked, this, &CompareDialog::pickTextColor);
    connect(addedColorButton, &QPushButton::clicked, this, &CompareDialog::pickAddedColor);
    connect(removedColorButton, &QPushButton::clicked, this, &CompareDialog::pickRemovedColor);
    connect(lightThemeButton, &QPushButton::clicked, this, &CompareDialog::applyLightTheme);
    connect(darkThemeButton, &QPushButton::clicked, this, &CompareDialog::applyDarkTheme);
    connect(resetColorsButton, &QPushButton::clicked, this, &CompareDialog::resetColors);
    connect(findNextButton, &QPushButton::clicked, this, &CompareDialog::findNext);
    connect(findPreviousButton, &QPushButton::clicked, this, &CompareDialog::findPrevious);
    connect(findLineEdit, &QLineEdit::returnPressed, this, &CompareDialog::findNext);

    QShortcut *findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [=]() {
        findLineEdit->setFocus();
        findLineEdit->selectAll();
    });

    applyEditorColors();
}

void CompareDialog::setActionsToSuspendWhileActive(const QList<QAction *> &actions)
{
    conflictingActions = actions;
}

void CompareDialog::setEditorFont(const QFont &font)
{
    leftEditor->setFont(font);
    rightEditor->setFont(font);
}

void CompareDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);

    if (event->type() == QEvent::ActivationChange) {
        suspendConflictingShortcuts(isActiveWindow());
    }
}

void CompareDialog::closeEvent(QCloseEvent *event)
{
    suspendConflictingShortcuts(false);
    QDialog::closeEvent(event);
}

void CompareDialog::suspendConflictingShortcuts(bool suspend)
{
    // On macOS the app's menu bar is shared across all windows, so a QAction's
    // shortcut (e.g. Cmd+C on the main window's Copy action) is otherwise still
    // triggered even while this dialog's own text widgets have focus, "stealing"
    // Cmd+C / Cmd+V / Cmd+F away from them. Disabling those actions while this
    // dialog is the active window lets the shortcut fall through to the focused
    // widget instead.
    for (QAction *action : std::as_const(conflictingActions)) {
        if (action != Q_NULLPTR) {
            action->setEnabled(!suspend);
        }
    }
}

void CompareDialog::setLeftText(const QString &text, const QString &label)
{
    isAligned = false;
    rawLeftText = text;
    leftEditor->setPlainText(text);
    leftFileLabel->setText(label);
}

void CompareDialog::setRightText(const QString &text, const QString &label)
{
    isAligned = false;
    rawRightText = text;
    rightEditor->setPlainText(text);
    rightFileLabel->setText(label);
}

void CompareDialog::populateTabCombo(QComboBox *combo, ScintillaNext *preferredSelection)
{
    combo->clear();
    combo->addItem(tr("(choose an open tab)"), QVariant::fromValue<void *>(Q_NULLPTR));

    if (dockedEditor == Q_NULLPTR) {
        return;
    }

    int preferredIndex = 0;
    const QVector<ScintillaNext *> editors = dockedEditor->editors();
    for (ScintillaNext *editor : editors) {
        combo->addItem(editor->getName(), QVariant::fromValue<void *>(editor));
        if (editor == preferredSelection) {
            preferredIndex = combo->count() - 1;
        }
    }

    combo->setCurrentIndex(preferredIndex);
}

void CompareDialog::setAvailableTabs(DockedEditor *editorManager, ScintillaNext *preferredLeft, ScintillaNext *preferredRight)
{
    dockedEditor = editorManager;

    populateTabCombo(leftTabCombo, preferredLeft);
    populateTabCombo(rightTabCombo, preferredRight);

    if (leftTabCombo->currentIndex() > 0) {
        loadLeftFromTab(leftTabCombo->currentIndex());
    }
    if (rightTabCombo->currentIndex() > 0) {
        loadRightFromTab(rightTabCombo->currentIndex());
    }
}

void CompareDialog::loadTabInto(QComboBox *combo, QPlainTextEdit *editor, QLabel *label)
{
    ScintillaNext *scintilla = static_cast<ScintillaNext *>(combo->currentData().value<void *>());

    if (scintilla == Q_NULLPTR || dockedEditor == Q_NULLPTR || !dockedEditor->editors().contains(scintilla)) {
        return;
    }

    const QString text = QString::fromUtf8(scintilla->get_text_range(0, static_cast<int>(scintilla->length())));

    isAligned = false;
    if (editor == leftEditor) rawLeftText = text;
    else rawRightText = text;

    editor->setPlainText(text);
    label->setText(scintilla->getName());
}

void CompareDialog::loadLeftFromTab(int index)
{
    Q_UNUSED(index)
    loadTabInto(leftTabCombo, leftEditor, leftFileLabel);
}

void CompareDialog::loadRightFromTab(int index)
{
    Q_UNUSED(index)
    loadTabInto(rightTabCombo, rightEditor, rightFileLabel);
}

void CompareDialog::refreshLeftFromTab()
{
    loadTabInto(leftTabCombo, leftEditor, leftFileLabel);
}

void CompareDialog::refreshRightFromTab()
{
    loadTabInto(rightTabCombo, rightEditor, rightFileLabel);
}

void CompareDialog::copyLeftText()
{
    // Copies the true original text, not the alignment-padded version shown
    // in the pane after a Compare (which has blank filler lines inserted).
    QGuiApplication::clipboard()->setText(rawLeftText);
}

void CompareDialog::copyRightText()
{
    QGuiApplication::clipboard()->setText(rawRightText);
}

void CompareDialog::openLeftFile()
{
    loadFileInto(leftEditor, leftFileLabel, tr("left"));
}

void CompareDialog::openRightFile()
{
    loadFileInto(rightEditor, rightFileLabel, tr("right"));
}

void CompareDialog::loadFileInto(QPlainTextEdit *editor, QLabel *label, const QString &side)
{
    const QString filePath = QFileDialog::getOpenFileName(this, tr("Select %1 file to compare").arg(side));
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    const QString text = stream.readAll();

    isAligned = false;
    if (editor == leftEditor) rawLeftText = text;
    else rawRightText = text;

    editor->setPlainText(text);
    label->setText(QFileInfo(filePath).fileName());
}

void CompareDialog::compare()
{
    if (!isAligned) {
        rawLeftText = leftEditor->toPlainText();
        rawRightText = rightEditor->toPlainText();
    }

    highlightDifferences();
}

void CompareDialog::applyEditorColors()
{
    const QString styleSheet = QString("QPlainTextEdit { background-color: %1; color: %2; }")
        .arg(editorBackgroundColor.name(), editorTextColor.name());

    leftEditor->setStyleSheet(styleSheet);
    rightEditor->setStyleSheet(styleSheet);

    paintRowHighlights();
}

void CompareDialog::pickBackgroundColor()
{
    const QColor color = QColorDialog::getColor(editorBackgroundColor, this, tr("Choose Background Color"));
    if (color.isValid()) {
        editorBackgroundColor = color;
        applyEditorColors();
    }
}

void CompareDialog::pickTextColor()
{
    const QColor color = QColorDialog::getColor(editorTextColor, this, tr("Choose Text Color"));
    if (color.isValid()) {
        editorTextColor = color;
        applyEditorColors();
    }
}

void CompareDialog::pickAddedColor()
{
    const QColor color = QColorDialog::getColor(addedLineColor, this, tr("Choose Added Line Color"));
    if (color.isValid()) {
        addedLineColor = color;
        paintRowHighlights();
    }
}

void CompareDialog::pickRemovedColor()
{
    const QColor color = QColorDialog::getColor(removedLineColor, this, tr("Choose Removed Line Color"));
    if (color.isValid()) {
        removedLineColor = color;
        paintRowHighlights();
    }
}

void CompareDialog::resetColors()
{
    applyThemePreset(DefaultBackgroundColor, DefaultTextColor, DefaultAddedLineColor, DefaultRemovedLineColor, DefaultModifiedLineColor);
}

void CompareDialog::applyLightTheme()
{
    applyThemePreset(DefaultBackgroundColor, DefaultTextColor, DefaultAddedLineColor, DefaultRemovedLineColor, DefaultModifiedLineColor);
}

void CompareDialog::applyDarkTheme()
{
    applyThemePreset(DarkBackgroundColor, DarkTextColor, DarkAddedLineColor, DarkRemovedLineColor, DarkModifiedLineColor);
}

void CompareDialog::applyThemePreset(const QColor &background, const QColor &text, const QColor &added, const QColor &removed, const QColor &modified)
{
    editorBackgroundColor = background;
    editorTextColor = text;
    addedLineColor = added;
    removedLineColor = removed;
    modifiedLineColor = modified;
    applyEditorColors();
}

void CompareDialog::highlightDifferences()
{
    // The document contents are about to be replaced (realigned), which would
    // leave any previous find-match highlight pointing at stale positions.
    findHighlightEditor = Q_NULLPTR;
    findHighlightCursor = QTextCursor();

    const QStringList leftLines = rawLeftText.split('\n');
    const QStringList rightLines = rawRightText.split('\n');

    const QList<DiffOp> ops = computeDiff(leftLines, rightLines);

    QStringList alignedLeft;
    QStringList alignedRight;
    QList<RowKind> rowKind;
    QHash<int, QList<QPair<int, int>>> leftModifiedRanges;
    QHash<int, QList<QPair<int, int>>> rightModifiedRanges;

    // Pair up adjacent "block of removed lines" + "block of inserted lines" as
    // line replacements when the lines are similar enough, so we can highlight
    // just the part of the line that actually changed instead of the whole line.
    for (int i = 0; i < ops.size(); ) {
        if (ops[i].type == OpType::Equal) {
            alignedLeft << ops[i].leftLine;
            alignedRight << ops[i].rightLine;
            rowKind << RowKind::Equal;
            ++i;
            continue;
        }

        int deleteRunEnd = i;
        while (deleteRunEnd < ops.size() && ops[deleteRunEnd].type == OpType::Delete) ++deleteRunEnd;
        int insertRunEnd = deleteRunEnd;
        while (insertRunEnd < ops.size() && ops[insertRunEnd].type == OpType::Insert) ++insertRunEnd;

        const int deleteCount = deleteRunEnd - i;
        const int insertCount = insertRunEnd - deleteRunEnd;
        const int pairCount = qMin(deleteCount, insertCount);

        for (int p = 0; p < pairCount; ++p) {
            const QString &leftLine = ops[i + p].leftLine;
            const QString &rightLine = ops[deleteRunEnd + p].rightLine;

            const CharDiffResult charDiff = computeCharDiff(leftLine, rightLine);

            if (charDiff.similarity >= ModifiedSimilarityThreshold) {
                const int rowIndex = alignedLeft.size();
                alignedLeft << leftLine;
                alignedRight << rightLine;
                rowKind << RowKind::Modified;
                if (!charDiff.leftRanges.isEmpty()) leftModifiedRanges[rowIndex] = charDiff.leftRanges;
                if (!charDiff.rightRanges.isEmpty()) rightModifiedRanges[rowIndex] = charDiff.rightRanges;
            }
            else {
                alignedLeft << leftLine;
                alignedRight << QString();
                rowKind << RowKind::Removed;

                alignedLeft << QString();
                alignedRight << rightLine;
                rowKind << RowKind::Added;
            }
        }

        for (int p = pairCount; p < deleteCount; ++p) {
            alignedLeft << ops[i + p].leftLine;
            alignedRight << QString();
            rowKind << RowKind::Removed;
        }
        for (int p = pairCount; p < insertCount; ++p) {
            alignedLeft << QString();
            alignedRight << ops[deleteRunEnd + p].rightLine;
            rowKind << RowKind::Added;
        }

        i = insertRunEnd;
    }

    isAligned = true;
    leftEditor->setPlainText(alignedLeft.join('\n'));
    rightEditor->setPlainText(alignedRight.join('\n'));

    lastRowKind = rowKind;
    lastLeftModifiedRanges = leftModifiedRanges;
    lastRightModifiedRanges = rightModifiedRanges;
    paintRowHighlights();
}

void CompareDialog::paintRowHighlights()
{
    QList<QTextEdit::ExtraSelection> leftSelections;
    QList<QTextEdit::ExtraSelection> rightSelections;

    QTextBlock leftBlock = leftEditor->document()->firstBlock();
    QTextBlock rightBlock = rightEditor->document()->firstBlock();

    const QColor unchangedGapColor = editorBackgroundColor.lightness() < 128 ? editorBackgroundColor.lighter(130) : editorBackgroundColor.darker(110);
    const QColor leftInlineColor = removedLineColor.darker(115);
    const QColor rightInlineColor = addedLineColor.darker(115);

    int changedCount = 0;

    for (int i = 0; i < lastRowKind.size() && leftBlock.isValid() && rightBlock.isValid(); ++i) {
        const RowKind kind = lastRowKind[i];

        if (kind != RowKind::Equal) {
            ++changedCount;

            QColor leftColor = unchangedGapColor;
            QColor rightColor = unchangedGapColor;
            if (kind == RowKind::Removed) leftColor = removedLineColor;
            else if (kind == RowKind::Modified) { leftColor = modifiedLineColor; rightColor = modifiedLineColor; }
            if (kind == RowKind::Added) rightColor = addedLineColor;

            QTextEdit::ExtraSelection leftSel;
            leftSel.format.setBackground(leftColor);
            leftSel.format.setProperty(QTextFormat::FullWidthSelection, true);
            leftSel.cursor = QTextCursor(leftBlock);
            leftSelections << leftSel;

            QTextEdit::ExtraSelection rightSel;
            rightSel.format.setBackground(rightColor);
            rightSel.format.setProperty(QTextFormat::FullWidthSelection, true);
            rightSel.cursor = QTextCursor(rightBlock);
            rightSelections << rightSel;

            if (kind == RowKind::Modified) {
                for (const QPair<int, int> &range : lastLeftModifiedRanges.value(i)) {
                    QTextCursor cursor(leftBlock);
                    cursor.setPosition(leftBlock.position() + range.first);
                    cursor.setPosition(leftBlock.position() + range.first + range.second, QTextCursor::KeepAnchor);

                    QTextEdit::ExtraSelection inlineSel;
                    inlineSel.format.setBackground(leftInlineColor);
                    inlineSel.cursor = cursor;
                    leftSelections << inlineSel;
                }
                for (const QPair<int, int> &range : lastRightModifiedRanges.value(i)) {
                    QTextCursor cursor(rightBlock);
                    cursor.setPosition(rightBlock.position() + range.first);
                    cursor.setPosition(rightBlock.position() + range.first + range.second, QTextCursor::KeepAnchor);

                    QTextEdit::ExtraSelection inlineSel;
                    inlineSel.format.setBackground(rightInlineColor);
                    inlineSel.cursor = cursor;
                    rightSelections << inlineSel;
                }
            }
        }

        leftBlock = leftBlock.next();
        rightBlock = rightBlock.next();
    }

    leftRowSelections = leftSelections;
    rightRowSelections = rightSelections;
    applySelections();
    updateMinimaps();

    if (!lastRowKind.isEmpty()) {
        if (changedCount == 0) {
            summaryLabel->setText(tr("Files are identical."));
        }
        else {
            summaryLabel->setText(tr("%n differing line(s) found.", "", changedCount));
        }
    }
}

void CompareDialog::updateMinimaps()
{
    QVector<int> rowKinds;
    rowKinds.reserve(lastRowKind.size());
    for (RowKind kind : std::as_const(lastRowKind)) {
        rowKinds << static_cast<int>(kind);
    }

    leftMinimap->setColors(removedLineColor, addedLineColor, modifiedLineColor);
    rightMinimap->setColors(removedLineColor, addedLineColor, modifiedLineColor);
    leftMinimap->setDiffRows(rowKinds);
    rightMinimap->setDiffRows(rowKinds);
}

void CompareDialog::applySelections()
{
    QList<QTextEdit::ExtraSelection> left = leftRowSelections;
    QList<QTextEdit::ExtraSelection> right = rightRowSelections;

    if (findHighlightEditor == leftEditor && findHighlightCursor.hasSelection()) {
        QTextEdit::ExtraSelection highlight;
        highlight.cursor = findHighlightCursor;
        highlight.format.setBackground(FindHighlightColor);
        left << highlight;
    }
    else if (findHighlightEditor == rightEditor && findHighlightCursor.hasSelection()) {
        QTextEdit::ExtraSelection highlight;
        highlight.cursor = findHighlightCursor;
        highlight.format.setBackground(FindHighlightColor);
        right << highlight;
    }

    leftEditor->setExtraSelections(left);
    rightEditor->setExtraSelections(right);
}

void CompareDialog::findNext()
{
    performFind(true);
}

void CompareDialog::findPrevious()
{
    performFind(false);
}

void CompareDialog::performFind(bool forward)
{
    const QString needle = findLineEdit->text();
    if (needle.isEmpty()) {
        return;
    }

    const int scope = findScopeCombo->currentIndex(); // 0 = Both, 1 = Left, 2 = Right

    // Prefer whichever editor currently has focus so repeated Find Next/Previous
    // presses continue from where the last match left off.
    QPlainTextEdit *primary = rightEditor->hasFocus() ? rightEditor : leftEditor;
    QPlainTextEdit *secondary = (primary == leftEditor) ? rightEditor : leftEditor;

    QList<QPlainTextEdit *> candidates;
    if (scope == 1) candidates << leftEditor;
    else if (scope == 2) candidates << rightEditor;
    else candidates << primary << secondary;

    QTextDocument::FindFlags flags;
    if (!forward) flags |= QTextDocument::FindBackward;

    bool found = false;
    for (QPlainTextEdit *editor : std::as_const(candidates)) {
        if (editor->find(needle, flags)) {
            found = true;
            editor->setFocus();
            findHighlightEditor = editor;
            findHighlightCursor = editor->textCursor();
            applySelections();
            break;
        }
    }

    if (!found) {
        summaryLabel->setText(tr("\"%1\" not found.").arg(needle));
    }
}
