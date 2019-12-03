
/*
 * Copyright (c) 2014-2019 Patrizio Bekerle -- http://www.bekerle.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * QPlainTextEdit markdown highlighter
 */

#include <QTimer>
#include <QDebug>
#include <QTextDocument>
#include "markdownhighlighter.h"
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <utility>


/**
 * Markdown syntax highlighting
 *
 * markdown syntax:
 * http://daringfireball.net/projects/markdown/syntax
 *
 * @param parent
 * @return
 */
MarkdownHighlighter::MarkdownHighlighter(QTextDocument *parent,
                                         HighlightingOptions highlightingOptions)
        : QSyntaxHighlighter(parent),
          _highlightingOptions(highlightingOptions) {
   // _highlightingOptions = highlightingOptions;
    _timer = new QTimer(this);
    QObject::connect(_timer, SIGNAL(timeout()),
                     this, SLOT(timerTick()));
    _timer->start(1000);

    // initialize the highlighting rules
    initHighlightingRules();

    // initialize the text formats
    initTextFormats();
}

/**
 * Does jobs every second
 */
void MarkdownHighlighter::timerTick() {
    // qDebug() << "timerTick: " << this << ", " << this->parent()->parent()->parent()->objectName();

    // re-highlight all dirty blocks
    reHighlightDirtyBlocks();

    // emit a signal every second if there was some highlighting done
    if (_highlightingFinished) {
        _highlightingFinished = false;
        emit(highlightingFinished());
    }
}

/**
 * Re-highlights all dirty blocks
 */
void MarkdownHighlighter::reHighlightDirtyBlocks() {
    while (_dirtyTextBlocks.count() > 0) {
        QTextBlock block = _dirtyTextBlocks.at(0);
        rehighlightBlock(block);
        _dirtyTextBlocks.removeFirst();
    }
}

/**
 * Clears the dirty blocks vector
 */
void MarkdownHighlighter::clearDirtyBlocks() {
    _dirtyTextBlocks.clear();
}

/**
 * Adds a dirty block to the list if it doesn't already exist
 *
 * @param block
 */
void MarkdownHighlighter::addDirtyBlock(const QTextBlock& block) {
    if (!_dirtyTextBlocks.contains(block)) {
        _dirtyTextBlocks.append(block);
    }
}

/**
 * Initializes the highlighting rules
 *
 * regexp tester:
 * https://regex101.com
 *
 * other examples:
 * /usr/share/kde4/apps/katepart/syntax/markdown.xml
 */
void MarkdownHighlighter::initHighlightingRules() {
    // highlight the reference of reference links
    {
        HighlightingRule rule(HighlighterState::MaskedSyntax);
        rule.pattern = QRegularExpression(QStringLiteral(R"(^\[.+?\]: \w+://.+$)"));
        _highlightingRulesPre.append(rule);
    }

    // highlight unordered lists
    {
        HighlightingRule rule(HighlighterState::List);
        rule.pattern = QRegularExpression(QStringLiteral("^\\s*[-*+]\\s"));
        rule.useStateAsCurrentBlockState = true;
        _highlightingRulesPre.append(rule);

        // highlight ordered lists
        rule.pattern = QRegularExpression(QStringLiteral(R"(^\s*\d+\.\s)"));
        _highlightingRulesPre.append(rule);
    }

    // highlight block quotes
    {
        HighlightingRule rule(HighlighterState::BlockQuote);
        rule.pattern = QRegularExpression(
                    _highlightingOptions.testFlag(
                        HighlightingOption::FullyHighlightedBlockQuote) ?
                        QStringLiteral("^\\s*(>\\s*.+)") : QStringLiteral("^\\s*(>\\s*)+"));
        _highlightingRulesPre.append(rule);
    }

    // highlight horizontal rulers
    {
        HighlightingRule rule(HighlighterState::HorizontalRuler);
        rule.pattern = QRegularExpression(QStringLiteral("^([*\\-_]\\s?){3,}$"));
        _highlightingRulesPre.append(rule);
    }

    // highlight tables without starting |
    // we drop that for now, it's far too messy to deal with
//    rule = HighlightingRule();
//    rule.pattern = QRegularExpression("^.+? \\| .+? \\| .+$");
//    rule.state = HighlighterState::Table;
//    _highlightingRulesPre.append(rule);

    /*
     * highlight italic
     * this goes before bold so that bold can overwrite italic
     *
     * text to test:
     * **bold** normal **bold**
     * *start of line* normal
     * normal *end of line*
     * * list item *italic*
     */
    {
        HighlightingRule rule(HighlighterState::Italic);
        // we don't allow a space after the starting * to prevent problems with
        // unordered lists starting with a *
        rule.pattern = QRegularExpression(
                    QStringLiteral(R"((?:^|[^\*\b])(?:\*([^\* ][^\*]*?)\*)(?:[^\*\b]|$))"));
        rule.capturingGroup = 1;
        _highlightingRulesAfter.append(rule);

        rule.pattern = QRegularExpression(QStringLiteral("\\b_([^_]+)_\\b"));
        _highlightingRulesAfter.append(rule);
    }

    {
        HighlightingRule rule(HighlighterState::Bold);
        // highlight bold
        rule.pattern = QRegularExpression(QStringLiteral(R"(\B\*{2}(.+?)\*{2}\B)"));
        rule.capturingGroup = 1;
        _highlightingRulesAfter.append(rule);
        rule.pattern = QRegularExpression(QStringLiteral("\\b__(.+?)__\\b"));
        _highlightingRulesAfter.append(rule);
    }

    {
        HighlightingRule rule(HighlighterState::MaskedSyntax);
        // highlight strike through
        rule.pattern = QRegularExpression(QStringLiteral(R"(\~{2}(.+?)\~{2})"));
        rule.capturingGroup = 1;
        _highlightingRulesAfter.append(rule);
    }

    // highlight urls
    {
        HighlightingRule rule(HighlighterState::Link);

        // highlight urls without any other markup
        rule.pattern = QRegularExpression(QStringLiteral(R"(\b\w+?:\/\/[^\s]+)"));
        rule.capturingGroup = 0;
        _highlightingRulesAfter.append(rule);

        // highlight urls with <> but without any . in it
        rule.pattern = QRegularExpression(QStringLiteral(R"(<(\w+?:\/\/[^\s]+)>)"));
        rule.capturingGroup = 1;
        _highlightingRulesAfter.append(rule);

        // highlight links with <> that have a .in it
        //    rule.pattern = QRegularExpression("<(.+?:\\/\\/.+?)>");
        rule.pattern = QRegularExpression(QStringLiteral("<([^\\s`][^`]*?\\.[^`]*?[^\\s`])>"));
        rule.capturingGroup = 1;
        _highlightingRulesAfter.append(rule);

        // highlight urls with title
        //    rule.pattern = QRegularExpression("\\[(.+?)\\]\\(.+?://.+?\\)");
        //    rule.pattern = QRegularExpression("\\[(.+?)\\]\\(.+\\)\\B");
        rule.pattern = QRegularExpression(QStringLiteral(R"(\[([^\[\]]+)\]\((\S+|.+?)\)\B)"));
        _highlightingRulesAfter.append(rule);

        // highlight urls with empty title
        //    rule.pattern = QRegularExpression("\\[\\]\\((.+?://.+?)\\)");
        rule.pattern = QRegularExpression(QStringLiteral(R"(\[\]\((.+?)\))"));
        _highlightingRulesAfter.append(rule);

        // highlight email links
        rule.pattern = QRegularExpression(QStringLiteral("<(.+?@.+?)>"));
        _highlightingRulesAfter.append(rule);

        // highlight reference links
        rule.pattern = QRegularExpression(QStringLiteral(R"(\[(.+?)\]\[.+?\])"));
        _highlightingRulesAfter.append(rule);
    }

    // Images
    {
        // highlight images with text
        HighlightingRule rule(HighlighterState::Image);
        rule.pattern = QRegularExpression(QStringLiteral(R"(!\[(.+?)\]\(.+?\))"));
        rule.capturingGroup = 1;
        _highlightingRulesAfter.append(rule);

        // highlight images without text
        rule.pattern = QRegularExpression(QStringLiteral(R"(!\[\]\((.+?)\))"));
        _highlightingRulesAfter.append(rule);
    }

    // highlight images links
    {
//        HighlightingRule rule;
        HighlightingRule rule(HighlighterState::Link);
        rule.pattern = QRegularExpression(QStringLiteral(R"(\[!\[(.+?)\]\(.+?\)\]\(.+?\))"));
        rule.capturingGroup = 1;
        _highlightingRulesAfter.append(rule);

        // highlight images links without text
        rule.pattern = QRegularExpression(QStringLiteral(R"(\[!\[\]\(.+?\)\]\((.+?)\))"));
        _highlightingRulesAfter.append(rule);
    }

    // highlight trailing spaces
    {
        HighlightingRule rule(HighlighterState::TrailingSpace);
        rule.pattern = QRegularExpression(QStringLiteral("( +)$"));
        rule.capturingGroup = 1;
        _highlightingRulesAfter.append(rule);
    }

    // highlight inline code
    {
        HighlightingRule rule(HighlighterState::InlineCodeBlock);
//        HighlightingRule rule;
        rule.pattern = QRegularExpression(QStringLiteral("`(.+?)`"));
        rule.capturingGroup = 1;
        _highlightingRulesAfter.append(rule);
    }

    // highlight code blocks with four spaces or tabs in front of them
    // and no list character after that
    {
        HighlightingRule rule(HighlighterState::CodeBlock);
//        HighlightingRule rule;
        rule.pattern = QRegularExpression(QStringLiteral("^((\\t)|( {4,})).+$"));
        rule.disableIfCurrentStateIsSet = true;
        _highlightingRulesAfter.append(rule);
    }

    // highlight inline comments
    {
        HighlightingRule rule(HighlighterState::Comment);
        rule.pattern = QRegularExpression(QStringLiteral(R"(<!\-\-(.+?)\-\->)"));
        rule.capturingGroup = 1;
        _highlightingRulesAfter.append(rule);

        // highlight comments for Rmarkdown for academic papers
        rule.pattern = QRegularExpression(QStringLiteral(R"(^\[.+?\]: # \(.+?\)$)"));
        _highlightingRulesAfter.append(rule);
    }

    // highlight tables with starting |
    {
        HighlightingRule rule(HighlighterState::Table);
        rule.pattern = QRegularExpression(QStringLiteral("^\\|.+?\\|$"));
        _highlightingRulesAfter.append(rule);
    }
}

/**
 * Initializes the text formats
 *
 * @param defaultFontSize
 */
void MarkdownHighlighter::initTextFormats(int defaultFontSize) {
    QTextCharFormat format;

    // set character formats for headlines
    format = QTextCharFormat();
    format.setForeground(QBrush(QColor(0, 49, 110)));
    format.setFontWeight(QFont::Bold);
    format.setFontPointSize(defaultFontSize * 1.6);
    _formats[H1] = format;
    format.setFontPointSize(defaultFontSize * 1.5);
    _formats[H2] = format;
    format.setFontPointSize(defaultFontSize * 1.4);
    _formats[H3] = format;
    format.setFontPointSize(defaultFontSize * 1.3);
    _formats[H4] = format;
    format.setFontPointSize(defaultFontSize * 1.2);
    _formats[H5] = format;
    format.setFontPointSize(defaultFontSize * 1.1);
    _formats[H6] = format;
    format.setFontPointSize(defaultFontSize);

    // set character format for horizontal rulers
    format = QTextCharFormat();
    format.setForeground(QBrush(Qt::darkGray));
    format.setBackground(QBrush(Qt::lightGray));
    _formats[HorizontalRuler] = format;

    // set character format for lists
    format = QTextCharFormat();
    format.setForeground(QBrush(QColor(163, 0, 123)));
    _formats[List] = format;

    // set character format for links
    format = QTextCharFormat();
    format.setForeground(QBrush(QColor(0, 128, 255)));
    format.setFontUnderline(true);
    _formats[Link] = format;

    // set character format for images
    format = QTextCharFormat();
    format.setForeground(QBrush(QColor(0, 191, 0)));
    format.setBackground(QBrush(QColor(228, 255, 228)));
    _formats[Image] = format;

    // set character format for code blocks
    format = QTextCharFormat();
    format.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    format.setBackground(QColor(220, 220, 220));
    _formats[CodeBlock] = format;
    _formats[InlineCodeBlock] = format;

    // set character format for italic
    format = QTextCharFormat();
    format.setFontWeight(QFont::StyleItalic);
    format.setFontItalic(true);
    _formats[Italic] = format;

    // set character format for bold
    format = QTextCharFormat();
    format.setFontWeight(QFont::Bold);
    _formats[Bold] = format;

    // set character format for comments
    format = QTextCharFormat();
    format.setForeground(QBrush(Qt::gray));
    _formats[Comment] = format;

    // set character format for masked syntax
    format = QTextCharFormat();
    format.setForeground(QBrush("#cccccc"));
    _formats[MaskedSyntax] = format;

    // set character format for tables
    format = QTextCharFormat();
    format.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    format.setForeground(QBrush(QColor("#649449")));
    _formats[Table] = format;

    // set character format for block quotes
    format = QTextCharFormat();
    format.setForeground(QBrush(QColor(Qt::darkRed)));
    _formats[BlockQuote] = format;

    format = QTextCharFormat();
    _formats[HeadlineEnd] = format;

    format = QTextCharFormat();
    _formats[NoState] = format;
}

/**
 * Sets the text formats
 *
 * @param formats
 */
void MarkdownHighlighter::setTextFormats(
        QHash<HighlighterState, QTextCharFormat> formats) {
    _formats = std::move(formats);
}

/**
 * Sets a text format
 *
 * @param formats
 */
void MarkdownHighlighter::setTextFormat(HighlighterState state,
                                        QTextCharFormat format) {
    _formats[state] = std::move(format);
}

/**
 * Does the markdown highlighting
 *
 * @param text
 */
void MarkdownHighlighter::highlightBlock(const QString &text) {
    setCurrentBlockState(HighlighterState::NoState);
    currentBlock().setUserState(HighlighterState::NoState);
    highlightMarkdown(text);
    _highlightingFinished = true;
}

void MarkdownHighlighter::highlightMarkdown(const QString& text) {
    if (!text.isEmpty()) {
        highlightAdditionalRules(_highlightingRulesPre, text);

        // needs to be called after the horizontal ruler highlighting
        highlightHeadline(text);

        highlightAdditionalRules(_highlightingRulesAfter, text);
    }

    highlightCommentBlock(text);
    highlightCodeBlock(text);
    highlightFrontmatterBlock(text);
}

/**
 * Highlight headlines
 *
 * @param text
 */
void MarkdownHighlighter::highlightHeadline(const QString& text) {
    bool headingFound = text.startsWith(QLatin1String("# ")) ||
                        text.startsWith(QLatin1String("## ")) ||
                        text.startsWith(QLatin1String("### ")) ||
                        text.startsWith(QLatin1String("#### ")) ||
                        text.startsWith(QLatin1String("##### ")) ||
                        text.startsWith(QLatin1String("###### "));

    const QTextCharFormat &maskedFormat = _formats[HighlighterState::MaskedSyntax];

    if (headingFound) {
        int count = 0;
        int len = text.length() > 6 ? 6 : text.length();
        //check only first 6 chars of text
        for (int i = 0; i < len; ++i) {
            if (text.at(i) == QLatin1Char('#')) {
                ++count;
            }
        }

        const auto state = HighlighterState(HighlighterState::H1 + count - 1);

        QTextCharFormat &format = _formats[state];
        QTextCharFormat currentMaskedFormat = maskedFormat;

        // set the font size from the current rule's font format
        currentMaskedFormat.setFontPointSize(format.fontPointSize());

        // first highlight everything as MaskedSyntax
        setFormat(0, text.length(), currentMaskedFormat);

        const int length = text.length() - count;
        // then highlight with the real format
        setFormat(count, length, _formats[state]);

        // set a margin for the current block
        setCurrentBlockMargin(state);

        setCurrentBlockState(state);
        currentBlock().setUserState(state);
        return;
    }

    auto hasOnlyHeadChars = [](const QString &txt, const QChar c) -> bool {
        if (txt.isEmpty()) return false;
        for (int i = 0; i < txt.length(); ++i) {
            if (txt.at(i) != c)
                return false;
        }
        return true;
    };

    // take care of ==== and ---- headlines

    QTextBlock previousBlock = currentBlock().previous();
    const QString &previousText = previousBlock.text();

    const bool pattern1 = hasOnlyHeadChars(text, QLatin1Char('='));
    if (pattern1) {

        if (( (previousBlockState() == HighlighterState::H1) ||
               previousBlockState() == HighlighterState::NoState) &&
               previousText.length() > 0) {
            QTextCharFormat currentMaskedFormat = maskedFormat;
            // set the font size from the current rule's font format
            currentMaskedFormat.setFontPointSize(
                        _formats[HighlighterState::H1].fontPointSize());

            setFormat(0, text.length(), currentMaskedFormat);
            setCurrentBlockState(HighlighterState::HeadlineEnd);
            previousBlock.setUserState(HighlighterState::H1);

            // set a margin for the current block
            setCurrentBlockMargin(HighlighterState::H1);

            // we want to re-highlight the previous block
            // this must not done directly, but with a queue, otherwise it
            // will crash
            // setting the character format of the previous text, because this
            // causes text to be formatted the same way when writing after
            // the text
            addDirtyBlock(previousBlock);
        }
        return;
    }

    const bool pattern2 = hasOnlyHeadChars(text, QLatin1Char('-'));
    if (pattern2) {

        if (( (previousBlockState() == HighlighterState::H2) ||
               previousBlockState() == HighlighterState::NoState) &&
               previousText.length() > 0) {
            // set the font size from the current rule's font format
            QTextCharFormat currentMaskedFormat = maskedFormat;
            currentMaskedFormat.setFontPointSize(
                        _formats[HighlighterState::H2].fontPointSize());

            setFormat(0, text.length(), currentMaskedFormat);
            setCurrentBlockState(HighlighterState::HeadlineEnd);
            previousBlock.setUserState(HighlighterState::H2);

            // set a margin for the current block
            setCurrentBlockMargin(HighlighterState::H2);

            // we want to re-highlight the previous block
            addDirtyBlock(previousBlock);
        }
        return;
    }

    //check next block for ====
    QTextBlock nextBlock = currentBlock().next();
    const QString &nextBlockText = nextBlock.text();
    const bool nextHasEqualChars = hasOnlyHeadChars(nextBlockText, QLatin1Char('='));
    if (nextHasEqualChars) {
        setFormat(0, text.length(), _formats[HighlighterState::H1]);
        setCurrentBlockState(HighlighterState::H1);
        currentBlock().setUserState(HighlighterState::H1);
    }
    //check next block for ----
    const bool nextHasMinusChars = hasOnlyHeadChars(nextBlockText, QLatin1Char('-'));
    if (nextHasMinusChars) {
        setFormat(0, text.length(), _formats[HighlighterState::H2]);
        setCurrentBlockState(HighlighterState::H2);
        currentBlock().setUserState(HighlighterState::H2);
    }
}


/**
 * Sets a margin for the current block
 *
 * @param state
 */
void MarkdownHighlighter::setCurrentBlockMargin(
        MarkdownHighlighter::HighlighterState state) {
    // this is currently disabled because it causes multiple problems:
    // - it prevents "undo" in headlines
    //   https://github.com/pbek/QOwnNotes/issues/520
    // - invisible lines at the end of a note
    //   https://github.com/pbek/QOwnNotes/issues/667
    // - a crash when reaching the invisible lines when the current line is
    //   highlighted
    //   https://github.com/pbek/QOwnNotes/issues/701
    return;

    qreal margin;

    switch (state) {
        case HighlighterState::H1:
            margin = 5;
            break;
        case HighlighterState::H2:
        case HighlighterState::H3:
        case HighlighterState::H4:
        case HighlighterState::H5:
        case HighlighterState::H6:
            margin = 3;
            break;
        default:
            return;
    }

    QTextBlockFormat blockFormat = currentBlock().blockFormat();
    blockFormat.setTopMargin(2);
    blockFormat.setBottomMargin(margin);

    // this prevents "undo" in headlines!
    QTextCursor* myCursor = new QTextCursor(currentBlock());
    myCursor->setBlockFormat(blockFormat);
}

/**
 * Highlight multi-line code blocks
 *
 * @param text
 */
void MarkdownHighlighter::highlightCodeBlock(const QString& text) {

    if (text.startsWith("```")) {
        if (previousBlockState() != HighlighterState::CodeBlock &&
                previousBlockState() < 200) {
            QStringRef lang = text.midRef(3, text.length());
            if (lang == "cpp") {
                setCurrentBlockState(HighlighterState::CodeCpp);
            } else if (lang == "js") {
                setCurrentBlockState(HighlighterState::CodeJs);
            } else if (text == "```"){
                setCurrentBlockState(HighlighterState::CodeBlock);
            } else {
                setCurrentBlockState(HighlighterState::CodeBlock);
            }
        } else if (previousBlockState() == HighlighterState::CodeBlock ||
                   previousBlockState() >= 200) {
            setCurrentBlockState(HighlighterState::CodeBlockEnd);
        }

        // set the font size from the current rule's font format
        QTextCharFormat &maskedFormat =
                _formats[HighlighterState::MaskedSyntax];
        maskedFormat.setFontPointSize(
                    _formats[HighlighterState::CodeBlock].fontPointSize());

        setFormat(0, text.length(), maskedFormat);
    } else if (previousBlockState() == HighlighterState::CodeBlock ||
               previousBlockState() == HighlighterState::CodeCpp) {

        if (previousBlockState() == HighlighterState::CodeCpp) {
            setCurrentBlockState(HighlighterState::CodeCpp);
            highlightSyntax(text);
        } else if (previousBlockState() == HighlighterState::CodeJs){
            setCurrentBlockState(HighlighterState::CodeJs);
            highlightSyntax(text);
        } else {
            setFormat(0, text.length(), _formats[HighlighterState::CodeBlock]);
            setCurrentBlockState(HighlighterState::CodeBlock);
        }
    }
}

void loadCppData(QStringList &types, QStringList &keywords, QStringList &preproc) {
    types = QStringList{
        /* Qt specific */
        "QString", "QList", "QVector", "QHash", "QMap",
        /*cpp */
        "int", "float", "string", "double", "long", "vector",
        "short", "char", "void", "bool", "wchar_t",
        "class", "struct", "union", "enum"
    };

    keywords = QStringList{
        "while", "if", "for", "do", "return", "else", "switch",
        "case", "break", "continue",
        "namespace", "using",
        "unsigned", "const", "static", "mutable", "auto"
        "asm ", "volatile",
        "static_cast", "dynamic_cast", "reinterpret_cast", "const_cast",
        "nullptr",
        "public", "private", "protected", "signal", "slot",
        "new", "delete", "operator", "template", "this",
        "false", "true", "explicit ", "sizeof",
        "try", "catch", "throw"
    };

    preproc = QStringList{
        "ifndef", "ifdef", "include", "define", "endif"
    };
}

/**
 * @brief Does the code syntax highlighting
 * @param text
 */
void MarkdownHighlighter::highlightSyntax(const QString &text)
{
    if (text.isEmpty()) return;

    QStringList types,
                keywords,
                preproc;

    switch (currentBlockState()) {
        case HighlighterState::CodeCpp :
            loadCppData(types, keywords, preproc);
            break;
    }

    // keep the default code block format
    QTextCharFormat f = _formats[CodeBlock];
    setFormat(0, text.length(), f);

    for (int i=0; i< text.length(); i++) {

        while (!text[i].isLetter()) {
            //inline comment
            if (text[i] == QLatin1Char('/')) {
                if((i+1) < text.length()){
                    if(text[i+1] == QLatin1Char('/')) {
                        f.setForeground(Qt::darkGray);
                        setFormat(i, text.length(), f);
                        return;
                    } else if(text[i+1] == QLatin1Char('*')) {
                        int next = text.indexOf("*/");
                        f.setForeground(Qt::darkGray);
                        if (next == -1) {
                            setFormat(i, text.length(), f);
                            return;
                        } else {
                            next += 2;
                            setFormat(i, next - i, f);
                            i = next;
                            if (i >= text.length()) return;
                        }
                    }
                }
                return;
            //multi comment
            } else if (text[i] == QLatin1Char('/') && text[i+1] == QLatin1Char('*')) {
                int next = text.indexOf("*/");
                f.setForeground(Qt::darkGray);
                if (next == -1) {
                    setFormat(i, text.length(), f);
                } else {
                    next += 2;
                    setFormat(i, next - i, f);
                }
                return;
            //integer literal
            } else if (text[i].isNumber()) {
                if ( ((i+1) < text.length() && (i-1) > 0 ) &&
                     (text[i+1].isLetter() || text[i-1].isLetter()) ) {
                    i++;
                    continue;
                }
                f.setForeground(Qt::darkYellow);
                setFormat(i, 1, f);
            //string literal
            } else if (text[i] == "\"") {
                int pos = i;
                int cnt = 1;
                f.setForeground(Qt::darkGreen);
                i++;
                //bound check
                if ( (i+1) >= text.length()) return;
                while (i < text.length()) {
                    if (text[i] == "\"") {
                        cnt++;
                        i++;
                        break;
                    }
                    i++; cnt++;
                    //bound check
                    if ( (i+1) >= text.length()) {
                        cnt++;
                        break;
                    }
                }
                setFormat(pos, cnt, f);
            }  else if (text[i] == "\'") {
                int pos = i;
                int cnt = 1;
                f.setForeground(Qt::darkGreen);
                i++;
                //bound check
                if ( (i+1) >= text.length()) return;
                while (i < text.length()) {
                    if (text[i] == "\'") {
                        cnt++;
                        i++;
                        break;
                    }
                    //bound check
                    if ( (i+1) >= text.length()) {
                        cnt++;
                        break;
                    }
                    i++; cnt++;
                }
                setFormat(pos, cnt, f);
            }
            if (i+1 >= text.length()) return;
            i++;
        }

        for (int j = 0; j < types.length(); j++) {
            if (types[j] == text.midRef(i, types[j].length())) {
                //check if we are in the middle of a word
                if ( (i+types[j].length()) < text.length() && i > 0) {
                    if (text[i + types[j].length()].isLetter() || text[i-1].isLetter()) {
                        continue;
                    }
                }
                f.setForeground(Qt::darkBlue);
                setFormat(i, types[j].length(), f);
                i += types[j].length();
            }
        }

        for (int j = 0; j < keywords.length(); j++) {
            if (keywords[j] == text.midRef(i, keywords[j].length())) {
                if ( (i+keywords[j].length()) < text.length() && i > 0) {
                    if (text[i + keywords[j].length()].isLetter() || text[i-1].isLetter()) {
                        continue;
                    }
                }
                f.setForeground(Qt::cyan);
                setFormat(i, keywords[j].length(), f);
                i += keywords[j].length();
            }
        }

        for (int j = 0; j < preproc.length(); j++) {
            if (preproc[j] == text.midRef(i, preproc[j].length())) {
                if ( (i+preproc[j].length()) < text.length() && i > 0) {
                    if (text[i + preproc[j].length()].isLetter() || text[i-1].isLetter()) {
                        continue;
                    }
                }
                f.setForeground(Qt::magenta);
                setFormat(i, preproc[j].length(), f);
                i += preproc[j].length();
            }
        }

    }
}

/**
 * Highlight multi-line frontmatter blocks
 *
 * @param text
 */
void MarkdownHighlighter::highlightFrontmatterBlock(const QString& text) {
    // return if there is no frontmatter in this document
    if (document()->firstBlock().text() != "---") {
        return;
    }

    if (text == "---") {
        bool foundEnd = previousBlockState() == HighlighterState::FrontmatterBlock;

        // return if the frontmatter block was already highlighted in previous blocks,
        // there just can be one frontmatter block
        if (!foundEnd && document()->firstBlock() != currentBlock()) {
            return;
        }

        setCurrentBlockState(foundEnd ? HighlighterState::FrontmatterBlockEnd : HighlighterState::FrontmatterBlock);

        QTextCharFormat &maskedFormat =
                _formats[HighlighterState::MaskedSyntax];
        setFormat(0, text.length(), maskedFormat);
    } else if (previousBlockState() == HighlighterState::FrontmatterBlock) {
        setCurrentBlockState(HighlighterState::FrontmatterBlock);
        setFormat(0, text.length(), _formats[HighlighterState::MaskedSyntax]);
    }
}

/**
 * Highlight multi-line comments
 *
 * @param text
 */
void MarkdownHighlighter::highlightCommentBlock(QString text) {
    bool highlight = false;
    text = text.trimmed();
    QString startText = QStringLiteral("<!--");
    QString endText = QStringLiteral("-->");

    // we will skip this case because that is an inline comment and causes
    // troubles here
    if (text.startsWith(startText) && text.contains(endText)) {
        return;
    }

    if (text.startsWith(startText) ||
            (!text.endsWith(endText) &&
                    (previousBlockState() == HighlighterState::Comment))) {
        setCurrentBlockState(HighlighterState::Comment);
        highlight = true;
    } else if (text.endsWith(endText)) {
        highlight = true;
    }

    if (highlight) {
        setFormat(0, text.length(), _formats[HighlighterState::Comment]);
    }
}

/**
 * Format italics, bolds and links in headings(h1-h6)
 *
 * @param format The format that is being applied
 * @param match The regex match
 * @param capturedGroup The captured group
*/
void MarkdownHighlighter::setHeadingStyles(const QTextCharFormat &format,
                                           const QRegularExpressionMatch &match,
                                           const int capturedGroup) {
    QTextCharFormat f;
    int state = currentBlockState();
    if (state == HighlighterState::H1) f = _formats[H1];
    else if (state == HighlighterState::H2) f = _formats[H2];
    else if (state == HighlighterState::H3) f = _formats[H3];
    else if (state == HighlighterState::H4) f = _formats[H4];
    else if (state == HighlighterState::H5) f = _formats[H5];
    else f = _formats[H6];

    if (format == _formats[HighlighterState::Italic]) {
        f.setFontItalic(true);
        setFormat(match.capturedStart(capturedGroup),
                  match.capturedLength(capturedGroup),
                  f);
        return;
    } else if (format == _formats[HighlighterState::Bold]) {
        setFormat(match.capturedStart(capturedGroup),
                  match.capturedLength(capturedGroup),
                  f);
        return;
    }  else if (format == _formats[HighlighterState::Link]) {
        QTextCharFormat link = _formats[Link];
        link.setFontPointSize(f.fontPointSize());
        if (capturedGroup == 1) {
            setFormat(match.capturedStart(capturedGroup),
                  match.capturedLength(capturedGroup),
                  link);
        }
        return;
    }
/**
 * Waqar144
 * TODO: Test this again and make it work correctly
 * Q: Do we even need this in headings?
 */
//disabling these, as these work, but not as good I think.
//    else if (format == _formats[HighlighterState::InlineCodeBlock]) {
//        QTextCharFormat ff;
//        f.setFontPointSize(1.6);
//        f.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
//        f.setBackground(QColor(220, 220, 220));
//        setFormat(match.capturedStart(capturedGroup),
//                  match.capturedEnd(capturedGroup) - 18,
//                  f);
//        return;
//    }
}

/**
 * Highlights the rules from the _highlightingRules list
 *
 * @param text
 */
void MarkdownHighlighter::highlightAdditionalRules(
        const QVector<HighlightingRule> &rules, const QString& text) {
    const QTextCharFormat &maskedFormat = _formats[HighlighterState::MaskedSyntax];

    for(const HighlightingRule &rule : rules) {
            // continue if another current block state was already set if
            // disableIfCurrentStateIsSet is set
            if (rule.disableIfCurrentStateIsSet &&
                    (currentBlockState() != HighlighterState::NoState)) {
                continue;
            }

            QRegularExpression expression(rule.pattern);
            QRegularExpressionMatchIterator iterator = expression.globalMatch(text);
            int capturingGroup = rule.capturingGroup;
            int maskedGroup = rule.maskedGroup;
            QTextCharFormat &format = _formats[rule.state];

            // store the current block state if useStateAsCurrentBlockState
            // is set
            if (iterator.hasNext() && rule.useStateAsCurrentBlockState) {
                setCurrentBlockState(rule.state);
            }

            // find and format all occurrences
            while (iterator.hasNext()) {
                QRegularExpressionMatch match = iterator.next();

                // if there is a capturingGroup set then first highlight
                // everything as MaskedSyntax and highlight capturingGroup
                // with the real format
                if (capturingGroup > 0) {
                    QTextCharFormat currentMaskedFormat = maskedFormat;
                    // set the font size from the current rule's font format
                    if (format.fontPointSize() > 0) {
                        currentMaskedFormat.setFontPointSize(format.fontPointSize());
                    }

                    if ((currentBlockState() == HighlighterState::H1 ||
                        currentBlockState() == HighlighterState::H2 ||
                        currentBlockState() == HighlighterState::H3 ||
                        currentBlockState() == HighlighterState::H4 ||
                        currentBlockState() == HighlighterState::H5 ||
                        currentBlockState() == HighlighterState::H6) &&
                        format != _formats[HighlighterState::InlineCodeBlock]) {
                        //setHeadingStyles(format, match, maskedGroup);

                    } else {

                        setFormat(match.capturedStart(maskedGroup),
                              match.capturedLength(maskedGroup),
                              currentMaskedFormat);
                    }
                }

                if ((currentBlockState() == HighlighterState::H1 ||
                    currentBlockState() == HighlighterState::H2 ||
                    currentBlockState() == HighlighterState::H3 ||
                    currentBlockState() == HighlighterState::H4 ||
                    currentBlockState() == HighlighterState::H5 ||
                    currentBlockState() == HighlighterState::H6) &&
                    format != _formats[HighlighterState::InlineCodeBlock]) {
                    setHeadingStyles(format, match, capturingGroup);

                } else {

                setFormat(match.capturedStart(capturingGroup),
                          match.capturedLength(capturingGroup),
                          format);
                }
            }
        }
}

void MarkdownHighlighter::setHighlightingOptions(const HighlightingOptions options) {
    _highlightingOptions = options;
}
