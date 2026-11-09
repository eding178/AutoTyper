#include "../src/input_plan.h"
#include "../src/text_compare.h"
#include <cassert>
#include <iostream>
#include <string>

// Independent synthetic regression checks. No user-provided SQL is embedded.
int main() {
    using typer::compareText;
    using typer::normalizeNewlines;
    assert(compareText(u"", u"").equal);
    assert(compareText(u"  A\r\n\tB \rC\n", u"  A\n\tB \nC\r\n").equal);
    assert(normalizeNewlines(u"\r\r\n\n") == u"\n\n\n");
    for (const auto* changed : {u"A", u" A", u"  A ", u"  a", u"\tA", u"  A\n"}) {
        assert(!compareText(u"  A", changed).equal);
    }
    assert(!compareText(u"e\u0301", u"\u00e9").equal); // Do not hide Unicode differences.
    assert(!compareText(u"A", u"\ufeffA").equal); // Do not strip a BOM silently.
    assert(!compareText(u"A B", u"A\u00a0B").equal);

    auto first = compareText(u"alpha\r\n  beta", u"alpha\n\tbeta");
    assert(!first.equal && first.line == 2 && first.column == 1 && first.mismatchOffset == 6);
    auto trailing = compareText(u"word ", u"word");
    assert(!trailing.equal && trailing.line == 1 && trailing.column == 5);
    auto missingNewline = compareText(u"abc\n", u"abc");
    assert(!missingNewline.equal && missingNewline.line == 1 && missingNewline.column == 4);
    auto extraNewline = compareText(u"a\n", u"a\n\n");
    assert(!extraNewline.equal && extraNewline.line == 2 && extraNewline.column == 1);
    auto emoji = compareText(u"A\U0001f600B", u"A\U0001f601B");
    assert(!emoji.equal && emoji.mismatchOffset == 1 && emoji.column == 2);
    auto afterEmoji = compareText(u"A\U0001f600B", u"A\U0001f600X");
    assert(!afterEmoji.equal && afterEmoji.mismatchOffset == 3 && afterEmoji.column == 3);
    auto incompleteEmoji = compareText(u"A\U0001f600", std::u16string{u'A', char16_t(0xd83d)});
    assert(!incompleteEmoji.equal && incompleteEmoji.column == 2 && incompleteEmoji.mismatchOffset == 1);

    std::u16string fixture;
    for (unsigned i = 0; i < 225; ++i) {
        fixture.append((i % 4) * 4, u' ');
        fixture += u"row_";
        const auto index = std::to_string(i);
        for (char c : index) fixture += static_cast<char16_t>(c);
        fixture += u" = 'synthetic \u00e1\u00f1\u20ac \U0001f600 payload with spaces and sentinel';";
        fixture += (i % 3 == 0 ? u"\r\n" : i % 3 == 1 ? u"\n" : u"\r");
    }
    auto plan = typer::makePlan(fixture);
    assert(plan);
    std::u16string delivered;
    for (const auto& stroke : plan.strokes) {
        if (stroke.kind == typer::Kind::Enter) delivered += u'\n';
        else if (stroke.kind == typer::Kind::Tab) delivered += u'\t';
        else {
            delivered += static_cast<char16_t>(stroke.first);
            if (stroke.units == 2) delivered += static_cast<char16_t>(stroke.second);
        }
    }
    assert(compareText(fixture, delivered).equal);
    assert(delivered == normalizeNewlines(fixture));
    const auto original = delivered;
    std::size_t loss = delivered.find(u"payload", 8400);
    assert(loss != std::u16string::npos);
    delivered.erase(loss, 3);
    const auto lost = compareText(fixture, delivered);
    assert(!lost.equal && lost.mismatchOffset == loss);
    assert(lost.expectedLength == lost.actualLength + 3);
    delivered = original;
    const auto secondLine = delivered.find(u'\n') + 1;
    delivered.insert(secondLine, 4, u'\t');
    const auto indented = compareText(fixture, delivered);
    assert(!indented.equal && indented.line == 2 && indented.column == 1);
    assert(indented.actualLength == indented.expectedLength + 4);
    std::cout << "PASS: exact whitespace/Unicode comparison, CR/LF equivalence, mismatch positions, "
                 "225-line synthetic roundtrip, late dropped characters and added indentation\n";
}
